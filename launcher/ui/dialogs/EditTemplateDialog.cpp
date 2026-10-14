// SPDX-FileCopyrightText: 2026 Kerilom <89039577+Gleb-Lisovskiy@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "EditTemplateDialog.h"
#include "ui_EditTemplateDialog.h"

#include <QDialogButtonBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>

EditTemplateDialog::EditTemplateDialog(const InstanceTemplate& tpl, QWidget* parent)
    : QDialog(parent), ui(new Ui::EditTemplateDialog), m_template(tpl)
{
    ui->setupUi(this);

    ui->nameEdit->setText(tpl.name);
    ui->descriptionEdit->setPlainText(tpl.description);

    connect(ui->nameEdit, &QLineEdit::textChanged, this, &EditTemplateDialog::updateDialogState);
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &EditTemplateDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &EditTemplateDialog::reject);

    updateDialogState();
}

EditTemplateDialog::~EditTemplateDialog()
{
    delete ui;
}

InstanceTemplate EditTemplateDialog::templateToSave() const
{
    InstanceTemplate tpl = m_template;
    tpl.name = ui->nameEdit->text().trimmed();
    tpl.description = ui->descriptionEdit->toPlainText().trimmed();
    return tpl;
}

void EditTemplateDialog::updateDialogState()
{
    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(!ui->nameEdit->text().trimmed().isEmpty());
}
