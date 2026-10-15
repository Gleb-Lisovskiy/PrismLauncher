// SPDX-FileCopyrightText: 2026 Kerilom <89039577+Gleb-Lisovskiy@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "InstanceTemplate.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QUuid>

#include <algorithm>
#include <utility>

#include "FileSystem.h"
#include "minecraft/Component.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "net/Mode.h"
#include "settings/SettingsObject.h"

namespace {

/** A group of instance settings that hang off a single "Override..." switch.
 *
 *  A template only carries what the instance deliberately overrode, never inherited values: an
 *  instance that follows the global memory setting should produce a template that follows it too,
 *  rather than freezing whatever the global setting happened to be on the day it was saved. */
struct SettingsGroup {
    const char* gate;
    QStringList keys;
};

const QList<SettingsGroup>& settingsGroups()
{
    static const QList<SettingsGroup> groups = {
        { "OverrideJavaLocation", { "JavaPath", "IgnoreJavaCompatibility" } },
        { "OverrideJavaArgs", { "JvmArgs" } },
        { "OverrideWindow", { "LaunchMaximized", "MinecraftWinWidth", "MinecraftWinHeight" } },
        { "OverrideMemory", { "MinMemAlloc", "MaxMemAlloc", "PermGen", "LowMemWarning" } },
        { "OverrideNativeWorkarounds", { "UseNativeOpenAL", "CustomOpenALPath", "UseNativeGLFW", "CustomGLFWPath" } },
        { "OverridePerformance", { "EnableFeralGamemode", "EnableMangoHud", "UseDiscreteGpu", "UseZink" } },
        { "OverrideMiscellaneous", { "CloseAfterLaunch", "QuitAfterGameStop" } },
        { "OverrideLegacySettings", { "OnlineFixes" } },
        { "OverrideEnv", { "Env" } },
    };
    return groups;
}

/** Settings that have no override switch but are still worth carrying over. */
const QStringList& standaloneSettings()
{
    static const QStringList keys = { "AutomaticJava" };
    return keys;
}

QList<InstanceTemplate::Component> readComponents(BaseInstance& instance)
{
    QList<InstanceTemplate::Component> out;

    auto* mc = qobject_cast<MinecraftInstance*>(&instance);
    if (!mc)
        return out;

    auto* profile = mc->getPackProfile();
    if (!profile)
        return out;

    // The component list is loaded lazily: an instance the user has not opened yet reports no
    // components at all. Without loading it here a template would remember no game version, and an
    // instance built from it would have nothing to install.
    if (!profile->isLoaded())
        profile->reload(Net::Mode::Offline);

    for (int i = 0; i < profile->rowCount(); i++) {
        auto component = profile->getComponent(static_cast<size_t>(i));
        if (!component || !component->isEnabled())
            continue;
        out.append({ component->getID(), component->getVersion() });
    }
    return out;
}

/** Lists every file and folder under a directory, descending into subdirectories.
 *
 *  Hidden entries are skipped, matching the launcher's own resource lists, and an entry keeps its
 *  path relative to the game directory so nested layouts survive the round trip. Folders are
 *  reported before their contents, and an empty folder is reported too, so the save dialog can show
 *  the instance's shape rather than only the leaves that happen to hold files.
 *
 *  Within a directory, folders come before files, each in name order: the tree the picker builds
 *  from this reads better when the folders of a level sit together instead of being scattered among
 *  the files. A folder is still listed before anything inside it, so the shape is walked top down.
 *
 *  Returns false when the caller's progress callback asks to stop. */
bool collectContentFiles(const QString& dir,
                         const QString& relative,
                         QList<InstanceTemplate::File>& out,
                         const InstanceTemplate::FileScanProgress& progress,
                         int& sinceReport)
{
    QDir qdir(dir);
    qdir.setFilter(QDir::Readable | QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs);
    qdir.setSorting(QDir::Name | QDir::IgnoreCase | QDir::LocaleAware);

    // The directory listing already comes in name order, so a stable partition on the folder flag
    // gives folders first and files second without disturbing the order within either group.
    QFileInfoList entries = qdir.entryInfoList();
    std::stable_partition(entries.begin(), entries.end(), [](const QFileInfo& entry) { return entry.isDir(); });

    for (const QFileInfo& entry : entries) {
        const QString name = entry.fileName();
        const QString path = relative.isEmpty() ? name : relative + QLatin1Char('/') + name;
        if (entry.isDir()) {
            out.append({ path, {}, true });
            if (!collectContentFiles(entry.absoluteFilePath(), path, out, progress, sinceReport))
                return false;
        } else if (entry.isFile()) {
            out.append({ path, entry.absoluteFilePath(), false });
        }

        // Reporting on every entry would spend more time repainting the dialog than reading the
        // folder, so the callback only hears about it in batches.
        if (progress && ++sinceReport >= 64) {
            sinceReport = 0;
            if (!progress(static_cast<int>(out.size())))
                return false;
        }
    }
    return true;
}

}  // namespace

