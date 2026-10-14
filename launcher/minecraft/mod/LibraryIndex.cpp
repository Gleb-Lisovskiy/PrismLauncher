// SPDX-FileCopyrightText: 2026 Kerilom <89039577+Gleb-Lisovskiy@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "LibraryIndex.h"

#include <algorithm>

#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QSaveFile>

#include "FileSystem.h"
#include "minecraft/mod/Mod.h"
#include "minecraft/mod/tasks/LocalModParseTask.h"
#include "minecraft/mod/tasks/LocalResourceParse.h"

namespace {
/** Reads a mod's own metadata out of the jar: name, mod id, version and description.
 *
 *  A file dropped into the library by hand has none of this until something opens it, and the file
 *  name is a poor stand-in for any of it. Returns false when the file is not a mod we can read, or
 *  carries no metadata we understand; the caller then falls back to the file name. */
bool readModDetails(const QFileInfo& file, ModDetails& details)
{
    Mod mod{ file };
    if (!ModUtils::process(mod, ModUtils::ProcessingLevel::BasicInfoOnly) || !mod.valid()) {
        return false;
    }

    details = mod.details();
    return true;
}

bool isTypeDirName(const QString& name)
{
    for (const auto type :
         { LibraryIndex::Type::Mod, LibraryIndex::Type::ResourcePack, LibraryIndex::Type::ShaderPack, LibraryIndex::Type::TexturePack }) {
        if (name == LibraryIndex::typeDirName(type)) {
            return true;
        }
    }
    return false;
}

/** Works out what a payload is by opening it.
 *
 *  A file that is not in one of the four directories has to say what it is itself, and the
 *  launcher already knows how to ask: this is the same identification it uses for a file dropped
 *  onto an instance, so an archive in the root of the library ends up in the tab it would have
 *  landed in there. Returns nothing for the kinds of file that cannot be installed into an
 *  instance, such as a data pack or a world save. */
std::optional<LibraryIndex::Type> sniffPayloadType(const QFileInfo& file)
{
    switch (ResourceUtils::identify(file)) {
        case ModPlatform::ResourceType::Mod:
            return LibraryIndex::Type::Mod;
        case ModPlatform::ResourceType::ResourcePack:
            return LibraryIndex::Type::ResourcePack;
        case ModPlatform::ResourceType::TexturePack:
            return LibraryIndex::Type::TexturePack;
        case ModPlatform::ResourceType::ShaderPack:
            return LibraryIndex::Type::ShaderPack;
        default:
            return std::nullopt;
    }
}

/** What a notes file says: the description the user reads, and the few facts about the payload
 *  that the payload itself does not carry. */
struct Notes {
    QString markdown;
    LibraryIndex::Origin origin;
    QString mcVersion;
    QString name;
    QString version;
};

/** Splits a notes file into its front matter and its description.
 *
 *  The front matter is only front matter if the file starts with "---" and has a matching line
 *  later on. Anything else is the description as written, because guessing wrong there would eat
 *  the user's text rather than a field of it. */
Notes parseNotes(const QString& text)
{
    Notes notes;
    const auto lines = text.split('\n');

    int end = -1;
    if (!lines.isEmpty() && lines.first().trimmed() == "---") {
        for (int i = 1; i < lines.size(); i++) {
            if (lines[i].trimmed() == "---") {
                end = i;
                break;
            }
        }
    }

    if (end < 0) {
        notes.markdown = text.trimmed();
        return notes;
    }

    for (int i = 1; i < end; i++) {
        const auto line = lines[i];
        const auto colon = line.indexOf(':');
        if (colon <= 0) {
            continue;
        }

        const auto key = line.left(colon).trimmed().toLower();
        const auto value = line.mid(colon + 1).trimmed();
        if (value.isEmpty()) {
            continue;
        }

        if (key == "provider") {
            notes.origin.provider = value.compare("modrinth", Qt::CaseInsensitive) == 0 ? ModPlatform::ResourceProvider::MODRINTH
                                                                                        : ModPlatform::ResourceProvider::FLAME;
        } else if (key == "project-id") {
            notes.origin.projectId = value;
        } else if (key == "file-id") {
            notes.origin.fileId = value;
        } else if (key == "mc-version") {
            notes.mcVersion = value;
        } else if (key == "name") {
            notes.name = value;
        } else if (key == "version") {
            notes.version = value;
        }
    }

    notes.markdown = lines.mid(end + 1).join('\n').trimmed();
    return notes;
}

QString renderNotes(const LibraryIndex::Entry& entry, const QString& markdown)
{
    const auto& origin = entry.origin;
    const auto& mcVersion = entry.details.mcversion;

    QString text;
    if (origin.isValid() || !mcVersion.isEmpty()) {
        text += "---\n";
        if (origin.isValid()) {
            text += QString("provider: %1\n").arg(ModPlatform::ProviderCapabilities::name(origin.provider));
            text += QString("project-id: %1\n").arg(origin.projectId);
            if (!origin.fileId.isEmpty()) {
                text += QString("file-id: %1\n").arg(origin.fileId);
            }
        }
        // The versions a project says its file is for, which a jar does not record anywhere the
        // page's filter can read. Writing them down here is what keeps the file from being offered
        // to an instance it was never built for.
        if (!mcVersion.isEmpty()) {
            text += QString("mc-version: %1\n").arg(mcVersion);
        }
        text += "---\n";
    }

    if (!markdown.isEmpty()) {
        text += markdown;
        text += "\n";
    }

    return text;
}
}  // namespace

