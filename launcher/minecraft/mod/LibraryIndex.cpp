// SPDX-FileCopyrightText: 2026 Kerilom <89039577+Gleb-Lisovskiy@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "LibraryIndex.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QObject>
#include <QSaveFile>

#include "FileSystem.h"

namespace {
constexpr int kManifestVersion = 1;

QString typeDirName(LibraryIndex::Type type)
{
    switch (type) {
        case LibraryIndex::Type::Mod:
            return "mods";
        case LibraryIndex::Type::ResourcePack:
            return "resourcepacks";
        case LibraryIndex::Type::ShaderPack:
            return "shaderpacks";
        case LibraryIndex::Type::TexturePack:
            return "texturepacks";
    }
    return "mods";
}

/** Files live directly under <type>/ in the library root, but a manifest is meant to be
 *  hand-editable: an absolute path or a ".." in it would otherwise let an entry point anywhere on
 *  the filesystem, so only known type directories are honoured. */
bool isSafeRelativePath(const QString& path)
{
    if (path.isEmpty() || QDir::isAbsolutePath(path) || path.contains("..")) {
        return false;
    }

    const auto slash = path.indexOf('/');
    if (slash <= 0 || slash == path.size() - 1) {
        return false;
    }

    static const QStringList knownDirs{ typeDirName(LibraryIndex::Type::Mod),
                                        typeDirName(LibraryIndex::Type::ResourcePack),
                                        typeDirName(LibraryIndex::Type::ShaderPack),
                                        typeDirName(LibraryIndex::Type::TexturePack) };
    return knownDirs.contains(path.left(slash));
}

/** QJsonArray has no toStringList(); entries that are not strings are skipped rather than
 *  turning into empty strings that later look like real metadata. */
QStringList toStringList(const QJsonValue& value)
{
    QStringList result;
    for (const auto& item : value.toArray()) {
        if (item.isString()) {
            result.append(item.toString());
        }
    }
    return result;
}
}  // namespace

LibraryIndex::LibraryIndex(QString rootDir) : m_root(std::move(rootDir)) {}

QString LibraryIndex::typeDirName(Type type)
{
    return ::typeDirName(type);
}

QString LibraryIndex::makeId(Type type, const ModDetails& details, const QString& fileName)
{
    // The version is part of the identity so that two versions of one mod can coexist, which is
    // what makes the library useful for building pack variants. Without a version the file name
    // carries the identity instead.
    const auto identity = details.mod_id.isEmpty() ? fileName : details.mod_id + "\n" + details.version;
    auto digest = QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha1).toHex();
    return QString("%1-%2").arg(typeDirName(type), digest.left(12));
}

QString LibraryIndex::absoluteFilePath(const Entry& entry) const
{
    return FS::PathCombine(m_root, entry.file);
}

QJsonObject LibraryIndex::toJson(const Entry& entry)
{
    QJsonObject obj;
    obj["id"] = entry.id;
    obj["type"] = typeDirName(entry.type);
    obj["file"] = entry.file;
    if (entry.addedAt.isValid()) {
        obj["addedAt"] = entry.addedAt.toString(Qt::ISODate);
    }

    const ModDetails& d = entry.details;
    if (!d.mod_id.isEmpty()) {
        obj["modId"] = d.mod_id;
    }
    if (!d.name.isEmpty()) {
        obj["name"] = d.name;
    }
    if (!d.version.isEmpty()) {
        obj["version"] = d.version;
    }
    if (!d.mcversion.isEmpty()) {
        obj["mcVersion"] = d.mcversion;
    }
    if (!d.description.isEmpty()) {
        obj["description"] = d.description;
    }
    if (!d.homeurl.isEmpty()) {
        obj["homepage"] = d.homeurl;
    }
    if (!d.authors.isEmpty()) {
        obj["authors"] = QJsonArray::fromStringList(d.authors);
    }
    if (!d.dependencies.isEmpty()) {
        obj["dependencies"] = QJsonArray::fromStringList(d.dependencies);
    }

    if (!entry.projectId.isEmpty()) {
        QJsonObject source;
        source["provider"] = ModPlatform::ProviderCapabilities::name(entry.provider);
        source["projectId"] = entry.projectId;
        source["fileId"] = entry.fileId;
        obj["source"] = source;
    }

    return obj;
}

