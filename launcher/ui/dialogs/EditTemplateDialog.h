// SPDX-FileCopyrightText: 2026 Kerilom <89039577+Gleb-Lisovskiy@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QDialog>

#include "InstanceTemplate.h"

namespace Ui {
class EditTemplateDialog;
}

/** Edits the parts of a template that are not its recipe: the name it is listed under, the version
 *  label the picker shows for it, and the markdown description shown beside it.
 *
 *  Everything the template captured from the instance when it was saved (the components, the
 *  overridden settings, the library files it uses) is left alone. Those describe a snapshot, and
 *  turning a template into a different snapshot is what "Save from an instance..." is for. */
class EditTemplateDialog : public QDialog {
    Q_OBJECT

   public:
    explicit EditTemplateDialog(const InstanceTemplate& tpl, QWidget* parent = nullptr);
    ~EditTemplateDialog() override;

    /** The edited template, ready to be stored under the same identifier. */
    [[nodiscard]] InstanceTemplate templateToSave() const;

   private slots:
    void updateDialogState();

   private:
    Ui::EditTemplateDialog* ui = nullptr;
    InstanceTemplate m_template;
};
