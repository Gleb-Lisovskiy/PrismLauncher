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

LibraryAPI::LibraryAPI(LibraryIndex* library, LibraryIndex::Type type, QString gameVersion)
    : m_library(library), m_type(type), m_gameVersion(std::move(gameVersion))
{}

/** Finds the entry a pack id refers to.
 *
 *  A pack is identified by its project's id where the entry knows one, so that the metadata written
 *  into the instance matches what the platforms use. Entries that came from a file on disk are only
 *  reachable by their own id. */
const LibraryIndex::Entry* LibraryAPI::findEntry(const QString& id) const
{
    if (!m_library) {
        return nullptr;
    }

    if (const auto* entry = m_library->find(id); entry) {
        return entry;
    }

    for (const auto& entry : m_library->entries()) {
        if (entry.type == m_type && entry.projectId == id) {
            return &entry;
        }
    }
    return nullptr;
}

ModPlatform::IndexedPack::Ptr LibraryAPI::toPack(const LibraryIndex::Entry& entry) const
{
    auto pack = std::make_shared<ModPlatform::IndexedPack>();

    // The installed mod records this as its project_id, and that is what an update check compares
    // against. Using the library entry's own id would make a file that came from CurseForge look
    // like a project the server has never heard of, and no update would ever be offered for it.
    pack->addonId = entry.projectId.isEmpty() ? entry.id : entry.projectId;
    // There is no provider to point metadata at; the entry keeps its own source separately, which
    // is what the install path uses.
    pack->provider = ModPlatform::ResourceProvider::FLAME;
    pack->name = entry.details.name.isEmpty() ? QFileInfo(entry.file).fileName() : entry.details.name;
    // The slug names the packwiz index entry. The mod id is what makes it recognisable; the entry
    // id is the fallback for a file that declares no mod id at all.
    pack->slug = entry.details.mod_id.isEmpty() ? entry.id : entry.details.mod_id;
    pack->description = entry.details.description;
    pack->websiteUrl = entry.details.homeurl;
    pack->versionsLoaded = true;
    // A local file has no long description, gallery or donation links to fetch, so there is nothing
    // for getProjectInfo to look up. Saying so keeps the page from asking, and keeps it from
    // swapping the pack out from under the version list while the user is picking from it.
    pack->extraDataLoaded = true;

    auto version = toVersion(entry);
    pack->versions.append(version);
    return pack;
}

ModPlatform::IndexedVersion LibraryAPI::toVersion(const LibraryIndex::Entry& entry) const
{
    ModPlatform::IndexedVersion version;
    version.addonId = entry.projectId.isEmpty() ? entry.id : entry.projectId;
    version.fileId = entry.fileId.isEmpty() ? entry.id : entry.fileId;
    version.version = entry.details.version.isEmpty() ? QFileInfo(entry.file).fileName() : entry.details.version;
    version.version_number = version.version;
    version.fileName = QFileInfo(m_library->absoluteFilePath(entry)).fileName();
    // Not a URL: this is the path of a file that is already on disk.
    version.downloadUrl = m_library->absoluteFilePath(entry);
    version.date = entry.addedAt.toString(Qt::ISODate);
    version.side = ModPlatform::Side::UniversalSide;
    version.is_preferred = true;

    // Both of these are checked by the page's filter, and an unset value in either of them makes it
    // reject the version rather than offer it. A local file is a plain release of whatever the
    // instance runs, so both are filled in from the instance rather than left blank.
    version.version_type = ModPlatform::IndexedVersionType::Release;

    if (!entry.details.mcversion.isEmpty()) {
        // The field is free text a human typed into the manifest, so the parts need trimming before
        // they can be compared against the instance's version.
        version.mcVersion = entry.details.mcversion.split(',', Qt::SkipEmptyParts);
        for (auto& mcVersion : version.mcVersion) {
            mcVersion = mcVersion.trimmed();
        }
        version.mcVersion.removeAll("");
    } else if (!m_gameVersion.isEmpty()) {
        version.mcVersion = QStringList{ m_gameVersion };
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
            packs.append(toPack(entry));
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
        if (const auto* entry = findEntry(args.pack->addonId.toString())) {
            if (entry->type == m_type && !entry->broken) {
                versions.append(toVersion(*entry));
            }
        }
    }

    answerAsync(std::move(callbacks), std::move(versions));
    return nullptr;
}

Task::Ptr LibraryAPI::getProjectInfo(ProjectInfoArgs&& args, Callback<ModPlatform::IndexedPack::Ptr>&& callbacks, bool) const
{
    if (m_library && args.pack) {
        if (const auto* entry = findEntry(args.pack->addonId.toString())) {
            auto pack = toPack(*entry);
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
