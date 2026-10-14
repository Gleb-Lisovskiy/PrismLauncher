// SPDX-FileCopyrightText: 2026 Kerilom <89039577+Gleb-Lisovskiy@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "TemplateInstanceCreationTask.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>

#include <utility>

#include "Application.h"
#include "FileSystem.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "minecraft/mod/LibraryIndex.h"
#include "settings/INISettingsObject.h"
#include "settings/SettingsObject.h"

TemplateCreationTask::TemplateCreationTask(InstanceTemplate tpl) : InstanceCreationTask(), m_template(std::move(tpl)) {}

std::unique_ptr<MinecraftInstance> TemplateCreationTask::createInstance()
{
    setStatus(tr("Creating instance from template \"%1\"").arg(m_template.name));

    auto instance = std::make_unique<MinecraftInstance>(
        m_globalSettings, std::make_unique<INISettingsObject>(FS::PathCombine(m_stagingPath, "instance.cfg")), m_stagingPath);

    SettingsObject::Lock lock(instance->settings());

    auto components = instance->getPackProfile();
    components->buildingFromScratch();
    for (const auto& component : m_template.components) {
        if (component.uid.isEmpty() || component.version.isEmpty())
            continue;
        components->setComponentVersion(component.uid, component.version, component.uid == QStringLiteral("net.minecraft"));
    }

    instance->setName(name());
    instance->setIconKey(m_instIcon);

    InstanceTemplate::applySettings(instance->settings(), m_template.settings);

    copyLibraryFiles(*instance);

    return instance;
}

void TemplateCreationTask::copyLibraryFiles(MinecraftInstance& instance)
{
    if (m_template.libraryEntries.isEmpty())
        return;

    auto* library = APPLICATION->library();
    if (!library)
        return;

    int copied = 0;
    for (const auto& id : m_template.libraryEntries) {
        const auto* entry = library->find(id);
        if (!entry)
            continue;

        const QString targetDir = InstanceTemplate::resourceDirFor(instance, entry->type);
        if (targetDir.isEmpty() || !FS::ensureFolderPathExists(targetDir))
            continue;

        const QString source = library->absoluteFilePath(*entry);
        if (!QFileInfo::exists(source))
            continue;

        const QString destination = FS::PathCombine(targetDir, QFileInfo(entry->file).fileName());
        // Two library entries can describe the same file name; the first one wins.
        if (QFileInfo::exists(destination))
            continue;

        setStatus(tr("Copying template files... (%1/%2)").arg(++copied).arg(m_template.libraryEntries.size()));
        if (!QFile::copy(source, destination))
            qWarning() << "Could not copy a library file into the new instance:" << source << "->" << destination;
    }
}
