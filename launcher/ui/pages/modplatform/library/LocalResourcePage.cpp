// SPDX-FileCopyrightText: 2026 Kerilom <89039577+Gleb-Lisovskiy@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "LocalResourcePage.h"

#include <QFileInfo>
#include <QMessageBox>

#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "minecraft/mod/LibraryIndex.h"
#include "minecraft/mod/ModFolderModel.h"
#include "minecraft/mod/ResourcePackFolderModel.h"
#include "minecraft/mod/ShaderPackFolderModel.h"
#include "minecraft/mod/TexturePackFolderModel.h"
#include "modplatform/library/LibraryAPI.h"
#include "ui/dialogs/ResourceDownloadDialog.h"
#include "ui_ResourcePage.h"

using namespace ResourceDownload;

namespace {
/** Returns the instance folder that holds resources of this type. */
ResourceFolderModel* folderFor(BaseInstance& instance, LibraryIndex::Type type)
{
    auto* minecraft = dynamic_cast<MinecraftInstance*>(&instance);
    if (!minecraft) {
        return nullptr;
    }

    switch (type) {
        case LibraryIndex::Type::Mod:
            return minecraft->loaderModList();
        case LibraryIndex::Type::ResourcePack:
            return minecraft->resourcePackList();
        case LibraryIndex::Type::ShaderPack:
            return minecraft->shaderPackList();
        case LibraryIndex::Type::TexturePack:
            return minecraft->texturePackList();
    }
    return nullptr;
}

/** Checks that the library file is really there before anything tries to copy it. */
QString checkCopyable(ResourceFolderModel* folder, const QString& path)
{
    if (!folder) {
        return QObject::tr("This instance does not use the standard resource folders.");
    }
    if (!QFileInfo::exists(path)) {
        return QObject::tr("The file is missing from the library: %1").arg(path);
    }
    return {};
}

/** Shared implementation of the install step; each page only differs in its own type. */
void doInstall(ResourceDownloadDialog* dialog,
               BaseInstance& instance,
               LibraryIndex* library,
               LibraryIndex::Type type,
               ModPlatform::IndexedPack::Ptr pack,
               ModPlatform::IndexedVersion& version)
{
    QWidget* parent = dialog;
    if (version.is_currently_selected) {
        // Clicking a selected entry again takes it back off the list, as the download flow does.
        version.is_currently_selected = false;
        return;
    }

    auto* folder = folderFor(instance, type);

    // The library entry knows which online project it came from; without that the instance would
    // treat the copied file as unmanaged and offer nothing for it later.
    const auto* entry = library ? library->find(pack->addonId.toString()) : nullptr;

    const auto error = checkCopyable(folder, version.downloadUrl);
    if (!error.isEmpty()) {
        QMessageBox::warning(parent, QObject::tr("Could Not Install"), error);
        return;
    }

    if (entry && !entry->projectId.isEmpty()) {
        ModPlatform::IndexedVersion metadata;
        metadata.addonId = entry->projectId;
        metadata.fileId = entry->fileId;
        metadata.fileName = version.fileName;
        metadata.version = version.version;
        metadata.mcVersion = version.mcVersion;
        metadata.side = version.side;
        folder->installResourceWithFlameMetadata(version.downloadUrl, metadata);
    } else if (!folder->installResource(version.downloadUrl)) {
        QMessageBox::warning(parent, QObject::tr("Could Not Install"),
                             QObject::tr("Could not copy %1 into the instance.").arg(QFileInfo(version.downloadUrl).fileName()));
        return;
    }

    version.is_currently_selected = true;

    // Nothing was queued for download, so the dialog's confirm button has no way of noticing on
    // its own that something got picked.
    dialog->refreshButtonStatus();
}
}  // namespace

/* --- Mods --- */

