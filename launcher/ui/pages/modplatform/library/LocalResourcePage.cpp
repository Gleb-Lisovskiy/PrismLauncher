// SPDX-FileCopyrightText: 2026 Kerilom <89039577+Gleb-Lisovskiy@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "LocalResourcePage.h"

#include "Application.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "modplatform/library/LibraryAPI.h"
#include "ui/dialogs/ResourceDownloadDialog.h"
#include "ui/pages/modplatform/ModModel.h"
#include "ui/pages/modplatform/ResourcePackModel.h"
#include "ui/pages/modplatform/ShaderPackModel.h"
#include "ui/pages/modplatform/TexturePackModel.h"
#include "ui_ResourcePage.h"

using namespace ResourceDownload;

namespace {
/** Minecraft version the dialog was opened for. */
QString gameVersionOf(BaseInstance& instance)
{
    return static_cast<MinecraftInstance&>(instance).getPackProfile()->getComponentVersion("net.minecraft");
}
}  // namespace

/* None of these pages install anything themselves.
 *
 * The file is already on disk, so the only thing that differs from an online provider is where the
 * version's downloadUrl points: at a path rather than a URL. ResourceDownloadTask recognises that
 * and copies instead of downloading, which means the selection travels the ordinary route - it
 * shows up in the review box, runs with the rest of the queue under one progress bar, and cancelling
 * leaves nothing behind.
 *
 * ResourcePage wires none of the signals below up; every provider page connects them in its own
 * constructor.
 */
/* --- Mods --- */

LocalModPage::LocalModPage(ModDownloadDialog* dialog, BaseInstance& instance, LibraryIndex* library)
    : ModPage(dialog, instance), m_library(library)
{
    connect(m_ui->versionSelectionBox, &QComboBox::currentIndexChanged, this, &LocalModPage::onVersionSelectionChanged);
    connect(m_ui->resourceSelectionButton, &QPushButton::clicked, this, &LocalModPage::onResourceSelected);

    m_model =
        new ModModel(instance, new LibraryAPI(library, LibraryIndex::Type::Mod, gameVersionOf(instance)), debugName(), metaEntryBase());
    m_ui->packView->setModel(m_model);

    // Must come after setModel: a view has no selection model until it has one, and connecting to
    // the nullptr that selectionModel() returns there fails silently, leaving the page inert.
    connect(m_ui->packView->selectionModel(), &QItemSelectionModel::currentChanged, this, &LocalModPage::onSelectionChanged);
}

LocalModPage* LocalModPage::create(ModDownloadDialog* dialog, BaseInstance& instance, LibraryIndex* library)
{
    auto page = new LocalModPage(dialog, instance, library);
    auto* model = static_cast<ModModel*>(page->getModel());

    auto filterWidget = page->createFilterWidget();
    page->setFilterWidget(filterWidget);
    model->setFilter(page->getFilter());

    connect(model, &ResourceModel::versionListUpdated, page, &ResourcePage::versionListUpdated);
    connect(model, &ResourceModel::projectInfoUpdated, page, &ResourcePage::updateUi);
    connect(model, &QAbstractListModel::modelReset, page, &ResourcePage::modelReset);
    return page;
}

std::unique_ptr<ModFilterWidget> LocalModPage::createFilterWidget()
{
    // The library has no categories to filter by, so only the version and loader filters make sense.
    return ModFilterWidget::create(&static_cast<MinecraftInstance&>(m_baseInstance), false);
}

/* --- Resource packs --- */

LocalResourcePackPage::LocalResourcePackPage(ResourcePackDownloadDialog* dialog, BaseInstance& instance, LibraryIndex* library)
    : ResourcePackResourcePage(dialog, instance), m_library(library)
{
    connect(m_ui->versionSelectionBox, &QComboBox::currentIndexChanged, this, &LocalResourcePackPage::onVersionSelectionChanged);
    connect(m_ui->resourceSelectionButton, &QPushButton::clicked, this, &LocalResourcePackPage::onResourceSelected);

    m_model = new ResourcePackResourceModel(instance, new LibraryAPI(library, LibraryIndex::Type::ResourcePack, gameVersionOf(instance)),
                                            debugName(), metaEntryBase());
    m_ui->packView->setModel(m_model);

    connect(m_ui->packView->selectionModel(), &QItemSelectionModel::currentChanged, this, &LocalResourcePackPage::onSelectionChanged);
}

