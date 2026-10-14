// SPDX-FileCopyrightText: 2026 Kerilom <89039577+Gleb-Lisovskiy@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QJsonArray>

#include "minecraft/mod/LibraryIndex.h"
#include "modplatform/ResourceAPI.h"
#include "tasks/Task.h"

/** Serves the user's local files as if they were a mod platform.
 *
 *  The interface was written around HTTP: search and version listing go through URLs and the base
 *  implementation fetches them. A provider that already has the data on disk overrides the two
 *  entry points that matter and answers from memory instead, which is why getProjectVersions is
 *  virtual. The URL builders stay unused.
 *
 *  Each library entry becomes one project with a single "version" whose downloadUrl is the path
 *  of the file. The page that uses this provider is responsible for copying that file rather than
 *  downloading it; queueing a download for a local path would fail.
 */
class LibraryAPI : public ResourceAPI {
   public:
    LibraryAPI(LibraryIndex* library, LibraryIndex::Type type, QString gameVersion);

    auto getSortingMethods() const -> QList<SortingMethod> override;
    std::pair<Task::Ptr, QByteArray*> getProjects(QStringList addonIds) const override;

    Task::Ptr searchProjects(SearchArgs&& args, Callback<QList<ModPlatform::IndexedPack::Ptr>>&& callbacks) const override;
    Task::Ptr getProjectVersions(VersionSearchArgs&& args, Callback<QVector<ModPlatform::IndexedVersion>>&& callbacks) const override;

    /** No network, so there is nothing to fetch. */
    std::pair<Task::Ptr, QByteArray*> getProject(QString, bool) const override { return { nullptr, nullptr }; }

    Task::Ptr getProjectInfo(ProjectInfoArgs&& args, Callback<ModPlatform::IndexedPack::Ptr>&& callbacks, bool) const override;
    Task::Ptr getDependencyVersion(DependencySearchArgs&&, Callback<ModPlatform::IndexedVersion>&&) const override { return nullptr; }

    auto getSearchURL(const SearchArgs&) const -> std::optional<QString> override { return std::nullopt; }
    auto getInfoURL(const QString&) const -> std::optional<QString> override { return std::nullopt; }
    auto getVersionsURL(const VersionSearchArgs&) const -> std::optional<QString> override { return std::nullopt; }
    auto getDependencyURL(const DependencySearchArgs&) const -> std::optional<QString> override { return std::nullopt; }

    void loadIndexedPack(ModPlatform::IndexedPack&, QJsonObject&) const override {}
    ModPlatform::IndexedVersion loadIndexedPackVersion(QJsonObject&, ModPlatform::ResourceType) const override { return {}; }
    QJsonArray documentToArray(QJsonDocument&) const override { return {}; }
    void loadExtraPackInfo(ModPlatform::IndexedPack&, QJsonObject&) const override {}

    /** Builds the IndexedPack for a library entry. */
    [[nodiscard]] ModPlatform::IndexedPack::Ptr toPack(const LibraryIndex::Entry& entry) const;

    /** Builds the single IndexedVersion for a library entry.
     *
     *  The downloadUrl holds the local file path, not a URL. */
    [[nodiscard]] ModPlatform::IndexedVersion toVersion(const LibraryIndex::Entry& entry) const;

   private:
    /** Finds the entry a pack id refers to, by entry id or by the project it came from. */
    [[nodiscard]] const LibraryIndex::Entry* findEntry(const QString& id) const;

    LibraryIndex* m_library = nullptr;
    LibraryIndex::Type m_type;

    /** Minecraft version of the instance the dialog was opened for.
     *
     *  A jar on disk does not record which Minecraft it was built for in any field the page filter
     *  can read, and ModModel::checkVersionFilters drops every version whose mcVersion misses the
     *  instance. Without this the tab lists projects and then finds no version for any of them. */
    QString m_gameVersion;
};