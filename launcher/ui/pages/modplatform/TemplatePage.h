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
 *  Lists the recipes the user saved and hands the selected one to the dialog as the task to run,
 *  exactly like the platform pages hand over a VanillaCreationTask. Managing the list lives here
 *  too: saving the current state of an instance as a new template, renaming one, and deleting one
 *  that is no longer wanted. */
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
    void onSaveFromInstance();
    void onRename();
    void onDelete();
    void onRefresh();

   private:
    /** Rebuilds the list from the store, keeping the selection when it still exists. */
    void reload();
    /** Offers the selected template to the dialog; a blank page suggests nothing. */
    void suggestCurrent();
    void updatePreview();
    void updateButtons();

    [[nodiscard]] const InstanceTemplate* currentTemplate() const;

    Ui::TemplatePage* ui = nullptr;
    NewInstanceDialog* dialog = nullptr;
    QString m_selectedId;
};
