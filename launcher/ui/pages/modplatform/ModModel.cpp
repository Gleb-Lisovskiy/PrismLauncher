// SPDX-FileCopyrightText: 2023 flowln <flowlnlnln@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "ModModel.h"

#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "minecraft/mod/ModFolderModel.h"
#include "modplatform/ModIndex.h"

#include <QMessageBox>
#include <algorithm>

namespace ResourceDownload {

ModModel::ModModel(BaseInstance& base_inst, ResourceAPI* api, QString debugName, QString metaEntryBase)
    : ResourceModel(api), m_base_instance(base_inst), m_debugName(debugName + " (Model)"), m_metaEntryBase(metaEntryBase)
{}

/******** Make data requests ********/

ResourceAPI::SearchArgs ModModel::createSearchArguments()
{
    auto profile = static_cast<MinecraftInstance const&>(m_base_instance).getPackProfile();

    Q_ASSERT(profile);
    Q_ASSERT(m_filter);

    std::optional<std::vector<Version>> versions{};
    std::optional<QStringList> categories{};
    auto loaders = profile->getSupportedModLoaders();

    // Version filter
    if (!m_filter->versions.empty())
        versions = m_filter->versions;
    if (m_filter->loaders)
        loaders = m_filter->loaders;
    if (!m_filter->categoryIds.empty())
        categories = m_filter->categoryIds;
    auto side = m_filter->side;

    auto sort = getCurrentSortingMethodByIndex();

    return {
        ModPlatform::ResourceType::Mod, m_next_search_offset, m_search_term, sort, loaders, versions, side, categories, m_filter->openSource
    };
}

ResourceAPI::VersionSearchArgs ModModel::createVersionsArguments(const QModelIndex& entry)
{
    return createVersionsArgumentsFor(m_packs[entry.row()], /*filtered=*/true);
}

ResourceAPI::VersionSearchArgs ModModel::createVersionsArgumentsFor(ModPlatform::IndexedPack::Ptr pack, bool filtered) const
{
    auto profile = static_cast<MinecraftInstance const&>(m_base_instance).getPackProfile();

    Q_ASSERT(profile);
    Q_ASSERT(m_filter);

    std::optional<std::vector<Version>> versions{};

    if (!filtered) {
        // Drop every instance-side constraint so the API answers with whatever the project has.
        std::optional<ModPlatform::ModLoaderTypes> noLoaders{};
        return { std::move(pack), versions, noLoaders, ModPlatform::ResourceType::Mod };
    }

    auto loaders = profile->getSupportedModLoaders();
    if (!m_filter->versions.empty())
        versions = m_filter->versions;
    if (m_filter->loaders)
        loaders = m_filter->loaders;

    return { std::move(pack), versions, loaders, ModPlatform::ResourceType::Mod };
}

void ModModel::versionRequestSucceeded(QVector<ModPlatform::IndexedVersion>& doc, QVariant pack, const QModelIndex& index)
{
    auto currentPack = data(index, Qt::UserRole).value<ModPlatform::IndexedPack::Ptr>();

    if (!currentPack || pack != currentPack->addonId) {
        return;
    }

    // The API filters by this instance's Minecraft version and mod loader before answering, so an
    // empty list here does not mean the project has no files at all. It means it ships none for
    // this combination, which is routine for mods that dropped a game version or were only ever
    // published for another loader. Retry unfiltered so the user sees what does exist instead of
    // an empty dropdown.
    if (doc.isEmpty() && !currentPack->unfilteredVersionsLoaded) {
        currentPack->unfilteredVersionsLoaded = true;
        requestUnfilteredVersions(index, std::move(currentPack));
        return;
    }

    ResourceModel::versionRequestSucceeded(doc, pack, index);
}

void ModModel::requestUnfilteredVersions(const QModelIndex& index, ModPlatform::IndexedPack::Ptr pack)
{
    auto args = createVersionsArgumentsFor(pack, /*filtered=*/false);
    auto addonId = pack->addonId;

    ResourceAPI::Callback<QVector<ModPlatform::IndexedVersion>> callbacks{};
    callbacks.on_succeed = [this, index, addonId](auto& doc) {
        if (!s_running_models.constFind(this).value()) {
            return;
        }
        // Deliberately the base implementation: this list is the final answer, and going back
        // through ModModel would trigger the fallback again.
        ResourceModel::versionRequestSucceeded(doc, addonId, index);
    };
    callbacks.on_fail = [](const QString& reason, int) {
        QMessageBox::critical(nullptr, tr("Error"), tr("A network error occurred. Could not load project versions: %1").arg(reason));
    };

    if (auto job = m_api->getProjectVersions(std::move(args), std::move(callbacks)); job) {
        runInfoJob(job);
    }
}

ResourceAPI::ProjectInfoArgs ModModel::createInfoArguments(const QModelIndex& entry)
{
    auto pack = m_packs[entry.row()];
    return { pack };
}

void ModModel::searchWithTerm(const QString& term, unsigned int sort, bool filter_changed)
{
    if (m_search_term == term && m_search_term.isNull() == term.isNull() && m_current_sort_index == sort && !filter_changed) {
        return;
    }

    setSearchTerm(term);
    m_current_sort_index = sort;

    refresh();
}

bool ModModel::isPackInstalled(ModPlatform::IndexedPack::Ptr pack) const
{
    auto allMods = static_cast<MinecraftInstance&>(m_base_instance).loaderModList()->allMods();
    return std::any_of(allMods.cbegin(), allMods.cend(), [pack](Mod* mod) {
        if (auto meta = mod->metadata(); meta)
            return meta->provider == pack->provider && meta->project_id == pack->addonId;
        return false;
    });
}

QVariant ModModel::getInstalledPackVersion(ModPlatform::IndexedPack::Ptr pack) const
{
    auto allMods = static_cast<MinecraftInstance&>(m_base_instance).loaderModList()->allMods();
    for (auto mod : allMods) {
        if (auto meta = mod->metadata(); meta && meta->provider == pack->provider && meta->project_id == pack->addonId) {
            return meta->version();
        }
    }
    return {};
}

bool checkSide(ModPlatform::Side filter, ModPlatform::Side value)
{
    return (filter != ModPlatform::Side::ClientSide && filter != ModPlatform::Side::ServerSide) ||
           (value != ModPlatform::Side::ClientSide && value != ModPlatform::Side::ServerSide) || filter == value;
}

bool ModModel::checkFilters(ModPlatform::IndexedPack::Ptr pack)
{
    if (!m_filter)
        return true;
    return !(m_filter->hideInstalled && isPackInstalled(pack)) && checkSide(m_filter->side, pack->side);
}

bool ModModel::checkVersionFilters(const ModPlatform::IndexedVersion& v)
{
    if (!m_filter)
        return true;
    auto loaders = static_cast<MinecraftInstance&>(m_base_instance).getPackProfile()->getSupportedModLoaders();
    if (m_filter->loaders)
        loaders = m_filter->loaders;
    return (!optedOut(v) &&                                                         // is opted out(aka curseforge download link)
            (!loaders.has_value() || !v.loaders || loaders.value() & v.loaders) &&  // loaders
            checkSide(m_filter->side, v.side) &&                                    // side
            (m_filter->releases.empty() ||                                          // releases
             std::find(m_filter->releases.cbegin(), m_filter->releases.cend(), v.version_type) != m_filter->releases.cend()) &&
            m_filter->checkMcVersions(v.mcVersion));  // mcVersions
}

}  // namespace ResourceDownload
