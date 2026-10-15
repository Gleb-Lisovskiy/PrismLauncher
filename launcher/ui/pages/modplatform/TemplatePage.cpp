// SPDX-FileCopyrightText: 2026 Kerilom <89039577+Gleb-Lisovskiy@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "TemplatePage.h"
#include "ui_TemplatePage.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QProgressDialog>
#include <QSignalBlocker>

#include "Application.h"
#include "InstanceTemplate.h"
#include "Markdown.h"
#include "icons/IconList.h"
#include "minecraft/TemplateInstanceCreationTask.h"
#include "ui/dialogs/EditTemplateDialog.h"
#include "ui/dialogs/NewInstanceDialog.h"
#include "ui/dialogs/SaveTemplateDialog.h"

namespace {

/** The template's files, grouped by the top-level folder they will be laid down in. */
QString filesHtml(const QList<InstanceTemplate::File>& files)
{
    if (files.isEmpty())
        return {};

    // A path is relative to the instance's game directory. Entries with a slash are grouped under
    // their top-level folder; the rest are top-level files or empty folders.
    QStringList rootEntries;
    QStringList groupOrder;
    QMap<QString, QStringList> groups;

    for (const auto& file : files) {
        const int slash = file.path.indexOf(QLatin1Char('/'));
        const QString suffix = file.isDir ? QStringLiteral("/") : QString();
        if (slash < 0) {
            rootEntries << QStringLiteral("<li>%1%2</li>").arg(file.path.toHtmlEscaped(), suffix);
            continue;
        }
        const QString top = file.path.left(slash);
        if (!groups.contains(top))
            groupOrder << top;
        groups[top] << QStringLiteral("<li>%1%2</li>").arg(file.path.mid(slash + 1).toHtmlEscaped(), suffix);
    }

    QStringList items = rootEntries;
    for (const QString& top : groupOrder)
        items << QStringLiteral("<li><b>%1/</b><ul>%2</ul></li>").arg(top.toHtmlEscaped(), groups.value(top).join(QString()));

    return QStringLiteral("<ul>%1</ul>").arg(items.join(QString()));
}

}  // namespace

TemplatePage::TemplatePage(NewInstanceDialog* dialog, QWidget* parent) : QWidget(parent), ui(new Ui::TemplatePage), dialog(dialog)
{
    ui->setupUi(this);

    connect(ui->templateList, &QListWidget::itemSelectionChanged, this, &TemplatePage::onSelectionChanged);
    connect(ui->versionSelectionBox, qOverload<int>(&QComboBox::currentIndexChanged), this, &TemplatePage::onVersionChanged);
    connect(ui->fromInstanceButton, &QPushButton::clicked, this, &TemplatePage::onSaveFromInstance);
    connect(ui->addVersionButton, &QPushButton::clicked, this, &TemplatePage::onAddVersion);
    connect(ui->editButton, &QPushButton::clicked, this, &TemplatePage::onEdit);
    connect(ui->deleteButton, &QPushButton::clicked, this, &TemplatePage::onDelete);
    connect(ui->refreshButton, &QPushButton::clicked, this, &TemplatePage::onRefresh);

    // Fill in the picker, but do not talk to the dialog yet: this page is not the one being shown,
    // so suggesting a task now would fight with whichever page actually is.
    reload();
}

TemplatePage::~TemplatePage()
{
    delete ui;
}

void TemplatePage::retranslate()
{
    ui->retranslateUi(this);
    // The labels carry translated words, so rebuild them too.
    reload();
}

void TemplatePage::openedImpl()
{
    // A template may have been saved while another page was showing, so take a fresh look.
    reload();
    suggestCurrent();
}

const InstanceTemplate* TemplatePage::currentTemplate() const
{
    // The version box names the version being used, so it is the box rather than the list that says
    // which of the selected template's versions is meant.
    const QString id = ui->versionSelectionBox->currentData().toString();
    if (id.isEmpty())
        return nullptr;

    return APPLICATION->templates()->find(id);
}

QList<InstanceTemplate> TemplatePage::currentVersions() const
{
    if (m_selectedFamily.isEmpty())
        return {};

    return APPLICATION->templates()->versionsOf(m_selectedFamily);
}

