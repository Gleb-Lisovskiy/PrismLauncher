// SPDX-FileCopyrightText: 2026 Kerilom <89039577+Gleb-Lisovskiy@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "ModConflictsDialog.h"

#include <QIcon>
#include <QTreeWidgetItem>

#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "minecraft/mod/Mod.h"
#include "minecraft/mod/ModFolderModel.h"
#include "ui_ModConflictsDialog.h"

ModConflictsDialog::ModConflictsDialog(MinecraftInstance* instance, ModFolderModel* mods, QWidget* parent)
    : QDialog(parent), ui(new Ui::ModConflictsDialog), m_instance(instance), m_mods(mods)
{
    ui->setupUi(this);

    connect(ui->rescanButton, &QPushButton::clicked, this, &ModConflictsDialog::rescan);

    rescan();
}

ModConflictsDialog::~ModConflictsDialog()
{
    delete ui;
}

QString ModConflictsDialog::describe(const ModProblem& problem)
{
    switch (problem.kind) {
        case ModProblem::Kind::MissingDependency:
            return tr("\"%1\" needs the mod \"%2\", which is not installed.").arg(problem.modName, problem.other);
        case ModProblem::Kind::DisabledDependency:
            return tr("\"%1\" needs the mod \"%2\", which is switched off.").arg(problem.modName, problem.other);
        case ModProblem::Kind::DuplicateModId:
            return tr("More than one switched-on mod claims the id \"%1\": %2").arg(problem.modId, problem.note);
        case ModProblem::Kind::Incompatible:
            return tr("\"%1\" says it cannot be loaded together with \"%2\".").arg(problem.modName, problem.other);
        case ModProblem::Kind::WrongLoader:
            return tr("\"%1\" is built for %2, while the instance runs %3.").arg(problem.modName, problem.note, problem.other);
        case ModProblem::Kind::WrongGameVersion:
            return tr("\"%1\" is marked for Minecraft %2, not for %3.").arg(problem.modName, problem.note, problem.other);
    }
    return {};
}

void ModConflictsDialog::rescan()
{
    QList<ModRecord> records;
    int enabled = 0;
    for (auto* mod : m_mods->allMods()) {
        const auto record = ModConflicts::recordFrom(*mod);
        enabled += record.enabled ? 1 : 0;
        records.append(record);
    }

    ModScanContext context;
    auto* profile = m_instance->getPackProfile();
    context.gameVersion = profile->getComponentVersion("net.minecraft");
    if (auto loaders = profile->getSupportedModLoaders())
        context.loaders = ModConflicts::loaderNames(*loaders);

    const auto problems = ModConflicts::scan(records, context);

    ui->problemTree->clear();
    for (const auto& problem : problems) {
        const auto text = describe(problem);
        auto* item = new QTreeWidgetItem(ui->problemTree);
        item->setText(0, problem.modName);
        item->setText(1, text);
        item->setIcon(0, QIcon::fromTheme(problem.severity == ModProblem::Severity::Error ? "status-bad" : "status-yellow"));
        item->setToolTip(0, problem.fileName);
        item->setToolTip(1, text);
    }
    ui->problemTree->resizeColumnToContents(0);
    ui->problemTree->setVisible(!problems.isEmpty());

    if (problems.isEmpty()) {
        ui->summaryLabel->setText(tr("Enabled mods: %1. Nothing looks wrong.").arg(enabled));
    } else {
        ui->summaryLabel->setText(
            tr("Problems found: %1. The ones that stop the game from loading are listed first.").arg(problems.size()));
    }

    // Metadata is read in the background, so a scan run too early is looking at half a mods folder.
    if (m_mods->hasPendingParseTasks()) {
        ui->summaryLabel->setText(ui->summaryLabel->text() + ' ' +
                                  tr("The mods are still being read, so there may be more to come in a moment."));
    }
}