LibraryIndex::LibraryIndex(QString rootDir) : m_root(std::move(rootDir)) {}

QString LibraryIndex::typeDirName(Type type)
{
    switch (type) {
        case Type::Mod:
            return "mods";
        case Type::ResourcePack:
            return "resourcepacks";
        case Type::ShaderPack:
            return "shaderpacks";
        case Type::TexturePack:
            return "texturepacks";
    }
    return "mods";
}

QString LibraryIndex::notesPathFor(const QString& payloadPath)
{
    return payloadPath + ".md";
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

bool LibraryIndex::rescan(QString* error)
{
    if (QFileInfo::exists(m_root) && !QFileInfo(m_root).isReadable()) {
        if (error) {
            *error = QObject::tr("The local files folder cannot be read: %1").arg(m_root);
        }
        return false;
    }

    m_entries.clear();

    for (const auto type : { Type::Mod, Type::ResourcePack, Type::ShaderPack, Type::TexturePack }) {
        scanDirectory(QDir(FS::PathCombine(m_root, typeDirName(type))), type);
    }
    // Everything else has to say what it is itself. This pass skips the directories above, which
    // have already been read, so a payload cannot end up listed twice.
    scanDirectory(QDir(m_root), std::nullopt);

    return true;
}

void LibraryIndex::scanDirectory(const QDir& dir, std::optional<Type> insideType)
{
    if (!dir.exists()) {
        return;
    }

    for (const auto& info : dir.entryInfoList(QDir::Files | QDir::NoSymLinks)) {
        // A notes file describes the payload it is named after; it is not a payload itself.
        if (info.fileName().endsWith(".md", Qt::CaseInsensitive)) {
            continue;
        }

        const auto path = info.absoluteFilePath();

        // Whatever opening the file told us last time is only worth keeping while the file itself
        // is unchanged. Everything that is not put back here is forgotten, so the cache cannot
        // outgrow the library.
        auto facts = m_cache.take(path);
        if (facts.size != info.size() || facts.modified != info.lastModified()) {
            facts = CachedPayload{};
            facts.size = info.size();
            facts.modified = info.lastModified();
        }

        auto type = insideType;
        if (!type) {
            if (!facts.sniffedType) {
                facts.sniffedType = sniffPayloadType(info);
            }
            type = facts.sniffedType;
        }
        if (!type) {
            m_cache.insert(path, facts);
            continue;  // Nothing this library can install anywhere.
        }

        addScannedFile(info, *type, QDir(m_root).relativeFilePath(path), facts);
        m_cache.insert(path, facts);
    }

    for (const auto& subDir : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks)) {
        // The four type directories are read on their own, from the library root.
        if (!insideType && isTypeDirName(subDir.fileName())) {
            continue;
        }
        scanDirectory(QDir(subDir.absoluteFilePath()), insideType);
    }
}

