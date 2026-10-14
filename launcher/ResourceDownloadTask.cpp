// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (c) 2022-2023 flowln <flowlnlnln@gmail.com>
 *  Copyright (C) 2022 Sefa Eyeoglu <contact@scrumplex.net>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "ResourceDownloadTask.h"

#include "minecraft/mod/tasks/LocalResourceCopyTask.h"

#include <utility>

#include "Application.h"

#include "FileSystem.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "minecraft/mod/ResourceFolderModel.h"

#include "minecraft/mod/ShaderPackFolderModel.h"
#include "modplatform/ModIndex.h"
#include "modplatform/helpers/HashUtils.h"
#include "net/ApiDownload.h"
#include "net/ChecksumValidator.h"

namespace {
Net::ModrinthDownloadMeta createModrinthMeta(BaseInstance* instance, QString reason)
{
    auto* mcInstance = dynamic_cast<MinecraftInstance*>(instance);
    if (!mcInstance) {
        return {};
    }

    auto* profile = mcInstance->getPackProfile();
    if (!profile) {
        return {};
    }

    auto loaders = profile->getModLoadersList();

    return {
        .reason = std::move(reason),
        .gameVersion = profile->getComponentVersion("net.minecraft"),
        .loader = !loaders.isEmpty() ? ModPlatform::getModLoaderAsString(loaders.first()) : "",
    };
}
}  // namespace

ResourceDownloadTask::ResourceDownloadTask(ModPlatform::IndexedPack::Ptr pack,
                                           ModPlatform::IndexedVersion version,
                                           ResourceFolderModel* packs,
                                           bool isIndexed,
                                           QString downloadReason)
    : m_pack(std::move(pack)), m_pack_version(std::move(version)), m_pack_model(packs)
{
    // A file already on disk is one the user picked out of their own local files rather than
    // something to be fetched. Its downloadUrl is a path, not a URL, and asking the network for a
    // path fails with ProtocolUnknownError, so it is copied instead.
    const bool fromLocalFiles = QFileInfo::exists(m_pack_version.downloadUrl);

    // A file from the local files only has a project to record when the launcher put it there
    // itself, which is what its file id testifies to. A file dropped into the folder by hand has
    // none, and writing an invented id would make a later update check offer a different project's
    // file. It is installed as an ordinary, unmanaged file instead.
    const bool hasOrigin = !m_pack_version.fileId.toString().isEmpty();

    if (isIndexed && (!fromLocalFiles || hasOrigin)) {
        m_update_task.reset(new LocalResourceUpdateTask(m_pack_model->indexDir(), *m_pack, m_pack_version));
        connect(m_update_task.get(), &LocalResourceUpdateTask::hasOldResource, this, &ResourceDownloadTask::hasOldResource);

        addTask(m_update_task);
    }

    const auto destination = m_pack_model->dir().absoluteFilePath(getFilename());

    if (fromLocalFiles) {
        // The task still goes through the queue, which is what puts it in the review box and makes
        // cancelling actually cancel something.
        m_copyTask = Task::Ptr(new LocalResourceCopyTask(m_pack_version.downloadUrl, destination));
        connect(m_copyTask.get(), &Task::succeeded, this, &ResourceDownloadTask::downloadSucceeded);
        connect(m_copyTask.get(), &Task::failed, this, &ResourceDownloadTask::downloadFailed);
        addTask(m_copyTask);
        return;
    }

    m_filesNetJob.reset(new NetJob(tr("Resource download"), APPLICATION->network()));
    m_filesNetJob->setStatus(tr("Downloading resource:\n%1").arg(m_pack_version.downloadUrl));

    auto action = Net::ApiDownload::makeFile(m_pack_version.downloadUrl, destination, Net::Download::Option::NoOptions,
                                             createModrinthMeta(m_pack_model->instance(), std::move(downloadReason)));
    if (!m_pack_version.hash_type.isEmpty() && !m_pack_version.hash.isEmpty()) {
        switch (Hashing::algorithmFromString(m_pack_version.hash_type)) {
            case Hashing::Algorithm::Md4:
                action->addValidator(new Net::ChecksumValidator(QCryptographicHash::Algorithm::Md4, m_pack_version.hash));
                break;
            case Hashing::Algorithm::Md5:
                action->addValidator(new Net::ChecksumValidator(QCryptographicHash::Algorithm::Md5, m_pack_version.hash));
                break;
            case Hashing::Algorithm::Sha1:
                action->addValidator(new Net::ChecksumValidator(QCryptographicHash::Algorithm::Sha1, m_pack_version.hash));
                break;
            case Hashing::Algorithm::Sha256:
                action->addValidator(new Net::ChecksumValidator(QCryptographicHash::Algorithm::Sha256, m_pack_version.hash));
                break;
            case Hashing::Algorithm::Sha512:
                action->addValidator(new Net::ChecksumValidator(QCryptographicHash::Algorithm::Sha512, m_pack_version.hash));
                break;
            default:
                break;
        }
    }
    m_filesNetJob->addNetAction(action);
    connect(m_filesNetJob.get(), &NetJob::succeeded, this, &ResourceDownloadTask::downloadSucceeded);
    connect(m_filesNetJob.get(), &NetJob::progress, this, &ResourceDownloadTask::downloadProgressChanged);
    connect(m_filesNetJob.get(), &NetJob::stepProgress, this, &ResourceDownloadTask::propagateStepProgress);
    connect(m_filesNetJob.get(), &NetJob::failed, this, &ResourceDownloadTask::downloadFailed);

    addTask(m_filesNetJob);
}

void ResourceDownloadTask::downloadSucceeded()
{
    m_filesNetJob.reset();
    m_copyTask.reset();
    auto oldName = std::get<0>(to_delete);
    auto oldFilename = std::get<1>(to_delete);

    if (oldName.isEmpty() || oldFilename == m_pack_version.fileName) {
        return;
    }

    m_pack_model->uninstallResource(oldFilename, true);

    // also rename the shader config file
    if (dynamic_cast<ShaderPackFolderModel*>(m_pack_model) != nullptr) {
        QFileInfo oldConfig(m_pack_model->dir(), oldFilename + ".txt");
        QFileInfo newConfig(m_pack_model->dir(), getFilename() + ".txt");

        if (oldConfig.exists() && !newConfig.exists()) {
            bool success = FS::move(oldConfig.filePath(), newConfig.filePath());

            if (!success) {
                emit logWarning(tr("Failed to rename shader config from '%1' to '%2'").arg(oldConfig.fileName(), newConfig.fileName()));
            }
        }
    }
}

void ResourceDownloadTask::downloadFailed(QString reason)
{
    m_filesNetJob.reset();
    m_copyTask.reset();
    emitFailed(std::move(reason));
}

void ResourceDownloadTask::downloadProgressChanged(qint64 current, qint64 total)
{
    emit progress(current, total);
}

// This indirection is done so that we don't delete a mod before being sure it was
// downloaded successfully!
void ResourceDownloadTask::hasOldResource(const QString& name, const QString& filename)
{
    to_delete = { name, filename };
}