void TemplatePage::reload()
{
    const QString keepFamily = m_selectedFamily;
    const QString keepId = m_selectedId;

    ui->templateList->clear();
    ui->versionSelectionBox->clear();

    // A name is what the picker lists, so two versions saved under one name are one entry here.
    QStringList seen;
    for (const auto& tpl : APPLICATION->templates()->templates()) {
        if (seen.contains(tpl.name))
            continue;
        seen << tpl.name;

        auto* item = new QListWidgetItem(tpl.name, ui->templateList);
        item->setData(Qt::UserRole, tpl.name);
        item->setToolTip(tpl.name);

        const QIcon icon = APPLICATION->icons()->getIcon(tpl.iconKey);
        if (!icon.isNull())
            item->setIcon(icon);
    }

    QListWidgetItem* toSelect = nullptr;
    for (int row = 0; row < ui->templateList->count(); row++) {
        auto* item = ui->templateList->item(row);
        if (item->data(Qt::UserRole).toString() == keepFamily) {
            toSelect = item;
            break;
        }
    }
    if (!toSelect && ui->templateList->count() > 0)
        toSelect = ui->templateList->item(0);
    if (toSelect)
        ui->templateList->setCurrentItem(toSelect);

    m_selectedFamily = toSelect ? toSelect->data(Qt::UserRole).toString() : QString();
    m_selectedId = keepId;

    updateVersionBox();
    updatePreview();
    updateButtons();
    // An empty list fires no selection change of its own, so make sure the dialog hears about it.
    suggestCurrent();
}

void TemplatePage::onSelectionChanged()
{
    auto* item = ui->templateList->currentItem();
    m_selectedFamily = item ? item->data(Qt::UserRole).toString() : QString();

    updateVersionBox();
    updatePreview();
    updateButtons();
    // An empty list fires no selection change of its own, so make sure the dialog hears about it.
    suggestCurrent();
}

void TemplatePage::onVersionChanged()
{
    m_selectedId = ui->versionSelectionBox->currentData().toString();

    updatePreview();
    updateButtons();
    suggestCurrent();
}

void TemplatePage::suggestCurrent()
{
    if (!isOpened)
        return;

    const auto* tpl = currentTemplate();
    if (!tpl) {
        dialog->setSuggestedPack();
        return;
    }

    dialog->setSuggestedPack(tpl->name, new TemplateCreationTask(*tpl));
    dialog->setSuggestedIconKey(tpl->iconKey);
}

void TemplatePage::updateVersionBox()
{
    const QSignalBlocker blocker(ui->versionSelectionBox);
    ui->versionSelectionBox->clear();

    const auto versions = currentVersions();
    for (const auto& tpl : versions)
        ui->versionSelectionBox->addItem(versionLabel(tpl), tpl.id);

    // Keep the version that was chosen; fall back to the first one when it is gone.
    int toSelect = ui->versionSelectionBox->findData(m_selectedId);
    if (toSelect < 0)
        toSelect = versions.isEmpty() ? -1 : 0;
    ui->versionSelectionBox->setCurrentIndex(toSelect);
    m_selectedId = ui->versionSelectionBox->currentData().toString();
}

void TemplatePage::updateButtons()
{
    const bool hasSelection = currentTemplate() != nullptr;
    ui->editButton->setEnabled(hasSelection);
    ui->deleteButton->setEnabled(hasSelection);
    ui->versionSelectionBox->setEnabled(hasSelection);
    // A version can only be added to a template that is there to be added to.
    ui->addVersionButton->setEnabled(!m_selectedFamily.isEmpty());
}

QString TemplatePage::gameSummary(const InstanceTemplate& tpl) const
{
    const QString minecraftVersion = tpl.minecraftVersion();
    QString game = minecraftVersion.isEmpty() ? tr("Unknown Minecraft version") : tr("Minecraft %1").arg(minecraftVersion);
    const QString loaderId = tpl.loaderId();
    if (!loaderId.isEmpty()) {
        const QString brand = InstanceTemplate::loaderBrand(loaderId);
        game += QStringLiteral(" · ");
        game += brand.isEmpty() ? loaderId : brand;
        const QString loaderVersion = tpl.loaderVersion();
        if (!loaderVersion.isEmpty())
            game += QLatin1Char(' ') + loaderVersion;
    }
    return game;
}

QString TemplatePage::versionLabel(const InstanceTemplate& tpl) const
{
    // A label is what the user asked the version to be called, so it wins; a version saved before
    // labels existed has none and is named by what it builds instead.
    return tpl.versionName.isEmpty() ? gameSummary(tpl) : tpl.versionName;
}

