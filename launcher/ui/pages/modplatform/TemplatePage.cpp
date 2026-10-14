// SPDX-FileCopyrightText: 2026 Kerilom <89039577+Gleb-Lisovskiy@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "TemplatePage.h"
#include "ui_TemplatePage.h"

#include <QFileInfo>
#include <QInputDialog>
#include <QListWidgetItem>
#include <QMessageBox>

#include "Application.h"
#include "InstanceTemplate.h"
#include "icons/IconList.h"
#include "minecraft/TemplateInstanceCreationTask.h"
#include "minecraft/mod/LibraryIndex.h"
#include "ui/dialogs/NewInstanceDialog.h"
#include "ui/dialogs/SaveTemplateDialog.h"

TemplatePage::TemplatePage(NewInstanceDialog* dialog, QWidget* parent) : QWidget(parent), ui(new Ui::TemplatePage), dialog(dialog)
{
    ui->setupUi(this);

    connect(ui->templateList, &QListWidget::itemSelectionChanged, this, &TemplatePage::onSelectionChanged);
    connect(ui->fromInstanceButton, &QPushButton::clicked, this, &TemplatePage::onSaveFromInstance);
    connect(ui->renameButton, &QPushButton::clicked, this, &TemplatePage::onRename);
    connect(ui->deleteButton, &QPushButton::clicked, this, &TemplatePage::onDelete);
    connect(ui->refreshButton, &QPushButton::clicked, this, &TemplatePage::onRefresh);

    // Fill in the list, but do not talk to the dialog yet: this page is not the one being shown, so
    // suggesting a task now would fight with whichever page actually is.
    reload();
}

TemplatePage::~TemplatePage()
{
    delete ui;
}

void TemplatePage::retranslate()
{
    ui->retranslateUi(this);
    updatePreview();
    updateButtons();
}

void TemplatePage::openedImpl()
{
    // A template may have been saved while another page was showing, so take a fresh look.
    reload();
    suggestCurrent();
}

const InstanceTemplate* TemplatePage::currentTemplate() const
{
    auto* item = ui->templateList->currentItem();
    if (!item)
        return nullptr;

    const QString id = item->data(Qt::UserRole).toString();
    if (id.isEmpty())
        return nullptr;

    return APPLICATION->templates()->find(id);
}

void TemplatePage::reload()
{
    const QString keepId = m_selectedId;

    ui->templateList->clear();

    for (const auto& tpl : APPLICATION->templates()->templates()) {
        auto* item = new QListWidgetItem(tpl.name, ui->templateList);
        item->setData(Qt::UserRole, tpl.id);
        item->setToolTip(tpl.name);

        const QIcon icon = APPLICATION->icons()->getIcon(tpl.iconKey);
        if (!icon.isNull())
            item->setIcon(icon);
    }

    QListWidgetItem* toSelect = nullptr;
    for (int row = 0; row < ui->templateList->count(); row++) {
        auto* item = ui->templateList->item(row);
        if (item->data(Qt::UserRole).toString() == keepId) {
            toSelect = item;
            break;
        }
    }
    // When the kept template is gone, the first one is a better default than an empty panel.
    if (!toSelect && ui->templateList->count() > 0)
        toSelect = ui->templateList->item(0);
    if (toSelect)
        ui->templateList->setCurrentItem(toSelect);

    m_selectedId = toSelect ? toSelect->data(Qt::UserRole).toString() : QString();

    updatePreview();
    updateButtons();
    // An empty list fires no selection change of its own, so make sure the dialog hears about it.
    suggestCurrent();
}

void TemplatePage::onSelectionChanged()
{
    auto* item = ui->templateList->currentItem();
    m_selectedId = item ? item->data(Qt::UserRole).toString() : QString();

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

void TemplatePage::updateButtons()
{
    const bool hasSelection = currentTemplate() != nullptr;
    ui->renameButton->setEnabled(hasSelection);
    ui->deleteButton->setEnabled(hasSelection);
}

void TemplatePage::updatePreview()
{
    const auto* tpl = currentTemplate();
    if (!tpl) {
        ui->preview->setHtml(QStringLiteral("<p>%1</p>").arg(tr("Select a template to see what it will create.")));
        return;
    }

    QStringList html;

    const QString minecraftVersion = tpl->minecraftVersion();
    QString game = minecraftVersion.isEmpty() ? tr("Unknown Minecraft version") : tr("Minecraft %1").arg(minecraftVersion.toHtmlEscaped());
    const QString loaderId = tpl->loaderId();
    if (!loaderId.isEmpty()) {
        const QString brand = InstanceTemplate::loaderBrand(loaderId);
        game += QStringLiteral(" &middot; ");
        game += (brand.isEmpty() ? loaderId.toHtmlEscaped() : brand);
        const QString loaderVersion = tpl->loaderVersion();
        if (!loaderVersion.isEmpty())
            game += QLatin1Char(' ') + loaderVersion.toHtmlEscaped();
    }
    html << QStringLiteral("<h3>%1</h3>").arg(tpl->name.toHtmlEscaped());
    html << QStringLiteral("<p>%1</p>").arg(game);

    if (tpl->libraryEntries.isEmpty()) {
        html << QStringLiteral("<p>%1</p>").arg(tr("No files are taken from your local files folder."));
    } else {
        QStringList names;
        auto* library = APPLICATION->library();
        for (const auto& id : tpl->libraryEntries) {
            const auto* entry = library ? library->find(id) : nullptr;
            if (!entry)
                continue;
            const QString label = entry->details.name.isEmpty() ? QFileInfo(entry->file).fileName() : entry->details.name;
            names << QStringLiteral("<li>%1</li>").arg(label.toHtmlEscaped());
        }
        if (names.isEmpty())
            html << QStringLiteral("<p>%1</p>").arg(tr("The files it used are no longer in your local files folder."));
        else
            html << QStringLiteral("<p>%1</p><ul>%2</ul>").arg(tr("Files taken from your local files folder:"), names.join(QString()));
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

    m_selectedId = APPLICATION->templates()->add(dlg.templateToSave());
    reload();
}

void TemplatePage::onRename()
{
    const auto* tpl = currentTemplate();
    if (!tpl)
        return;

    bool ok = false;
    const QString name =
        QInputDialog::getText(this, tr("Rename template"), tr("Template name:"), QLineEdit::Normal, tpl->name, &ok).trimmed();
    if (!ok || name.isEmpty())
        return;

    InstanceTemplate renamed = *tpl;
    renamed.name = name;
    m_selectedId = renamed.id;
    APPLICATION->templates()->update(renamed);
    reload();
}

void TemplatePage::onDelete()
{
    const auto* tpl = currentTemplate();
    if (!tpl)
        return;

    const QString name = tpl->name;
    const QString id = tpl->id;
    if (QMessageBox::question(this, tr("Delete template"), tr("Delete the template \"%1\"? This cannot be undone.").arg(name)) !=
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