QString InstanceTemplate::minecraftVersion() const
{
    for (const auto& component : components) {
        if (component.uid == QStringLiteral("net.minecraft"))
            return component.version;
    }
    return {};
}

QString InstanceTemplate::loaderId() const
{
    for (const auto& component : components) {
        if (!loaderBrand(component.uid).isEmpty())
            return component.uid;
    }
    return {};
}

QString InstanceTemplate::loaderVersion() const
{
    const QString loader = loaderId();
    if (loader.isEmpty())
        return {};
    for (const auto& component : components) {
        if (component.uid == loader)
            return component.version;
    }
    return {};
}

QString InstanceTemplate::loaderBrand(const QString& uid)
{
    auto it = ::Component::KNOWN_MODLOADERS.find(uid);
    if (it == ::Component::KNOWN_MODLOADERS.cend())
        return {};

    switch (it->type) {
        case ModPlatform::Fabric:
            return QStringLiteral("Fabric");
        case ModPlatform::Forge:
            return QStringLiteral("Forge");
        case ModPlatform::NeoForge:
            return QStringLiteral("NeoForge");
        case ModPlatform::Quilt:
            return QStringLiteral("Quilt");
        case ModPlatform::LiteLoader:
            return QStringLiteral("LiteLoader");
        default:
            return {};
    }
}

QMap<QString, QVariant> InstanceTemplate::readSettings(BaseInstance& instance)
{
    QMap<QString, QVariant> out;
    auto* settings = instance.settings();
    if (!settings)
        return out;

    for (const auto& group : settingsGroups()) {
        if (!settings->get(group.gate).toBool())
            continue;
        out.insert(group.gate, true);
        for (const auto& key : group.keys)
            out.insert(key, settings->get(key));
    }
    for (const auto& key : standaloneSettings())
        out.insert(key, settings->get(key));

    return out;
}

QList<InstanceTemplate::File> InstanceTemplate::contentFiles(const QString& gameRoot, const FileScanProgress& progress)
{
    QList<File> out;

    int sinceReport = 0;
    if (QDir(gameRoot).exists())
        collectContentFiles(gameRoot, {}, out, progress, sinceReport);
    if (progress)
        progress(static_cast<int>(out.size()));
    return out;
}

void InstanceTemplate::applySettings(SettingsObject* settings, const QMap<QString, QVariant>& values)
{
    if (!settings)
        return;
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        if (!settings->set(it.key(), it.value()))
            qWarning() << "Template wanted to set unknown instance setting" << it.key();
    }
}

InstanceTemplate InstanceTemplate::captureFrom(BaseInstance& instance, const QString& name, const FileScanProgress& progress)
{
    InstanceTemplate tpl;
    tpl.name = name;
    tpl.iconKey = instance.iconKey();
    tpl.createdAt = QDateTime::currentDateTime();
    tpl.components = readComponents(instance);
    tpl.settings = readSettings(instance);
    if (auto* mc = qobject_cast<MinecraftInstance*>(&instance))
        tpl.files = contentFiles(mc->gameRoot(), progress);
    return tpl;
}

