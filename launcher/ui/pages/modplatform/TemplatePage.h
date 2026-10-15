// SPDX-FileCopyrightText: 2026 Kerilom <89039577+Gleb-Lisovskiy@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QWidget>

#include "tasks/Task.h"
#include "ui/pages/BasePage.h"

namespace Ui {
class TemplatePage;
}

struct InstanceTemplate;
class NewInstanceDialog;

/** The "Templates" tab of the New Instance dialog.
 *
 *  Offers the recipes the user saved in a picker and hands the selected one to the dialog as the
 *  task to run, exactly like the platform pages hand over a VanillaCreationTask. Managing the
 *  templates lives here too: saving the current state of an instance as a new template, adding
 *  another version of one already saved, editing it, and deleting one that is no longer wanted.
 *
 *  A template is listed by its name alone; its versions are offered in the box beside the picker,
 *  so the same recipe saved for two Minecraft versions is one entry with two versions, not two
 *  entries to keep in step. */
class TemplatePage : public QWidget, public BasePage {
    Q_OBJECT

   public:
    explicit TemplatePage(NewInstanceDialog* dialog, QWidget* parent = nullptr);
    ~TemplatePage() override;

    QString displayName() const override { return tr("Templates"); }
    QIcon icon() const override { return QIcon::fromTheme("copy"); }
    QString id() const override { return "templates"; }
    QString helpPage() const override { return QString(); }
    bool shouldDisplay() const override { return true; }

    void retranslate() override;
    void openedImpl() override;

   private slots:
    void onSelectionChanged();
    void onVersionChanged();
    void onSaveFromInstance();
    void onAddVersion();
    void onEdit();
    void onDelete();
    void onRefresh();

   private:
    /** Rebuilds the picker from the store, keeping the selection when it still exists. */
    void reload();
    /** Offers the selected template to the dialog; a blank page suggests nothing. */
    void suggestCurrent();
    /** Refills the version box with the versions of the selected template. */
    void updateVersionBox();
    void updatePreview();
    void updateButtons();

    [[nodiscard]] const InstanceTemplate* currentTemplate() const;
    /** The versions saved under the selected template's name, in the order they were saved. */
    [[nodiscard]] QList<InstanceTemplate> currentVersions() const;
    /** Copies a captured template into the store, showing how far the copying has got. Returns the
     *  stored identifier, or an empty string when it was cancelled or failed. */
    QString storeTemplate(InstanceTemplate tpl);

    /** The short "Minecraft 1.20.1 · Forge" line describing what a template will build. */
    [[nodiscard]] QString gameSummary(const InstanceTemplate& tpl) const;
    /** What a version is called in the version box: its own label, or its game and loader when it
     *  has no label of its own. */
    [[nodiscard]] QString versionLabel(const InstanceTemplate& tpl) const;

    Ui::TemplatePage* ui = nullptr;
    NewInstanceDialog* dialog = nullptr;
    /** The name the selected template is listed under, which is what groups its versions. */
    QString m_selectedFamily;
    /** The identifier of the selected version, kept so a reload can put the selection back. */
    QString m_selectedId;
};