LibraryIndex::Entry LibraryIndex::fromJson(const QJsonObject& obj)
{
    Entry entry;
    entry.id = obj.value("id").toString();
    entry.file = obj.value("file").toString();

    const auto typeName = obj.value("type").toString();
    if (typeName == "resourcepacks") {
        entry.type = Type::ResourcePack;
    } else if (typeName == "shaderpacks") {
        entry.type = Type::ShaderPack;
    } else if (typeName == "texturepacks") {
        entry.type = Type::TexturePack;
    } else {
        entry.type = Type::Mod;
    }

    ModDetails& d = entry.details;
    d.mod_id = obj.value("modId").toString();
    d.name = obj.value("name").toString();
    d.version = obj.value("version").toString();
    d.mcversion = obj.value("mcVersion").toString();
    d.description = obj.value("description").toString();
    d.homeurl = obj.value("homepage").toString();
    d.authors = toStringList(obj.value("authors"));
    d.dependencies = toStringList(obj.value("dependencies"));

    const auto source = obj.value("source").toObject();
    if (!source.isEmpty()) {
        entry.projectId = source.value("projectId").toString();
        entry.fileId = source.value("fileId").toString();
    }

    entry.addedAt = QDateTime::fromString(obj.value("addedAt").toString(), Qt::ISODate);

    return entry;
}

void LibraryIndex::flagMissingFiles()
{
    for (auto& entry : m_entries) {
        entry.broken = !isSafeRelativePath(entry.file) || !QFileInfo::exists(absoluteFilePath(entry));
    }
}

bool LibraryIndex::load(QString* error)
{
    m_entries.clear();

    const QString manifest = manifestPath();
    if (!QFileInfo::exists(manifest)) {
        return true;  // A library that was never used has no manifest, which is not an error.
    }

    QFile file(manifest);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }

    QJsonParseError parseError{};
    const auto doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error) {
            *error = parseError.errorString();
        }
        return false;
    }

    const auto entries = doc.object().value("entries").toArray();
    for (const auto& value : entries) {
        auto entry = fromJson(value.toObject());
        if (entry.id.isEmpty() || entry.file.isEmpty()) {
            continue;  // Without an id or a path there is nothing to address the entry by.
        }
        m_entries.append(std::move(entry));
    }

    flagMissingFiles();
    return true;
}

bool LibraryIndex::save(QString* error) const
{
    QJsonArray entries;
    for (const auto& entry : m_entries) {
        entries.append(toJson(entry));
    }

    QJsonObject root;
    root["version"] = kManifestVersion;
    root["entries"] = entries;

    // QSaveFile writes to a temporary and renames, so an interrupted save leaves the previous
    // manifest intact rather than a truncated one.
    QSaveFile file(manifestPath());
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }
    return true;
}

