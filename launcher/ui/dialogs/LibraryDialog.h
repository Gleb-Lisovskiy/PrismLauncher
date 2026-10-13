// SPDX-FileCopyrightText: 2026 Kerilom <89039577+Gleb-Lisovskiy@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QDialog>
#include <QStringList>

#include "minecraft/mod/LibraryIndex.h"
#include "ui/pages/library/LibraryModel.h"

class QComboBox;
class QLineEdit;
class QPushButton;
class QTableView;
class QDialogButtonBox;

namespace Ui {
class LibraryDialogForm;
}

/** Browses the user's resource library and installs entries into an instance.
 *
 *  Entries are added by pointing at files already on disk, so nothing is downloaded and the
 *  launcher never has to guess whether a file is meant to be a mod or a resource pack: the type is
 *  asked for when the file is added. Installing copies the payload into the target instance's
 *  matching folder and writes the same metadata a direct download would have written, which keeps
 *  update checks working for the entry afterwards.
 */
class LibraryDialog : public QDialog {
    Q_OBJECT

   public:
    LibraryDialog(QWidget* parent = nullptr);
    ~LibraryDialog() override;

    /** Whether files may be dropped onto the dialog to add them. */
    void setAcceptDropsEnabled(bool enabled);

   protected:
    /** Accepts dropped files so they can be added without going through the file dialog, which
     *  is not reachable at all while the desktop portal is misbehaving. */
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

   public:
    /** Selects the given instance id in the target dropdown, if it exists. */
    void selectInstance(const QString& instanceId);

    /** Re-reads the manifest, picking up changes made elsewhere. */
    void reload();

   private:
    void updateButtonStates();
    [[nodiscard]] QStringList selectedIds() const;
    void onAddFiles();
    void onEdit();
    void onRemove();
    void onInstall();
    void addFiles(const QStringList& paths, LibraryIndex::Type type);
    void askTypeAndAdd(const QStringList& paths);
    void onShowContextMenu(const QPoint&);
    void showError(const QString& title, const QString& message);

   public:

    Ui::LibraryDialogForm* ui = nullptr;
    LibraryModel* m_model = nullptr;
    LibraryModel::FilterProxy* m_proxy = nullptr;
    bool m_acceptDrops = false;
};