LocalResourcePackPage* LocalResourcePackPage::create(ResourcePackDownloadDialog* dialog, BaseInstance& instance, LibraryIndex* library)
{
    auto page = new LocalResourcePackPage(dialog, instance, library);
    auto* model = static_cast<ResourcePackResourceModel*>(page->getModel());

    connect(model, &ResourceModel::versionListUpdated, page, &ResourcePage::versionListUpdated);
    connect(model, &ResourceModel::projectInfoUpdated, page, &ResourcePage::updateUi);
    connect(model, &QAbstractListModel::modelReset, page, &ResourcePage::modelReset);
    return page;
}

/* --- Shader packs --- */

LocalShaderPackPage::LocalShaderPackPage(ShaderPackDownloadDialog* dialog, BaseInstance& instance, LibraryIndex* library)
    : ShaderPackResourcePage(dialog, instance), m_library(library)
{
    connect(m_ui->versionSelectionBox, &QComboBox::currentIndexChanged, this, &LocalShaderPackPage::onVersionSelectionChanged);
    connect(m_ui->resourceSelectionButton, &QPushButton::clicked, this, &LocalShaderPackPage::onResourceSelected);

    m_model = new ShaderPackResourceModel(instance, new LibraryAPI(library, LibraryIndex::Type::ShaderPack, gameVersionOf(instance)),
                                          debugName(), metaEntryBase());
    m_ui->packView->setModel(m_model);

    connect(m_ui->packView->selectionModel(), &QItemSelectionModel::currentChanged, this, &LocalShaderPackPage::onSelectionChanged);
}

LocalShaderPackPage* LocalShaderPackPage::create(ShaderPackDownloadDialog* dialog, BaseInstance& instance, LibraryIndex* library)
{
    auto page = new LocalShaderPackPage(dialog, instance, library);
    auto* model = static_cast<ShaderPackResourceModel*>(page->getModel());

    connect(model, &ResourceModel::versionListUpdated, page, &ResourcePage::versionListUpdated);
    connect(model, &ResourceModel::projectInfoUpdated, page, &ResourcePage::updateUi);
    connect(model, &QAbstractListModel::modelReset, page, &ResourcePage::modelReset);
    return page;
}

/* --- Texture packs --- */

LocalTexturePackPage::LocalTexturePackPage(TexturePackDownloadDialog* dialog, BaseInstance& instance, LibraryIndex* library)
    : TexturePackResourcePage(dialog, instance), m_library(library)
{
    connect(m_ui->versionSelectionBox, &QComboBox::currentIndexChanged, this, &LocalTexturePackPage::onVersionSelectionChanged);
    connect(m_ui->resourceSelectionButton, &QPushButton::clicked, this, &LocalTexturePackPage::onResourceSelected);

    m_model = new TexturePackResourceModel(instance, new LibraryAPI(library, LibraryIndex::Type::TexturePack, gameVersionOf(instance)),
                                           debugName(), metaEntryBase());
    m_ui->packView->setModel(m_model);

    connect(m_ui->packView->selectionModel(), &QItemSelectionModel::currentChanged, this, &LocalTexturePackPage::onSelectionChanged);
}

LocalTexturePackPage* LocalTexturePackPage::create(TexturePackDownloadDialog* dialog, BaseInstance& instance, LibraryIndex* library)
{
    auto page = new LocalTexturePackPage(dialog, instance, library);
    auto* model = static_cast<TexturePackResourceModel*>(page->getModel());

    connect(model, &ResourceModel::versionListUpdated, page, &ResourcePage::versionListUpdated);
    connect(model, &ResourceModel::projectInfoUpdated, page, &ResourcePage::updateUi);
    connect(model, &QAbstractListModel::modelReset, page, &ResourcePage::modelReset);
    return page;
}