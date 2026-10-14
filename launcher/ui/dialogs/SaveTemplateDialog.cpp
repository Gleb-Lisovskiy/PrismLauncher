// SPDX-FileCopyrightText: 2026 Kerilom <89039577+Gleb-Lisovskiy@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "SaveTemplateDialog.h"
#include "ui_SaveTemplateDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>

#include "Application.h"
#include "InstanceList.h"
#include "Markdown.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/mod/LibraryIndex.h"

SaveTemplateDialog::SaveTemplateDialog(QWidget* parent) : QDialog(parent), ui(new Ui::SaveTemplateDialog)
{
    ui->setupUi(this);

    auto* instances = APPLICATION->instances();
    for (int i = 0; i < instances->count(); i++) {
        auto* instance = qobject_cast<MinecraftInstance*>(instances->at(i));
        if (!instance)
            continue;
        ui->instanceBox->addItem(instance->name(), instance->id());
    }

    connect(ui->instanceBox, &QComboBox::currentIndexChanged, this, &SaveTemplateDialog::onInstanceChanged);
    connect(ui->nameEdit, &QLineEdit::textEdited, this, [this] { m_nameTouched = true; });
    connect(ui->nameEdit, &QLineEdit::textChanged, this, &SaveTemplateDialog::updateDialogState);
    connect(ui->descriptionEdit, &QPlainTextEdit::textChanged, this, &SaveTemplateDialog::updatePreview);
    connect(ui->settingsCheck, &QCheckBox::toggled, this, &SaveTemplateDialog::updatePreview);
    connect(ui->libraryCheck, &QCheckBox::toggled, this, &SaveTemplateDialog::updatePreview);
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &SaveTemplateDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &SaveTemplateDialog::reject);

    onInstanceChanged();
}

SaveTemplateDialog::~SaveTemplateDialog()
{
    delete ui;
}

MinecraftInstance* SaveTemplateDialog::selectedInstance() const
{
    const QString id = ui->instanceBox->currentData().toString();
    if (id.isEmpty())
        return nullptr;

    return qobject_cast<MinecraftInstance*>(APPLICATION->instances()->getInstanceById(id));
}

InstanceTemplate SaveTemplateDialog::capture(bool applyChecks) const
{
    auto* instance = selectedInstance();
    if (!instance)
        return {};

    InstanceTemplate tpl = InstanceTemplate::captureFrom(*instance, ui->nameEdit->text().trimmed());
    tpl.description = ui->descriptionEdit->toPlainText().trimmed();
    if (applyChecks) {
        if (!ui->settingsCheck->isChecked())
            tpl.settings.clear();
        if (!ui->libraryCheck->isChecked())
            tpl.libraryEntries.clear();
    }
    return tpl;
}

InstanceTemplate SaveTemplateDialog::templateToSave() const
{
    return capture(true);
}

void SaveTemplateDialog::onInstanceChanged()
{
    auto* instance = selectedInstance();

    // Follow the instance's name until the user types one of their own.
    if (instance && !m_nameTouched)
        ui->nameEdit->setText(instance->name());

    updatePreview();
    updateDialogState();
}

void SaveTemplateDialog::updatePreview()
{
    auto* instance = selectedInstance();
    if (!instance) {
        m_hasComponents = false;
        ui->preview->setHtml(QStringLiteral("<p>%1</p>").arg(tr("There are no instances to save a template from yet.")));
        return;
    }

    // Decide what there is to offer from the full picture, before the checkboxes take anything away.
    const InstanceTemplate everything = capture(false);
    m_hasComponents = !everything.components.isEmpty();
    ui->settingsCheck->setEnabled(!everything.settings.isEmpty());
    ui->libraryCheck->setEnabled(!everything.libraryEntries.isEmpty());

    const InstanceTemplate tpl = capture(true);

    QString game =
        tpl.minecraftVersion().isEmpty() ? tr("Unknown Minecraft version") : tr("Minecraft %1").arg(tpl.minecraftVersion().toHtmlEscaped());
    const QString loaderId = tpl.loaderId();
    if (!loaderId.isEmpty()) {
        const QString brand = InstanceTemplate::loaderBrand(loaderId);
        game += QStringLiteral(" &middot; ");
        game += (brand.isEmpty() ? loaderId.toHtmlEscaped() : brand);
        const QString loaderVersion = tpl.loaderVersion();
        if (!loaderVersion.isEmpty())
            game += QLatin1Char(' ') + loaderVersion.toHtmlEscaped();
    }

    QStringList html;
    html << QStringLiteral("<p>%1</p>").arg(game);
    if (!tpl.description.isEmpty())
        html << markdownToHTML(tpl.description);

    QStringList names;
    auto* library = APPLICATION->library();
    for (const auto& id : tpl.libraryEntries) {
        const auto* entry = library ? library->find(id) : nullptr;
        if (!entry)
            continue;
        const QString label = entry->details.name.isEmpty() ? QFileInfo(entry->file).fileName() : entry->details.name;
        names << QStringLiteral("<li>%1</li>").arg(label.toHtmlEscaped());
    }
    if (!names.isEmpty())
        html << QStringLiteral("<p>%1</p><ul>%2</ul>").arg(tr("Files taken from your local files folder:"), names.join(QString()));

    if (!tpl.settings.isEmpty())
        html << QStringLiteral("<p>%1</p>").arg(tr("The instance settings you changed are remembered."));

    ui->preview->setHtml(html.join(QString()));
}

void SaveTemplateDialog::updateDialogState()
{
    // Without a component list the template cannot create anything, so it is not worth saving.
    const bool ok = selectedInstance() != nullptr && m_hasComponents && !ui->nameEdit->text().trimmed().isEmpty();
    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(ok);
}
