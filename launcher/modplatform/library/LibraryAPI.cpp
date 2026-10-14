// SPDX-FileCopyrightText: 2026 Kerilom <89039577+Gleb-Lisovskiy@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "LibraryAPI.h"

#include <QFileInfo>
#include <QTimer>

#include "Application.h"

namespace {
/** Answers a callback on the next event loop turn.
 *
 *  Returning synchronously would make the calling UI run its handlers re-entrantly, inside the
 *  model call, which the rest of the framework does not expect; the network-backed providers
 *  always answer asynchronously for that reason. */
template <typename Callback, typename Result>
void answerAsync(Callback&& callbacks, Result result)
{
    QTimer::singleShot(0, [callbacks = std::move(callbacks), result = std::move(result)]() mutable {
        if (callbacks.on_succeed) {
            callbacks.on_succeed(result);
        }
    });
}
}  // namespace

LibraryAPI::LibraryAPI(LibraryIndex* library, LibraryIndex::Type type) : m_library(library), m_type(type) {}

ModPlatform::IndexedPack::Ptr LibraryAPI::toPack(const LibraryIndex& library, const LibraryIndex::Entry& entry)
{
    auto pack = std::make_shared<ModPlatform::IndexedPack>();
    pack->addonId = entry.id;
    // There is no provider to point metadata at; the entry keeps its own source separately, which
    // is what the install path uses.
    pack->provider = ModPlatform::ResourceProvider::FLAME;
    pack->name = entry.details.name.isEmpty() ? QFileInfo(entry.file).fileName() : entry.details.name;
    pack->slug = entry.details.mod_id;
    pack->description = entry.details.description;
    pack->websiteUrl = entry.details.homeurl;
    pack->versionsLoaded = true;
    // A local file has no long description, gallery or donation links to fetch, so there is nothing
    // for getProjectInfo to look up. Saying so keeps the page from asking, and keeps it from
    // swapping the pack out from under the version list while the user is picking from it.
    pack->extraDataLoaded = true;

    auto version = toVersion(library, entry);
    pack->versions.append(version);
    return pack;
}

ModPlatform::IndexedVersion LibraryAPI::toVersion(const LibraryIndex& library, const LibraryIndex::Entry& entry)
{
    ModPlatform::IndexedVersion version;
    version.addonId = entry.id;
    version.fileId = entry.fileId.isEmpty() ? entry.id : entry.fileId;
    version.version = entry.details.version.isEmpty() ? QFileInfo(entry.file).fileName() : entry.details.version;
    version.version_number = version.version;
    version.fileName = QFileInfo(library.absoluteFilePath(entry)).fileName();
    // Not a URL: this is the path of a file that is already on disk.
    version.downloadUrl = library.absoluteFilePath(entry);
    version.date = entry.addedAt.toString(Qt::ISODate);
    version.side = ModPlatform::Side::UniversalSide;
    version.is_preferred = true;
    if (!entry.details.mcversion.isEmpty()) {
        version.mcVersion = entry.details.mcversion.split(',', Qt::SkipEmptyParts);
    }
    return version;
}

auto LibraryAPI::getSortingMethods() const -> QList<SortingMethod>
{
    return {};
}

std::pair<Task::Ptr, QByteArray*> LibraryAPI::getProjects(QStringList) const
{
    return { nullptr, nullptr };
}

Task::Ptr LibraryAPI::searchProjects(SearchArgs&& args, Callback<QList<ModPlatform::IndexedPack::Ptr>>&& callbacks) const
{
    QList<ModPlatform::IndexedPack::Ptr> packs;

    if (m_library) {
        for (const auto& entry : m_library->entries()) {
            if (entry.type != m_type) {
                continue;
            }
            if (args.search && !entry.details.name.contains(*args.search, Qt::CaseInsensitive) &&
                !entry.details.mod_id.contains(*args.search, Qt::CaseInsensitive)) {
                continue;
            }
            packs.append(toPack(*m_library, entry));
        }
    }

    qInfo() << "Local files search for" << (args.search ? *args.search : QString("(no term)")) << "returned" << packs.size() << "of type"
            << static_cast<int>(m_type) << "entries";
    answerAsync(std::move(callbacks), std::move(packs));
    return nullptr;
}

Task::Ptr LibraryAPI::getProjectVersions(VersionSearchArgs&& args, Callback<QVector<ModPlatform::IndexedVersion>>&& callbacks) const
{
    QVector<ModPlatform::IndexedVersion> versions;

    if (m_library && args.pack) {
        if (const auto* entry = m_library->find(args.pack->addonId.toString())) {
            if (entry->type == m_type && !entry->broken) {
                versions.append(toVersion(*m_library, *entry));
            }
        }
    }

    answerAsync(std::move(callbacks), std::move(versions));
    return nullptr;
}

Task::Ptr LibraryAPI::getProjectInfo(ProjectInfoArgs&& args, Callback<ModPlatform::IndexedPack::Ptr>&& callbacks, bool) const
{
    if (m_library && args.pack) {
        if (const auto* entry = m_library->find(args.pack->addonId.toString())) {
            auto pack = toPack(*m_library, *entry);
            // Answered on the next event loop turn rather than inline: this is called from
            // loadEntry, which runs inside the selection model's change notification, and swapping
            // the pack underneath it there leaves the page's version list pointing at a pack that
            // is no longer the one in the view.
            answerAsync(std::move(callbacks), std::move(pack));
            return nullptr;
        }
    }
    // Falling through without calling the callback would leave the page spinning forever.
    answerAsync(std::move(callbacks), ModPlatform::IndexedPack::Ptr());
    return nullptr;
}
