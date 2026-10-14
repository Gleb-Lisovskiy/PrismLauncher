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
 *  everything about it should be copied: the settings and the library files are offered separately
 *  so a template can be a bare "this version and loader" recipe when that is all that is wanted. */
class SaveTemplateDialog : public QDialog {
    Q_OBJECT

   public:
    explicit SaveTemplateDialog(QWidget* parent = nullptr);
    ~SaveTemplateDialog() override;

    /** The template described by the dialog, ready to be stored. */
    [[nodiscard]] InstanceTemplate templateToSave() const;

   private slots:
    void onInstanceChanged();
    void updatePreview();
    void updateDialogState();

   private:
    [[nodiscard]] MinecraftInstance* selectedInstance() const;

    /** Reads the selected instance. With applyChecks the current checkboxes are honoured, which is
     *  what gets saved and previewed; without them, everything the instance holds is returned, which
     *  is how the checkboxes decide whether they have anything to offer. */
    [[nodiscard]] InstanceTemplate capture(bool applyChecks) const;

    Ui::SaveTemplateDialog* ui = nullptr;
    bool m_nameTouched = false;
};
