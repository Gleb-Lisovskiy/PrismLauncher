// SPDX-FileCopyrightText: 2026 Kerilom <89039577+Gleb-Lisovskiy@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QDialog>
#include <QString>

#include "minecraft/mod/ModConflict.h"

class MinecraftInstance;
class ModFolderModel;

namespace Ui {
class ModConflictsDialog;
}

/** Everything the launcher can tell is wrong with one instance's mods, in a list.
 *
 *  A good deal of it is written in the mods themselves: what each one needs, what it refuses to
 *  live beside, which loader and which game versions it was built for. That is enough to catch a
 *  pack that will not start before it is started, which is the point of looking.
 *
 *  The list is a snapshot, so the dialog stays open and can look again: mods can be switched on
 *  and off while it is up. */
class ModConflictsDialog : public QDialog {
    Q_OBJECT

   public:
    ModConflictsDialog(MinecraftInstance* instance, ModFolderModel* mods, QWidget* parent = nullptr);
    ~ModConflictsDialog() override;

   private slots:
    /** Reads the mods again and refills the list. */
    void rescan();

   private:
    /** One problem as a sentence. */
    static QString describe(const ModProblem& problem);

    Ui::ModConflictsDialog* ui = nullptr;
    MinecraftInstance* m_instance = nullptr;
    ModFolderModel* m_mods = nullptr;
};
