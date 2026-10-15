// SPDX-FileCopyrightText: 2026 Kerilom <89039577+Gleb-Lisovskiy@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "TemplateInstanceCreationTask.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <utility>

#include "Application.h"
#include "FileSystem.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "settings/INISettingsObject.h"
#include "settings/SettingsObject.h"

TemplateCreationTask::TemplateCreationTask(InstanceTemplate tpl) : InstanceCreationTask(), m_template(std::move(tpl)) {}

std::unique_ptr<MinecraftInstance> TemplateCreationTask::createInstance()
{
    setStatus(tr("Creating instance from template \"%1\"").arg(m_template.name));

    // A template with no components would build an empty instance whose only failure would show up
    // much later, as a download error. Refuse it here so the user gets told what is wrong.
    if (m_template.minecraftVersion().isEmpty()) {
        setError(tr("The template does not remember a Minecraft version. Save it again from an instance."));
        return nullptr;
    }

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

    copyTemplateFiles(*instance);

    return instance;
}

void TemplateCreationTask::copyTemplateFiles(MinecraftInstance& instance)
{
    if (m_template.files.isEmpty())
        return;

    const QString payloadDir = APPLICATION->templates()->payloadDir(m_template.id);
    const QString targetRoot = instance.gameRoot();

    // Folders first, so an empty one survives even though no file recreates it.
    for (const auto& entry : m_template.files) {
        if (!entry.isDir)
            continue;
        const QString destination = FS::PathCombine(targetRoot, entry.path);
        if (!QDir().mkpath(destination))
            qWarning() << "Could not create a template folder in the new instance:" << destination;
    }

    int total = 0;
    for (const auto& entry : m_template.files) {
        if (!entry.isDir)
            total++;
    }

    int copied = 0;
    for (const auto& file : m_template.files) {
        if (file.isDir)
            continue;

        const QString source = FS::PathCombine(payloadDir, file.path);
        if (!QFileInfo::exists(source))
            continue;

        const QString destination = FS::PathCombine(targetRoot, file.path);
        // The template holds each file once, so two entries cannot collide; a file that is already
        // there is left alone rather than overwritten.
        if (QFileInfo::exists(destination))
            continue;

        setStatus(tr("Copying template files... (%1/%2)").arg(++copied).arg(total));
        if (!FS::ensureFilePathExists(destination) || !QFile::copy(source, destination))
            qWarning() << "Could not copy a template file into the new instance:" << source << "->" << destination;
    }
}
