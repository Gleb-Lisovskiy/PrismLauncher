// SPDX-FileCopyrightText: 2026 Kerilom <89039577+Gleb-Lisovskiy@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "InstanceTemplate.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QUuid>

#include <utility>

#include "Application.h"
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

QString InstanceTemplate::resourceDirFor(MinecraftInstance& instance, LibraryIndex::Type type)
{
    switch (type) {
        case LibraryIndex::Type::Mod:
            return instance.modsRoot();
        case LibraryIndex::Type::ResourcePack:
            return instance.resourcePacksDir();
        case LibraryIndex::Type::ShaderPack:
            return instance.shaderPacksDir();
        case LibraryIndex::Type::TexturePack:
            return instance.texturePacksDir();
    }
    return {};
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

QStringList InstanceTemplate::installedLibraryEntries(BaseInstance& instance)
{
    QStringList found;

    auto* mc = qobject_cast<MinecraftInstance*>(&instance);
    auto* library = APPLICATION->library();
    if (!mc || !library)
        return found;

    for (const auto& entry : library->entries()) {
        const QString dir = resourceDirFor(*mc, entry.type);
        if (dir.isEmpty())
            continue;

        const QFileInfo installed(FS::PathCombine(dir, QFileInfo(entry.file).fileName()));
        if (!installed.isFile())
            continue;

        // A shared file name is not proof enough that it is the same payload; the size has to agree
        // too, so an unrelated file that happens to share a name is not quietly claimed.
        const QFileInfo source(library->absoluteFilePath(entry));
        if (source.exists() && source.size() != installed.size())
            continue;

        found.append(entry.id);
    }
    return found;
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

InstanceTemplate InstanceTemplate::captureFrom(BaseInstance& instance, const QString& name)
{
    InstanceTemplate tpl;
    tpl.name = name;
    tpl.iconKey = instance.iconKey();
    tpl.createdAt = QDateTime::currentDateTime();
    tpl.components = readComponents(instance);
    tpl.settings = readSettings(instance);
    tpl.libraryEntries = installedLibraryEntries(instance);
    return tpl;
}

QJsonObject InstanceTemplate::toJson() const
{
    QJsonObject obj;
    obj.insert("id", id);
    obj.insert("name", name);
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

    QJsonArray libraryArr;
    for (const auto& entry : libraryEntries)
        libraryArr.append(entry);
    obj.insert("library", libraryArr);

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

    for (const auto& value : json.value("library").toArray()) {
        const QString id = value.toString();
        if (!id.isEmpty())
            tpl.libraryEntries.append(id);
    }

    const auto settingsObj = json.value("settings").toObject();
    for (auto it = settingsObj.constBegin(); it != settingsObj.constEnd(); ++it)
        tpl.settings.insert(it.key(), it.value().toVariant());

    return tpl;
}

InstanceTemplateStore::InstanceTemplateStore(QString filePath) : m_filePath(std::move(filePath)) {}

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
    root.insert("format-version", 1);
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

QString InstanceTemplateStore::add(InstanceTemplate tpl)
{
    if (tpl.id.isEmpty())
        tpl.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!tpl.createdAt.isValid())
        tpl.createdAt = QDateTime::currentDateTime();

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

    QString error;
    if (!save(&error))
        qWarning() << "Could not save the templates file:" << error;
    return id;
}

bool InstanceTemplateStore::update(const InstanceTemplate& tpl)
{
    for (auto& existing : m_templates) {
        if (existing.id == tpl.id) {
            existing = tpl;
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

bool InstanceTemplateStore::remove(const QString& id)
{
    for (auto it = m_templates.begin(); it != m_templates.end(); ++it) {
        if (it->id == id) {
            m_templates.erase(it);
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
