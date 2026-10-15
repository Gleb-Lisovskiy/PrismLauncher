// SPDX-FileCopyrightText: 2026 Kerilom <89039577+Gleb-Lisovskiy@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QDialog>

#include "InstanceTemplate.h"

namespace Ui {
class SaveTemplateDialog;
}

class MinecraftInstance;

/** Saves the current state of an existing instance as a new template.
 *
 *  An instance already set up the way the user likes is the quickest source of a template, but not
 *  everything about it should be copied: the settings and the files are offered separately so a
 *  template can be a bare "this version and loader" recipe when that is all that is wanted. The files
 *  the template would carry are listed in full, because copying them takes space and the user is the
 *  one who decides whether that is worth it. */
class SaveTemplateDialog : public QDialog {
    Q_OBJECT

   public:
    /** Saves the current state of an existing instance as a new template. */
    explicit SaveTemplateDialog(QWidget* parent = nullptr);
    /** Saves it as another version of an existing template.
     *
     *  The name is the existing template's and cannot be changed here: what is being added is a
     *  second build of the same recipe, not a different recipe. */
    SaveTemplateDialog(const QString& familyName, QWidget* parent);
    ~SaveTemplateDialog() override;

    /** The template described by the dialog, ready to be stored. */
    [[nodiscard]] InstanceTemplate templateToSave() const;

   private slots:
    void onInstanceChanged();
    void updatePreview();
    void updateDialogState();

   private:
    [[nodiscard]] MinecraftInstance* selectedInstance() const;

    /** Fills the file tree with everything the captured instance holds, mirroring its game
     *  directory. */
    void buildFilesTree();

    /** The captured instance with the dialog's name, description and checkboxes applied. Without
     *  applyChecks the file list is left whole, which is how the preview lists everything on offer. */
    [[nodiscard]] InstanceTemplate fromDialog(bool applyChecks) const;

    Ui::SaveTemplateDialog* ui = nullptr;
    /** The template this one is a version of; empty when the dialog creates a template of its own. */
    QString m_familyName;
    bool m_nameTouched = false;
    bool m_versionTouched = false;
    bool m_hasComponents = false;
    /** Everything the selected instance holds, captured once per instance so the checkboxes, the
     *  preview and the saved result all look at the same picture. */
    InstanceTemplate m_everything;
};
