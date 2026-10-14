// SPDX-FileCopyrightText: 2026 Kerilom <89039577+Gleb-Lisovskiy@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariant>

#include "minecraft/mod/LibraryIndex.h"

class BaseInstance;
class MinecraftInstance;
class SettingsObject;

/** A saved recipe for a new instance.
 *
 *  When you build modpacks you tend to start from the same place over and over: a particular
 *  Minecraft version, a particular mod loader, maybe the same handful of support mods and the same
 *  memory settings. A template is exactly that starting point, kept under a name and offered in the
 *  "New Instance" dialog next to the built-in platforms.
 *
 *  A template records four things:
 *
 *    - the game version and mod loader, taken from the instance's component list (so anything the
 *      instance had as a component comes along, not just Minecraft and the loader);
 *    - the instance settings that were overridden away from the global defaults (memory, Java,
 *      window size, ...), and only those, so a new instance still follows the global defaults for
 *      everything the template does not mention;
 *    - a list of entries in the local files library (see LibraryIndex) that were installed in the
 *      instance, so the same files are laid down in the new one;
 *    - a name and an icon.
 *
 *  Templates are user data rather than files on disk, so unlike the library they are kept in a
 *  small JSON document (see InstanceTemplateStore). */
struct InstanceTemplate {
    /** One entry of the instance's component list, e.g. net.minecraft / 1.20.1. */
    struct Component {
        QString uid;
        QString version;
    };

    QString id;
    QString name;
    QString iconKey = QStringLiteral("default");
    QDateTime createdAt;
    QList<Component> components;
    /** Identifiers of LibraryIndex entries that belong in a new instance. */
    QStringList libraryEntries;
    /** Instance settings to write, keyed by their setting id. */
    QMap<QString, QVariant> settings;

    [[nodiscard]] bool isValid() const { return !components.isEmpty() && !name.isEmpty(); }

    /** Version of the net.minecraft component, or an empty string. */
    [[nodiscard]] QString minecraftVersion() const;
    /** Component id of the mod loader, or an empty string for a vanilla setup. */
    [[nodiscard]] QString loaderId() const;
    /** Version of the mod loader, or an empty string. */
    [[nodiscard]] QString loaderVersion() const;

    [[nodiscard]] QJsonObject toJson() const;
    static InstanceTemplate fromJson(const QJsonObject& json);

    /** Reads an instance's components, overridden settings, and library files.
     *
     *  The caller decides what to keep: the save dialog lets the user drop the settings or the
     *  library files before the template is stored. */
    static InstanceTemplate captureFrom(BaseInstance& instance, const QString& name);

    /** The settings a template remembers. */
    static QMap<QString, QVariant> readSettings(BaseInstance& instance);

    /** Library entries whose payload is installed in the instance. */
    static QStringList installedLibraryEntries(BaseInstance& instance);

    /** Writes captured settings onto a settings object, skipping the ones it does not know. */
    static void applySettings(SettingsObject* settings, const QMap<QString, QVariant>& values);

    /** The folder inside an instance that resources of a given library type belong in. */
    static QString resourceDirFor(MinecraftInstance& instance, LibraryIndex::Type type);

    /** A short, untranslated name for a component uid that is a known mod loader, or an empty
     *  string if the uid is not one. */
    static QString loaderBrand(const QString& uid);
};

/** The set of templates the user has saved.
 *
 *  Held by the application and read and written whole. Only the launcher writes this file, so there
 *  is no chance of it drifting from reality the way a hand-edited manifest would; a file that
 *  cannot be parsed is reported and otherwise treated as empty rather than being overwritten. */
class InstanceTemplateStore {
   public:
    explicit InstanceTemplateStore(QString filePath);

    [[nodiscard]] QString filePath() const { return m_filePath; }

    /** Reads the file, replacing whatever was in memory. A missing file is not an error. */
    bool load(QString* error = nullptr);

    /** Writes the file. */
    bool save(QString* error = nullptr) const;

    [[nodiscard]] QList<InstanceTemplate> templates() const { return m_templates; }
    [[nodiscard]] bool empty() const { return m_templates.isEmpty(); }
    [[nodiscard]] const InstanceTemplate* find(const QString& id) const;

    /** Stores a template, assigning it an identifier when it has none. Returns that identifier. */
    QString add(InstanceTemplate tpl);

    /** Replaces the template with the same identifier. */
    bool update(const InstanceTemplate& tpl);

    bool remove(const QString& id);

   private:
    QString m_filePath;
    QList<InstanceTemplate> m_templates;
};