void TemplatePage::updatePreview()
{
    const auto* tpl = currentTemplate();
    if (!tpl) {
        ui->preview->setHtml(QStringLiteral("<p>%1</p>").arg(tr("Select a template to see what it will create.")));
        return;
    }

    QStringList html;

    html << QStringLiteral("<h3>%1</h3>").arg(tpl->name.toHtmlEscaped());
    if (!tpl->versionName.isEmpty())
        html << QStringLiteral("<p><b>%1</b></p>").arg(tpl->versionName.toHtmlEscaped());
    if (!tpl->description.isEmpty())
        html << markdownToHTML(tpl->description);
    html << QStringLiteral("<p>%1</p>").arg(gameSummary(*tpl).toHtmlEscaped());

    if (tpl->files.isEmpty()) {
        html << QStringLiteral("<p>%1</p>").arg(tr("It does not carry any of the instance's files."));
    } else {
        html << QStringLiteral("<p>%1</p>").arg(tr("It lays down these files and folders:"));
        html << filesHtml(tpl->files);
    }

    if (tpl->settings.isEmpty())
        html << QStringLiteral("<p>%1</p>").arg(tr("It follows your global settings."));
    else
        html << QStringLiteral("<p>%1</p>").arg(tr("It also remembers the instance settings you changed."));

    ui->preview->setHtml(html.join(QString()));
}

void TemplatePage::onSaveFromInstance()
{
    SaveTemplateDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    // The template carries its own copy of the instance's files, and a modpack can hold gigabytes of
    // them, so the copy gets a progress window rather than freezing the launcher.
    const InstanceTemplate tpl = dlg.templateToSave();
    const QString id = storeTemplate(tpl);
    if (id.isEmpty())
        return;

    m_selectedFamily = tpl.name;
    m_selectedId = id;
    reload();
}

void TemplatePage::onAddVersion()
{
    // The version belongs to the selected template, so its name is fixed and only the label is the
    // user's to choose.
    if (m_selectedFamily.isEmpty())
        return;

    SaveTemplateDialog dlg(m_selectedFamily, this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    const QString id = storeTemplate(dlg.templateToSave());
    if (id.isEmpty())
        return;

    m_selectedId = id;
    reload();
}

QString TemplatePage::storeTemplate(InstanceTemplate tpl)
{
    QProgressDialog progress(tr("Saving the template..."), tr("Cancel"), 0, 0, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);

    QString error;
    const QString id = APPLICATION->templates()->add(tpl, &error, [&progress](int done, int total) {
        progress.setMaximum(total);
        progress.setValue(done);
        QCoreApplication::processEvents();
        return !progress.wasCanceled();
    });
    progress.close();

    // A cancellation leaves the error empty, and staying silent is the point of cancelling.
    if (id.isEmpty() && !error.isEmpty())
        QMessageBox::warning(this, tr("Templates"), tr("Could not save the template: %1").arg(error));

    return id;
}

void TemplatePage::onEdit()
{
    const auto* tpl = currentTemplate();
    if (!tpl)
        return;

    EditTemplateDialog dlg(*tpl, this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    InstanceTemplate edited = dlg.templateToSave();
    m_selectedFamily = edited.name;
    m_selectedId = edited.id;
    QString error;
    if (!APPLICATION->templates()->update(edited, &error))
        QMessageBox::warning(this, tr("Templates"), tr("Could not save the template: %1").arg(error));
    reload();
}

void TemplatePage::onDelete()
{
    const auto* tpl = currentTemplate();
    if (!tpl)
        return;

    // Only the version is removed; the other versions of the same template stay where they are.
    const QString label = versionLabel(*tpl);
    const QString id = tpl->id;
    if (QMessageBox::question(this, tr("Delete version"), tr("Delete the version \"%1\"? This cannot be undone.").arg(label)) !=
        QMessageBox::Yes)
        return;

    APPLICATION->templates()->remove(id);
    m_selectedId.clear();
    reload();
}

void TemplatePage::onRefresh()
{
    QString error;
    if (!APPLICATION->templates()->load(&error) && !error.isEmpty())
        QMessageBox::warning(this, tr("Templates"), tr("Could not read the templates file: %1").arg(error));

    reload();
}
