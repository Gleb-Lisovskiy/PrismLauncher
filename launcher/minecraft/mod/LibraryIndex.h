// SPDX-FileCopyrightText: 2026 Kerilom <89039577+Gleb-Lisovskiy@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <optional>

#include <QDateTime>
#include <QDir>
#include <QHash>
#include <QString>
#include <QStringList>

#include "minecraft/mod/ModDetails.h"
#include "modplatform/ModIndex.h"

/** A user's own collection of mods and resource packs, stored outside of any instance.
 *
 *  Files are kept once in a shared directory and referenced by many instances, so building a
 *  modpack from a set of favourites does not duplicate hundreds of megabytes.
 *
 *  There is no manifest to keep in step: the directory is the list. Anything in it is picked up by
 *  the next scan, and what a file is gets decided by where it was put, or failing that by what it
 *  contains:
 *
 *      mods/            a mod
 *      resourcepacks/   a resource pack
 *      shaderpacks/     a shader pack
 *      texturepacks/    a texture pack
 *      anywhere else    identified by opening it, the same way a file dropped onto an instance is
 *
 *  So the directories are a convenience rather than a requirement: a pile of archives in the root
 *  of the library sorts itself into the right tab. A file put into one of the four directories is
 *  taken at its word, because that placement was deliberate.
 *
 *  The description of a resource lives in a markdown file next to it, named after the payload with
 *  an ".md" suffix - sodium.jar has its notes in sodium.jar.md. Its body is what the resource
 *  dialog shows, rendered as markdown. Anything before a leading "---" line is front matter saying
 *  what the launcher cannot work out on its own; only the fields that keep an installed copy
 *  updateable are written there:
 *
 *      ---
 *      provider: flame
 *      project-id: 306612
 *      file-id: 4567890
 *      ---
 *      # Sodium
 *      ...
 *
 *  Every payload that was dropped into the directory by hand has no such file, which is perfectly
 *  normal: it simply has no description, and nothing is invented for it.
 */
class LibraryIndex {
   public:
    /** What kind of file an entry holds. Maps onto the instance folders it can be installed into. */
    enum class Type { Mod, ResourcePack, ShaderPack, TexturePack };

    /** Where a payload came from, when the launcher fetched it itself.
     *
     *  A file the user copied in has no origin, and none is guessed for it. Writing a made-up
     *  project id into an instance's metadata would make a later update check offer some other
     *  project's file, which is worse than offering no update at all. */
    struct Origin {
        ModPlatform::ResourceProvider provider = ModPlatform::ResourceProvider::FLAME;
        QString projectId;
        QString fileId;

        [[nodiscard]] bool isValid() const { return !projectId.isEmpty(); }
    };

    struct Entry {
        QString id;
        Type type = Type::Mod;

        /** Path relative to the library root. Never absolute, so the directory stays movable. */
        QString file;

        /** What the payload says about itself. The name falls back to the file name. */
        ModDetails details;

        /** Body of the notes file, in markdown. Empty when there is no notes file. */
        QString notes;

        Origin origin;

        /** Last modification time of the payload. */
        QDateTime addedAt;
    };

    explicit LibraryIndex(QString rootDir);

    /** Turns the LocalFilesDir setting into an absolute path.
     *
     *  A bare name is relative to the launcher's data folder, which is where the default "localfiles"
     *  lives. An absolute path is used as it is, which is what browsing for a folder produces;
     *  combining the two, as a naive join would, silently nests the chosen folder inside the data
     *  folder and makes the setting look like it did nothing. An empty value falls back to the
     *  default rather than scanning the data folder itself. */
    static QString resolveRootDir(const QString& dataRoot, const QString& configured);

    /** Root directory of the library. */
    [[nodiscard]] QString rootDir() const { return m_root; }

    /** Absolute path of an entry's payload. */
    [[nodiscard]] QString absoluteFilePath(const Entry& entry) const;

    /** Path of the notes file belonging to a payload: the payload's path plus ".md". */
    [[nodiscard]] static QString notesPathFor(const QString& payloadPath);

    /** Rebuilds the list from what is on disk.
     *
     *  Runs at start-up and again whenever the user is about to pick resources, so a file dropped
     *  into the folder shows up without restarting the launcher. Payloads that have not changed
     *  since the last scan are not opened again, which is what keeps that affordable. A library
     *  directory that does not exist is not an error; it just means an empty library. */
    bool rescan(QString* error = nullptr);

    [[nodiscard]] QList<Entry> entries() const { return m_entries; }
    [[nodiscard]] int size() const { return static_cast<int>(m_entries.size()); }
    [[nodiscard]] bool empty() const { return m_entries.isEmpty(); }

    [[nodiscard]] Entry* find(const QString& id);
    [[nodiscard]] const Entry* find(const QString& id) const;

    /** Copies a file into the library, with notes recording where it came from.
     *
     *  Used when the launcher downloads a file for the library. Files the user places in the
     *  directory themselves are found by rescan() instead and do not come through here. The entry
     *  is filled in with the id and payload path that were assigned; a payload of the same name is
     *  replaced, but notes the user has written are kept. */
    bool add(Entry& entry, const QString& sourceFilePath, QString* error = nullptr);

    /** Directory of the library a type is filed under. */
    [[nodiscard]] static QString typeDirName(Type type);

    /** Stable identifier for an entry.
     *
     *  Identity is the loader-defined mod id plus the version, falling back to the file name when
     *  the mod id is unavailable. Including the version lets a library deliberately hold two
     *  versions of one mod, which modpack work needs, while re-adding the same version still
     *  replaces rather than piles up. */
    [[nodiscard]] static QString makeId(Type type, const ModDetails& details, const QString& fileName);

   private:
    /** What opening a payload once told us, so a rescan does not open it again.
     *
     *  Keyed by absolute path and kept only for files that were seen in the last scan, so it cannot
     *  outgrow the library. */
    struct CachedPayload {
        qint64 size = 0;
        QDateTime modified;
        std::optional<Type> sniffedType;
        std::optional<ModDetails> details;
    };

    /** Adds every payload under a directory, descending into subdirectories.
     *
     *  insideType is set while walking one of the four type directories, where the location decides
     *  the type; anywhere else the payload is identified by its contents. */
    void scanDirectory(const QDir& dir, std::optional<Type> insideType);

    /** Reads a payload's metadata and notes, and appends an entry for it. */
    void addScannedFile(const QFileInfo& file, Type type, const QString& relative, CachedPayload& facts);

    /** Writes the notes file for an entry, keeping any description it already had. */
    [[nodiscard]] bool writeNotes(const Entry& entry, QString* error);

    QString m_root;
    QList<Entry> m_entries;
    QHash<QString, CachedPayload> m_cache;
};
