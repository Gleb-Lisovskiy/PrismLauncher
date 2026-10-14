// SPDX-FileCopyrightText: 2026 Kerilom <89039577+Gleb-Lisovskiy@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "minecraft/mod/LibraryIndex.h"
#include "ui/pages/modplatform/ModPage.h"
#include "ui/pages/modplatform/ResourcePackPage.h"
#include "ui/pages/modplatform/ShaderPackPage.h"
#include "ui/pages/modplatform/TexturePackPage.h"

namespace ResourceDownload {
// The dialogs live in this namespace too; declared here because including
// ResourceDownloadDialog.h would be circular, as it includes these pages back.
class ModDownloadDialog;
class ResourcePackDownloadDialog;
class ShaderPackDownloadDialog;
class TexturePackDownloadDialog;

/** The local files library, shown as a tab in the ordinary resource dialog.
 *
 *  Looks like the Modrinth and CurseForge tabs and offers the same search, filtering and version
 *  selection, because it is driven by the same page and model classes. The one difference is the
 *  install step: the file is already on disk, so the page copies it instead of queueing a download.
 */
class LocalModPage : public ModPage {
    Q_OBJECT

   public:
    static LocalModPage* create(ModDownloadDialog* dialog, BaseInstance& instance, LibraryIndex* library);

    LocalModPage(ModDownloadDialog* dialog, BaseInstance& instance, LibraryIndex* library);

    inline auto displayName() const -> QString override { return tr("Local Files"); }
    inline auto icon() const -> QIcon override { return QIcon::fromTheme("folder"); }
    inline auto id() const -> QString override { return "local"; }
    inline auto metaEntryBase() const -> QString override { return "local"; }
    inline auto debugName() const -> QString override { return "Local files"; }
    inline auto shouldDisplay() const -> bool override { return true; }

    /** Nothing here is downloaded, so there is nothing to look up in the wiki for. */
    inline auto helpPage() const -> QString override { return {}; }

    std::unique_ptr<ModFilterWidget> createFilterWidget() override;
    void prepareProviderCategories() override {}

   private:
    LibraryIndex* m_library = nullptr;
};

/** Same idea for resource packs and shader packs, which have their own page base classes. */
class LocalResourcePackPage : public ResourcePackResourcePage {
    Q_OBJECT

   public:
    static LocalResourcePackPage* create(ResourcePackDownloadDialog* dialog, BaseInstance& instance, LibraryIndex* library);

    LocalResourcePackPage(ResourcePackDownloadDialog* dialog, BaseInstance& instance, LibraryIndex* library);

    inline auto displayName() const -> QString override { return tr("Local Files"); }
    inline auto icon() const -> QIcon override { return QIcon::fromTheme("folder"); }
    inline auto id() const -> QString override { return "local"; }
    inline auto metaEntryBase() const -> QString override { return "local"; }
    inline auto debugName() const -> QString override { return "Local files"; }
    inline auto shouldDisplay() const -> bool override { return true; }
    inline auto helpPage() const -> QString override { return {}; }

   private:
    LibraryIndex* m_library = nullptr;
};

class LocalShaderPackPage : public ShaderPackResourcePage {
    Q_OBJECT

   public:
    static LocalShaderPackPage* create(ShaderPackDownloadDialog* dialog, BaseInstance& instance, LibraryIndex* library);

    LocalShaderPackPage(ShaderPackDownloadDialog* dialog, BaseInstance& instance, LibraryIndex* library);

    inline auto displayName() const -> QString override { return tr("Local Files"); }
    inline auto icon() const -> QIcon override { return QIcon::fromTheme("folder"); }
    inline auto id() const -> QString override { return "local"; }
    inline auto metaEntryBase() const -> QString override { return "local"; }
    inline auto debugName() const -> QString override { return "Local files"; }
    inline auto shouldDisplay() const -> bool override { return true; }
    inline auto helpPage() const -> QString override { return {}; }

   private:
    LibraryIndex* m_library = nullptr;
};

/** Texture packs get their own tab as well, because the toolchain for them is different enough to
 *  be worth its own folder: a file put into texturepacks/ is a texture pack even though the modern
 *  format is the same as a resource pack's. */
class LocalTexturePackPage : public TexturePackResourcePage {
    Q_OBJECT

   public:
    static LocalTexturePackPage* create(TexturePackDownloadDialog* dialog, BaseInstance& instance, LibraryIndex* library);

    LocalTexturePackPage(TexturePackDownloadDialog* dialog, BaseInstance& instance, LibraryIndex* library);

    inline auto displayName() const -> QString override { return tr("Local Files"); }
    inline auto icon() const -> QIcon override { return QIcon::fromTheme("folder"); }
    inline auto id() const -> QString override { return "local"; }
    inline auto metaEntryBase() const -> QString override { return "local"; }
    inline auto debugName() const -> QString override { return "Local files"; }
    inline auto shouldDisplay() const -> bool override { return true; }
    inline auto helpPage() const -> QString override { return {}; }

   private:
    LibraryIndex* m_library = nullptr;
};
}  // namespace ResourceDownload