bool LibraryIndex::importPayload(Type type, const QString& sourceFilePath, QString& relativePath, QString* error)
{
    const QFileInfo source(sourceFilePath);
    if (!source.isFile()) {
        if (error) {
            *error = QObject::tr("No such file: %1").arg(sourceFilePath);
        }
        return false;
    }

    const QDir typeDir(FS::PathCombine(m_root, typeDirName(type)));
    if (!typeDir.mkpath(".")) {
        if (error) {
            *error = QObject::tr("Could not create the library directory: %1").arg(typeDir.absolutePath());
        }
        return false;
    }

    // Strip characters the destination filesystem would reject rather than silently mangling the
    // name, keeping the extension so mod loaders still recognise the file.
    QString name = FS::RemoveInvalidPathChars(source.fileName());
    if (name.isEmpty()) {
        name = makeId(type, ModDetails(), source.fileName()) + source.suffix();
    }
    const QString target = typeDir.absoluteFilePath(name);

    if (QFileInfo::exists(target) && QFileInfo(target).size() == source.size()) {
        relativePath = QDir(m_root).relativeFilePath(target);
        return true;  // Identical payload already present, keep the single copy.
    }

    // overwrite(true) is required: FS::copy refuses to replace an existing file otherwise, and an
    // updated build of a mod usually keeps the same file name.
    if (!FS::copy(sourceFilePath, target).overwrite(true)()) {
        if (error) {
            *error = QObject::tr("Could not copy %1 into the library").arg(sourceFilePath);
        }
        return false;
    }

    relativePath = QDir(m_root).relativeFilePath(target);
    return true;
}

bool LibraryIndex::add(Entry& entry, const QString& sourceFilePath, QString* error)
{
    Entry updated = entry;
    updated.id = updated.id.isEmpty() ? makeId(updated.type, updated.details, QFileInfo(sourceFilePath).fileName())
                                      : updated.id;
    if (!updated.addedAt.isValid()) {
        updated.addedAt = QDateTime::currentDateTime();
    }

    QString previousFile;
    if (const auto* existing = find(updated.id); existing) {
        previousFile = existing->file;
    }

    if (!importPayload(updated.type, sourceFilePath, updated.file, error)) {
        return false;
    }

    for (int i = 0; i < m_entries.size(); i++) {
        if (m_entries[i].id == updated.id) {
            m_entries[i] = updated;
            break;
        }
    }
    if (!find(updated.id)) {
        m_entries.append(updated);
    }

    // The replaced entry may have been the only reference to its payload.
    if (!previousFile.isEmpty() && previousFile != updated.file) {
        const bool stillUsed = std::any_of(m_entries.cbegin(), m_entries.cend(), [&previousFile](const Entry& e) {
            return e.file == previousFile;
        });
        if (!stillUsed) {
            QFile::remove(FS::PathCombine(m_root, previousFile));
        }
    }

    if (!save(error)) {
        return false;
    }

    flagMissingFiles();
    entry = updated;
    return true;
}

bool LibraryIndex::remove(const QString& id, QString* error)
{
    const auto it = std::find_if(m_entries.begin(), m_entries.end(), [&id](const Entry& e) { return e.id == id; });
    if (it == m_entries.end()) {
        return true;
    }

    const QString file = it->file;
    m_entries.erase(it);

    const bool stillUsed = std::any_of(m_entries.cbegin(), m_entries.cend(), [&file](const Entry& e) { return e.file == file; });
    if (!stillUsed && isSafeRelativePath(file)) {
        QFile::remove(FS::PathCombine(m_root, file));
    }

    return save(error);
}

bool LibraryIndex::update(const Entry& entry, QString* error)
{
    auto* existing = find(entry.id);
    if (!existing) {
        if (error) {
            *error = QObject::tr("No such library entry: %1").arg(entry.id);
        }
        return false;
    }

    existing->details = entry.details;

    if (!save(error)) {
        return false;
    }

    // A renamed or re-versioned entry could in principle point at a different payload, though the
    // path is untouched here, so re-check instead of trusting the flag set at load time.
    flagMissingFiles();
    return true;
}

LibraryIndex::Entry* LibraryIndex::find(const QString& id)
{
    const auto it = std::find_if(m_entries.begin(), m_entries.end(), [&id](const Entry& e) { return e.id == id; });
    return it == m_entries.end() ? nullptr : &*it;
}

const LibraryIndex::Entry* LibraryIndex::find(const QString& id) const
{
    const auto it = std::find_if(m_entries.cbegin(), m_entries.cend(), [&id](const Entry& e) { return e.id == id; });
    return it == m_entries.cend() ? nullptr : &*it;
}