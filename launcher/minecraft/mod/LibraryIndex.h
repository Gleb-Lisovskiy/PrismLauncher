// SPDX-FileCopyrightText: 2026 Kerilom <89039577+Gleb-Lisovskiy@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QDateTime>
#include <QDir>
#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "minecraft/mod/ModDetails.h"
#include "modplatform/ModIndex.h"

/** A user's own collection of mods and resource packs, stored outside of any instance.
 *
 *  Files are kept once in a shared directory and referenced by many instances, so building a
 *  modpack from a set of favourites does not duplicate hundreds of megabytes. Each entry keeps
 *  where it originally came from (provider, project and file ids) so that installing it into an
 *  instance can write the same metadata a direct download would have written, which keeps update
 *  checks working afterwards.
 *
 *  On-disk layout, rooted at the directory handed to the constructor:
 *
 *      library.json                 manifest, the only authoritative list of entries
 *      files/<type>/<filename>     the payload, one copy per entry
 *
 *  The manifest is rewritten whenever entries change. A file present on disk but absent from the
 *  manifest is ignored, and an entry pointing at a missing file is reported as broken rather than
 *  silently dropped, so a hand-edited manifest degrades instead of losing data.
 */
class LibraryIndex {
   public:
    /** What kind of file an entry holds. Maps onto the instance folders it can be installed into. */
    enum class Type { Mod, ResourcePack, ShaderPack, TexturePack };

    struct Entry {
        QString id;
        Type type = Type::Mod;

        /** Path relative to the library root. Never absolute, so the directory stays movable. */
        QString file;

        ModDetails details;

        /** Where the entry was added from. Empty when it came from a local file. */
        ModPlatform::ResourceProvider provider = ModPlatform::ResourceProvider::FLAME;
        QString projectId;
        QString fileId;

        QDateTime addedAt;

        /** True when the payload is missing or unreadable on disk. */
        bool broken = false;
    };

    explicit LibraryIndex(QString rootDir);

    /** Root directory of the library. Created if it does not exist. */
    [[nodiscard]] QString rootDir() const { return m_root; }

    /** Absolute path of an entry's payload, whether or not it currently exists. */
    [[nodiscard]] QString absoluteFilePath(const Entry& entry) const;

    /** Absolute path of the manifest. */
    [[nodiscard]] QString manifestPath() const { return FS::PathCombine(m_root, "library.json"); }

    /** Reads the manifest. Entries whose file is missing are kept and flagged broken.
     *  Returns false and fills error when the manifest exists but could not be read or parsed. */
    bool load(QString* error = nullptr);

    /** Writes the manifest. Returns false and fills error on failure. */
    bool save(QString* error = nullptr) const;

    [[nodiscard]] QList<Entry> entries() const { return m_entries; }
    [[nodiscard]] int size() const { return static_cast<int>(m_entries.size()); }
    [[nodiscard]] bool empty() const { return m_entries.isEmpty(); }

    /** Adds or replaces an entry, copying the payload into the library.
     *
     *  The passed entry is filled in with the id, relative payload path and timestamp that were
     *  assigned, so callers do not have to look them up again. An existing entry with the same id
     *  is replaced. Returns false and fills error without touching the manifest on failure.
     */
    bool add(Entry& entry, const QString& sourceFilePath, QString* error = nullptr);

    /** Removes an entry and, if no other entry references it, its payload. */
    bool remove(const QString& id, QString* error = nullptr);

    /** Replaces the description and metadata of an existing entry, keeping its identity.
     *
     *  Only the descriptive fields are taken from the argument; id, payload path and source are
     *  kept, because those decide which file the entry points at and where it came from - and
     *  changing them here would desynchronise the description from the payload. Returns false for
     *  an unknown id. */
    bool update(const Entry& entry, QString* error = nullptr);

    [[nodiscard]] Entry* find(const QString& id);
    [[nodiscard]] const Entry* find(const QString& id) const;

    /** Stable identifier for an entry, used to keep re-adds from duplicating.
     *
     *  Identity is the loader-defined mod id plus the version, falling back to the file name when
     *  the mod id is unavailable. Including the version lets a library deliberately hold two
     *  versions of the same mod, which modpack work needs, while re-adding the same version still
     *  replaces it instead of piling up. */
    [[nodiscard]] static QString makeId(Type type, const ModDetails& details, const QString& fileName);

    /** Subdirectory of files/ used for a type. */
    [[nodiscard]] static QString typeDirName(Type type);

   private:
    /** Copies the payload into files/<type>/, reusing an identical existing file if one is there. */
    [[nodiscard]] bool importPayload(Type type, const QString& sourceFilePath, QString& relativePath, QString* error);

    void flagMissingFiles();

    [[nodiscard]] static QJsonObject toJson(const Entry& entry);
    [[nodiscard]] static Entry fromJson(const QJsonObject& obj);

    QString m_root;
    QList<Entry> m_entries;
};