QJsonObject InstanceTemplate::toJson() const
{
    QJsonObject obj;
    obj.insert("id", id);
    obj.insert("name", name);
    if (!versionName.isEmpty())
        obj.insert("version-name", versionName);
    if (!description.isEmpty())
        obj.insert("description", description);
    obj.insert("icon", iconKey);
    if (createdAt.isValid())
        obj.insert("created-at", createdAt.toString(Qt::ISODate));

    QJsonArray componentsArr;
    for (const auto& component : components) {
        QJsonObject entry;
        entry.insert("uid", component.uid);
        entry.insert("version", component.version);
        componentsArr.append(entry);
    }
    obj.insert("components", componentsArr);

    QJsonArray filesArr;
    for (const auto& file : files) {
        QJsonObject entry;
        entry.insert("path", file.path);
        if (file.isDir)
            entry.insert("dir", true);
        filesArr.append(entry);
    }
    obj.insert("files", filesArr);

    QJsonObject settingsObj;
    for (auto it = settings.constBegin(); it != settings.constEnd(); ++it)
        settingsObj.insert(it.key(), QJsonValue::fromVariant(it.value()));
    obj.insert("settings", settingsObj);

    return obj;
}

InstanceTemplate InstanceTemplate::fromJson(const QJsonObject& json)
{
    InstanceTemplate tpl;
    tpl.id = json.value("id").toString();
    tpl.name = json.value("name").toString();
    tpl.versionName = json.value("version-name").toString();
    tpl.description = json.value("description").toString();
    tpl.iconKey = json.value("icon").toString(QStringLiteral("default"));
    tpl.createdAt = QDateTime::fromString(json.value("created-at").toString(), Qt::ISODate);

    for (const auto& value : json.value("components").toArray()) {
        const auto entry = value.toObject();
        const QString uid = entry.value("uid").toString();
        const QString version = entry.value("version").toString();
        if (!uid.isEmpty() && !version.isEmpty())
            tpl.components.append({ uid, version });
    }

    for (const auto& value : json.value("files").toArray()) {
        const auto entry = value.toObject();
        QString path = entry.value("path").toString();
        const bool isDir = entry.value("dir").toBool();

        // Templates written before the picker covered the whole game directory stored a path relative
        // to a content folder named by "type". The path is now relative to the game directory, so the
        // old type simply becomes its first component.
        const QString legacyType = entry.value("type").toString();
        if (!legacyType.isEmpty())
            path = path.isEmpty() ? legacyType : legacyType + QLatin1Char('/') + path;

        // An entry without a path names nothing that can be laid down, file or folder.
        if (path.isEmpty())
            continue;
        tpl.files.append({ path, {}, isDir });
    }

    const auto settingsObj = json.value("settings").toObject();
    for (auto it = settingsObj.constBegin(); it != settingsObj.constEnd(); ++it)
        tpl.settings.insert(it.key(), it.value().toVariant());

    return tpl;
}

InstanceTemplateStore::InstanceTemplateStore(QString filePath) : m_filePath(std::move(filePath)) {}

QString InstanceTemplateStore::dataDir() const
{
    return QFileInfo(m_filePath).absolutePath();
}

QString InstanceTemplateStore::payloadDir(const QString& id) const
{
    return FS::PathCombine(dataDir(), "instance_templates", id);
}

bool InstanceTemplateStore::stageFiles(InstanceTemplate& tpl, QString* error, const CopyProgress& progress)
{
    // Folders carry nothing to copy; they are recreated empty when an instance is built.
    int total = 0;
    for (const auto& file : tpl.files) {
        if (!file.isDir && !file.source.isEmpty())
            total++;
    }

    int done = 0;
    for (auto& file : tpl.files) {
        if (file.isDir || file.source.isEmpty())
            continue;

        const QString destination = FS::PathCombine(payloadDir(tpl.id), file.path);
        if (!FS::ensureFilePathExists(destination)) {
            if (error)
                *error = QCoreApplication::translate("InstanceTemplateStore", "Could not create the folder for the template's files.");
            return false;
        }
        if (!QFile::copy(file.source, destination)) {
            if (error)
                *error = QCoreApplication::translate("InstanceTemplateStore", "Could not copy \"%1\" into the template.").arg(file.source);
            return false;
        }
        // The payload now lives in the template, so the source is only meaningful the first time.
        file.source.clear();

        if (progress && !progress(++done, total)) {
            // A cancellation is not a failure, so no error is set; the caller cleans up and stays quiet.
            return false;
        }
    }
    return true;
}