void LibraryIndex::addScannedFile(const QFileInfo& file, Type type, const QString& relative, CachedPayload& facts)
{
    Entry entry;
    entry.type = type;
    entry.file = relative;
    entry.addedAt = file.lastModified();

    if (type == Type::Mod) {
        if (!facts.details) {
            ModDetails read;
            if (readModDetails(file, read)) {
                facts.details = read;
            } else {
                facts.details = ModDetails{};  // Read, and there was nothing to find.
            }
        }
        entry.details = *facts.details;
    }

    const auto notesPath = notesPathFor(file.absoluteFilePath());
    if (QFileInfo::exists(notesPath)) {
        QFile notesFile(notesPath);
        if (notesFile.open(QIODevice::ReadOnly)) {
            const auto notes = parseNotes(QString::fromUtf8(notesFile.readAll()));
            entry.notes = notes.markdown;
            entry.origin = notes.origin;

            // The notes are allowed to correct the payload, which is the only way to fix a name
            // or a version that the file itself gets wrong.
            if (!notes.name.isEmpty()) {
                entry.details.name = notes.name;
            }
            if (!notes.version.isEmpty()) {
                entry.details.version = notes.version;
            }
            if (!notes.mcVersion.isEmpty()) {
                entry.details.mcversion = notes.mcVersion;
            }
        }
    }

    if (entry.details.name.isEmpty()) {
        entry.details.name = file.completeBaseName();
    }

    // The id is derived after the metadata is known, because for mods it is the mod id.
    entry.id = makeId(type, entry.details, file.fileName());
    if (const auto* clash = find(entry.id)) {
        // Two payloads claiming the same mod and version, which a folder people copy into by hand
        // can easily end up with. One of them has to lose, and it should not be a coin toss.
        qWarning() << "Ignoring" << entry.file << "because" << clash->file << "is already the same resource";
        return;
    }

    m_entries.append(std::move(entry));
}

bool LibraryIndex::add(Entry& entry, const QString& sourceFilePath, QString* error)
{
    const QFileInfo source(sourceFilePath);
    if (!source.isFile()) {
        if (error) {
            *error = QObject::tr("No such file: %1").arg(sourceFilePath);
        }
        return false;
    }

    const QDir typeDir(FS::PathCombine(m_root, typeDirName(entry.type)));
    if (!typeDir.mkpath(".")) {
        if (error) {
            *error = QObject::tr("Could not create the library directory: %1").arg(typeDir.absolutePath());
        }
        return false;
    }

    // The caller usually knows the name the file should have in the library - the one the project
    // published it under - so that wins over the name of the temporary copy it arrived as.
    const auto requested = entry.file.isEmpty() ? source.fileName() : QFileInfo(entry.file).fileName();

    // Strip characters the destination filesystem would reject rather than silently mangling the
    // name, keeping the extension so mod loaders still recognise the file.
    QString name = FS::RemoveInvalidPathChars(requested);
    if (name.isEmpty()) {
        name = makeId(entry.type, ModDetails(), source.fileName()) + source.suffix();
    }
    const QString target = typeDir.absoluteFilePath(name);

    // overwrite(true) is required: FS::copy refuses to replace an existing file otherwise, and an
    // updated build of a mod usually keeps the same file name.
    if (!FS::copy(sourceFilePath, target).overwrite(true)()) {
        if (error) {
            *error = QObject::tr("Could not copy %1 into the library").arg(sourceFilePath);
        }
        return false;
    }

    entry.file = QDir(m_root).relativeFilePath(target);
    entry.addedAt = QFileInfo(target).lastModified();

    // A payload the launcher fetched for the library is still worth opening: what the project
    // calls a version and what the jar calls it are not always the same thing.
    if (entry.type == Type::Mod && entry.details.mod_id.isEmpty() && entry.details.version.isEmpty()) {
        ModDetails read;
        if (readModDetails(QFileInfo(target), read)) {
            entry.details = read;
        }
    }
    if (entry.details.name.isEmpty()) {
        entry.details.name = QFileInfo(target).completeBaseName();
    }

    entry.id = entry.id.isEmpty() ? makeId(entry.type, entry.details, name) : entry.id;

    if (!writeNotes(entry, error)) {
        return false;
    }

    if (auto* existing = find(entry.id)) {
        // An updated build usually arrives under a new file name. Keeping the old payload would
        // leave two copies of one mod and version in the folder, which the next scan would then
        // only report as a clash, so it goes with its notes.
        if (existing->file != entry.file) {
            const auto oldPath = absoluteFilePath(*existing);
            QFile::remove(oldPath);
            QFile::remove(notesPathFor(oldPath));
        }
        *existing = entry;
    } else {
        m_entries.append(entry);
    }
    return true;
}

bool LibraryIndex::writeNotes(const Entry& entry, QString* error)
{
    if (!entry.origin.isValid() && entry.details.mcversion.isEmpty()) {
        // Nothing the notes file would add. A description that does not exist yet is the user's to
        // write, and an empty file next to every payload would only be in the way.
        return true;
    }

    const auto path = notesPathFor(absoluteFilePath(entry));

    // A description the user has written belongs to them; only the front matter is rewritten.
    QString markdown = entry.notes;
    if (QFileInfo::exists(path)) {
        QFile existing(path);
        if (existing.open(QIODevice::ReadOnly)) {
            markdown = parseNotes(QString::fromUtf8(existing.readAll())).markdown;
        }
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }

    file.write(renderNotes(entry, markdown).toUtf8());
    if (!file.commit()) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }

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
