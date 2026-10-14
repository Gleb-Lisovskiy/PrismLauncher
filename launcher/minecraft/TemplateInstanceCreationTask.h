// SPDX-FileCopyrightText: 2026 Kerilom <89039577+Gleb-Lisovskiy@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "InstanceCreationTask.h"
#include "InstanceTemplate.h"

/** Creates a new instance from a saved template.
 *
 *  This is the counterpart of saving one: the recorded components are set up from scratch, the
 *  recorded settings are written to the fresh instance.cfg, and the library entries the template
 *  mentions are copied out of the library and into the matching folders of the new instance. */
class TemplateCreationTask final : public InstanceCreationTask {
    Q_OBJECT
   public:
    explicit TemplateCreationTask(InstanceTemplate tpl);

    std::unique_ptr<MinecraftInstance> createInstance() override;

   private:
    void copyLibraryFiles(MinecraftInstance& instance);

    InstanceTemplate m_template;
};