bool InstanceTemplateStore::load(QString* error)
{
    m_templates.clear();

    QFile file(m_filePath);
    if (!file.exists())
        return true;
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    const QByteArray data = file.readAll();
    file.close();

    if (data.trimmed().isEmpty())
        return true;

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error)
            *error = parseError.errorString();
        return false;
    }
    if (!document.isObject()) {
        if (error)
            *error = QCoreApplication::translate("InstanceTemplateStore", "The templates file is not a JSON object.");
        return false;
    }

    for (const auto& value : document.object().value("templates").toArray()) {
        auto tpl = InstanceTemplate::fromJson(value.toObject());
        // An entry without an identifier cannot be renamed or removed, so it is not worth keeping.
        if (!tpl.id.isEmpty())
            m_templates.append(tpl);
    }
    return true;
}

bool InstanceTemplateStore::save(QString* error) const
{
    QJsonArray templatesArr;
    for (const auto& tpl : m_templates)
        templatesArr.append(tpl.toJson());

    QJsonObject root;
    root.insert("format-version", 3);
    root.insert("templates", templatesArr);

    if (!FS::ensureFilePathExists(m_filePath)) {
        if (error)
            *error = QCoreApplication::translate("InstanceTemplateStore", "Could not create the folder for the templates file.");
        return false;
    }

    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

const InstanceTemplate* InstanceTemplateStore::find(const QString& id) const
{
    for (const auto& tpl : m_templates) {
        if (tpl.id == id)
            return &tpl;
    }
    return nullptr;
}

QList<InstanceTemplate> InstanceTemplateStore::versionsOf(const QString& name) const
{
    QList<InstanceTemplate> out;
    for (const auto& tpl : m_templates) {
        if (tpl.name == name)
            out.append(tpl);
    }
    return out;
}

QString InstanceTemplateStore::add(InstanceTemplate tpl, QString* error, const CopyProgress& progress)
{
    if (tpl.id.isEmpty())
        tpl.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!tpl.createdAt.isValid())
        tpl.createdAt = QDateTime::currentDateTime();

    // If this identifier is already known its old payload has to go first, or files the new version of
    // the template does not have would linger next to the freshly copied ones.
    for (const auto& existing : m_templates) {
        if (existing.id == tpl.id) {
            FS::deletePath(payloadDir(tpl.id));
            break;
        }
    }

    // The payloads are copied in before the template joins the list: one whose files could not be
    // stored should not be offered as though it worked.
    if (!stageFiles(tpl, error, progress)) {
        FS::deletePath(payloadDir(tpl.id));
        return {};
    }

    // The identifier is the template's identity, so a repeated one replaces the old entry instead of
    // producing two templates that cannot be told apart.
    const QString id = tpl.id;
    bool replaced = false;
    for (auto& existing : m_templates) {
        if (existing.id == id) {
            existing = tpl;
            replaced = true;
            break;
        }
    }
    if (!replaced)
        m_templates.append(tpl);

    QString writeError;
    if (!save(&writeError)) {
        qWarning() << "Could not save the templates file:" << writeError;
        if (error)
            *error = writeError;
    }
    return id;
}

bool InstanceTemplateStore::update(const InstanceTemplate& tpl, QString* error)
{
    for (auto& existing : m_templates) {
        if (existing.id == tpl.id) {
            // Editing a template only changes its name and description, so there is nothing new to
            // copy; staging still runs so a template loaded from an older file picks up any payloads
            // the caller happened to fill in.
            InstanceTemplate staged = tpl;
            if (!stageFiles(staged, error, {}))
                return false;
            existing = staged;
            QString writeError;
            if (!save(&writeError)) {
                qWarning() << "Could not save the templates file:" << writeError;
                if (error)
                    *error = writeError;
                return false;
            }
            return true;
        }
    }
    return false;
}

bool InstanceTemplateStore::remove(const QString& id)
{
    for (auto it = m_templates.begin(); it != m_templates.end(); ++it) {
        if (it->id == id) {
            m_templates.erase(it);
            // The folder is not referenced by anything else, so it goes with the template.
            FS::deletePath(payloadDir(id));
            QString error;
            if (!save(&error)) {
                qWarning() << "Could not save the templates file:" << error;
                return false;
            }
            return true;
        }
    }
    return false;
}
