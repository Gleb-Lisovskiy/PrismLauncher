// SPDX-FileCopyrightText: 2026 Kerilom <89039577+Gleb-Lisovskiy@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <functional>

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariant>

class BaseInstance;
class SettingsObject;

/** One saved version of a recipe for a new instance.
 *
 *  When you build modpacks you tend to start from the same place over and over: a particular
 *  Minecraft version, a particular mod loader, maybe the same handful of support mods and the same
 *  memory settings. A template is exactly that starting point, kept under a name and offered in the
 *  "New Instance" dialog next to the built-in platforms.
 *
 *  A name is the identity of the recipe, not of one build of it: saving a second time under the
 *  same name adds a version, and the dialog lets the wanted one be picked. Each version records
 *  four things:
 *
 *    - the game version and mod loader, taken from the instance's component list (so anything the
 *      instance had as a component comes along, not just Minecraft and the loader);
 *    - the instance settings that were overridden away from the global defaults (memory, Java,
 *      window size, ...), and only those, so a new instance still follows the global defaults for
 *      everything the template does not mention;
 *    - the files and folders of the instance's game directory. These are copied into a folder of the
 *      template's own when it is saved, so the template is a self-contained copy: the instance it was
 *      taken from can be changed or deleted afterwards and the template still builds the same thing;
 *    - an optional free-form version label, an icon, and an optional markdown description.
 *
 *  Templates are user data rather than files on disk, so unlike the library they are kept in a
 *  small JSON document (see InstanceTemplateStore) with the copied files in a sibling directory. */
struct InstanceTemplate {
    /** One entry of the instance's component list, e.g. net.minecraft / 1.20.1. */
    struct Component {
        QString uid;
        QString version;
    };

    /** One file or folder a template reproduces.
     *
     *  The payload itself lives inside the template's own folder, under the same relative path, so
     *  the template does not depend on the instance it came from or on the local files library. */
    struct File {
        /** Path relative to the instance's game directory, always with '/' separators. */
        QString path;
        /** Absolute path to copy the payload from while the template is being saved.
         *
         *  Only set on a freshly captured file, so the store knows what to copy; it is cleared once
         *  the payload has been staged and is never written to the templates file. Directories have
         *  no source: they are recreated empty rather than copied. */
        QString source;
        /** Whether this entry is a folder rather than a file. Folders carry no payload, but a
         *  folder that is empty still has to be remembered, or a new instance would not have it. */
        bool isDir = false;
    };

    QString id;
    /** The name the New Instance dialog groups versions by.
     *
     *  Two templates saved under the same name are two versions of one template: the dialog offers
     *  the name in its list and lets the version be picked in the "Version selected" box beside it. */
    QString name;
    /** Free-form label naming this one version, e.g. "1.20.1 forge" or "the fast one". Optional:
     *  when it is empty the version is named after the game version and mod loader it carries. */
    QString versionName;
    /** Free-form markdown shown in the template's preview; optional. */
    QString description;
    QString iconKey = QStringLiteral("default");
    QDateTime createdAt;
    QList<Component> components;
    /** Files and folders that are laid down in a new instance, relative to its game directory. */
    QList<File> files;
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

    /** Called while scanning an instance's content, once every so often, with how many entries have
     *  been seen so far. Returning false aborts the scan. */
    using FileScanProgress = std::function<bool(int seen)>;

    /** Reads an instance's components, overridden settings, and the files and folders of its game
     *  directory.
     *
     *  The files are only listed, not copied: copying happens when the template is stored, which is
     *  also the first point at which it has an identifier to put them under. The caller decides what
     *  to keep; the save dialog lets the user drop the settings or the files before the template is
     *  stored. */
    static InstanceTemplate captureFrom(BaseInstance& instance, const QString& name, const FileScanProgress& progress = {});

    /** The settings a template remembers. */
    static QMap<QString, QVariant> readSettings(BaseInstance& instance);

    /** Scans a game directory: every file and folder under it, with the files pointing at where
     *  they can be copied from.
     *
     *  Walks the whole game directory, descending into every subdirectory the way the launcher's own
     *  resource lists do. Empty folders are listed too, so the save dialog can show everything an
     *  instance holds, and within a directory folders come before files, each in name order.
     *  `gameRoot` is an instance's game directory. */
    static QList<File> contentFiles(const QString& gameRoot, const FileScanProgress& progress = {});

    /** Writes captured settings onto a settings object, skipping the ones it does not know. */
    static void applySettings(SettingsObject* settings, const QMap<QString, QVariant>& values);

    /** A short, untranslated name for a component uid that is a known mod loader, or an empty
     *  string if the uid is not one. */
    static QString loaderBrand(const QString& uid);
};

/** The set of templates the user has saved.
 *
 *  Held by the application and read and written whole. Only the launcher writes this file, so there
 *  is no chance of it drifting from reality the way a hand-edited manifest would; a file that
 *  cannot be parsed is reported and otherwise treated as empty rather than being overwritten.
 *
 *  The payloads a template carries are kept in a directory next to the file, named after the
 *  template's identifier, so removing a template removes its files too. */
class InstanceTemplateStore {
   public:
    /** Called as the template's payloads are copied in, with how many are done out of how many
     *  there are. Returning false stops the save. */
    using CopyProgress = std::function<bool(int done, int total)>;

    explicit InstanceTemplateStore(QString filePath);

    [[nodiscard]] QString filePath() const { return m_filePath; }

    /** The folder the templates file lives in. */
    [[nodiscard]] QString dataDir() const;
    /** The folder a template keeps its own payloads in. */
    [[nodiscard]] QString payloadDir(const QString& id) const;

    /** Reads the file, replacing whatever was in memory. A missing file is not an error. */
    bool load(QString* error = nullptr);

    /** Writes the file. */
    bool save(QString* error = nullptr) const;

    [[nodiscard]] QList<InstanceTemplate> templates() const { return m_templates; }
    [[nodiscard]] bool empty() const { return m_templates.isEmpty(); }
    [[nodiscard]] const InstanceTemplate* find(const QString& id) const;

    /** The templates saved under the given name, oldest first.
     *
     *  A name is what groups a template's versions, so this is the list the New Instance dialog
     *  offers for one entry of its template list. */
    [[nodiscard]] QList<InstanceTemplate> versionsOf(const QString& name) const;

    /** Stores a template, assigning it an identifier when it has none, copying in the payloads it
     *  was captured with. Returns that identifier, or an empty string when the copy was stopped or
     *  failed. */
    QString add(InstanceTemplate tpl, QString* error = nullptr, const CopyProgress& progress = {});

    /** Replaces the template with the same identifier, copying in any payloads it was captured
     *  with. */
    bool update(const InstanceTemplate& tpl, QString* error = nullptr);

    bool remove(const QString& id);

   private:
    /** Copies every file that still carries a source path into the template's folder, clearing the
     *  source afterwards so a later save does not copy it again. */
    bool stageFiles(InstanceTemplate& tpl, QString* error, const CopyProgress& progress);

    QString m_filePath;
    QList<InstanceTemplate> m_templates;
};