LocalModPage::LocalModPage(ModDownloadDialog* dialog, BaseInstance& instance, LibraryIndex* library)
    : ModPage(dialog, instance), m_library(library)
{
    // ResourcePage connects none of these; every provider page wires them up in its own
    // constructor. Without the first two the version box is never filled in, m_selectedVersionIndex
    // stays at -1, and the select button stays disabled.
    connect(m_ui->versionSelectionBox, &QComboBox::currentIndexChanged, this, &LocalModPage::onVersionSelectionChanged);
    connect(m_ui->resourceSelectionButton, &QPushButton::clicked, this, &LocalModPage::onResourceSelected);

    m_model = new ModModel(instance,
                           new LibraryAPI(library, LibraryIndex::Type::Mod,
                                          static_cast<MinecraftInstance&>(instance).getPackProfile()->getComponentVersion("net.minecraft")),
                           debugName(), metaEntryBase());
    m_ui->packView->setModel(m_model);

    // Must come after setModel: a view has no selection model until it has one, and
    // connecting to the nullptr that selectionModel() returns there fails silently,
    // leaving the whole page inert.
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

void LocalModPage::addResourceToDialog(ModPlatform::IndexedPack::Ptr pack, ModPlatform::IndexedVersion& version)
{
    doInstall(m_parentDialog, m_baseInstance, m_library, LibraryIndex::Type::Mod, std::move(pack), version);
}

/* --- Resource packs --- */

LocalResourcePackPage::LocalResourcePackPage(ResourcePackDownloadDialog* dialog, BaseInstance& instance, LibraryIndex* library)
    : ResourcePackResourcePage(dialog, instance), m_library(library)
{
    // ResourcePage connects none of these; every provider page wires them up in its own
    // constructor. Without the first two the version box is never filled in, m_selectedVersionIndex
    // stays at -1, and the select button stays disabled.
    connect(m_ui->versionSelectionBox, &QComboBox::currentIndexChanged, this, &LocalResourcePackPage::onVersionSelectionChanged);
    connect(m_ui->resourceSelectionButton, &QPushButton::clicked, this, &LocalResourcePackPage::onResourceSelected);

    m_model = new ResourcePackResourceModel(
        instance,
        new LibraryAPI(library, LibraryIndex::Type::ResourcePack,
                       static_cast<MinecraftInstance&>(instance).getPackProfile()->getComponentVersion("net.minecraft")),
        debugName(), metaEntryBase());
    m_ui->packView->setModel(m_model);

    // Must come after setModel: a view has no selection model until it has one, and
    // connecting to the nullptr that selectionModel() returns there fails silently,
    // leaving the whole page inert.
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

void LocalResourcePackPage::addResourceToDialog(ModPlatform::IndexedPack::Ptr pack, ModPlatform::IndexedVersion& version)
{
    doInstall(m_parentDialog, m_baseInstance, m_library, LibraryIndex::Type::ResourcePack, std::move(pack), version);
}

/* --- Shader packs --- */

LocalShaderPackPage::LocalShaderPackPage(ShaderPackDownloadDialog* dialog, BaseInstance& instance, LibraryIndex* library)
    : ShaderPackResourcePage(dialog, instance), m_library(library)
{
    // ResourcePage connects none of these; every provider page wires them up in its own
    // constructor. Without the first two the version box is never filled in, m_selectedVersionIndex
    // stays at -1, and the select button stays disabled.
    connect(m_ui->versionSelectionBox, &QComboBox::currentIndexChanged, this, &LocalShaderPackPage::onVersionSelectionChanged);
    connect(m_ui->resourceSelectionButton, &QPushButton::clicked, this, &LocalShaderPackPage::onResourceSelected);

    m_model = new ShaderPackResourceModel(
        instance,
        new LibraryAPI(library, LibraryIndex::Type::ShaderPack,
                       static_cast<MinecraftInstance&>(instance).getPackProfile()->getComponentVersion("net.minecraft")),
        debugName(), metaEntryBase());
    m_ui->packView->setModel(m_model);

    // Must come after setModel: a view has no selection model until it has one, and
    // connecting to the nullptr that selectionModel() returns there fails silently,
    // leaving the whole page inert.
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

void LocalShaderPackPage::addResourceToDialog(ModPlatform::IndexedPack::Ptr pack, ModPlatform::IndexedVersion& version)
{
    doInstall(m_parentDialog, m_baseInstance, m_library, LibraryIndex::Type::ShaderPack, std::move(pack), version);
}