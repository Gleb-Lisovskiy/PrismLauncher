// SPDX-FileCopyrightText: 2026 Kerilom <89039577+Gleb-Lisovskiy@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "LibraryDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QTableView>
#include <QVBoxLayout>

#include "Application.h"
#include "DesktopServices.h"
#include "FileSystem.h"
#include "InstanceList.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/mod/ModFolderModel.h"
#include "minecraft/mod/ResourcePackFolderModel.h"
#include "minecraft/mod/ShaderPackFolderModel.h"
#include "minecraft/mod/TexturePackFolderModel.h"
#include "ui_LibraryDialog.h"

namespace {

/** Maps a library type onto the instance folder it belongs in.
 *  Returns nullptr for anything that is not a Minecraft instance. */
ResourceFolderModel* folderFor(MinecraftInstance* instance, LibraryIndex::Type type)
{
    if (!instance) {
        return nullptr;
    }
    switch (type) {
        case LibraryIndex::Type::Mod:
            return instance->loaderModList();
        case LibraryIndex::Type::ResourcePack:
            return instance->resourcePackList();
        case LibraryIndex::Type::ShaderPack:
            return instance->shaderPackList();
        case LibraryIndex::Type::TexturePack:
            return instance->texturePackList();
    }
    return nullptr;
}

QString typeName(LibraryIndex::Type type)
{
    switch (type) {
        case LibraryIndex::Type::Mod:
            return QObject::tr("Mod");
        case LibraryIndex::Type::ResourcePack:
            return QObject::tr("Resource pack");
        case LibraryIndex::Type::ShaderPack:
            return QObject::tr("Shader pack");
        case LibraryIndex::Type::TexturePack:
            return QObject::tr("Texture pack");
    }
    return {};
}

LibraryIndex::Type typeFromName(const QString& name)
{
    if (name == typeName(LibraryIndex::Type::ResourcePack)) {
        return LibraryIndex::Type::ResourcePack;
    }
    if (name == typeName(LibraryIndex::Type::ShaderPack)) {
        return LibraryIndex::Type::ShaderPack;
    }
    if (name == typeName(LibraryIndex::Type::TexturePack)) {
        return LibraryIndex::Type::TexturePack;
    }
    return LibraryIndex::Type::Mod;
}

/** Small form for the fields a library entry carries that the user is allowed to change. */
bool editMetadata(QWidget* parent, LibraryIndex::Entry& entry)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("Edit Description"));

    auto* form = new QFormLayout(&dialog);
    auto* name = new QLineEdit(entry.details.name, &dialog);
    auto* version = new QLineEdit(entry.details.version, &dialog);
    auto* description = new QLineEdit(entry.details.description, &dialog);
    form->addRow(QObject::tr("&Name:"), name);
    form->addRow(QObject::tr("&Version:"), version);
    form->addRow(QObject::tr("&Description:"), description);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    entry.details.name = name->text().trimmed();
    entry.details.version = version->text().trimmed();
    entry.details.description = description->text().trimmed();
    return true;
}

}  // namespace

LibraryDialog::LibraryDialog(QWidget* parent) : QDialog(parent)
{
    ui = new Ui::LibraryDialogForm();
    ui->setupUi(this);

    m_model = new LibraryModel(this);
    m_proxy = new LibraryModel::FilterProxy(this);
    m_proxy->setSourceModel(m_model);
    m_proxy->setSortRole(Qt::DisplayRole);
    ui->libraryView->setModel(m_proxy);
    ui->libraryView->horizontalHeader()->setStretchLastSection(false);
    ui->libraryView->resizeColumnsToContents();
    // The name is the only column worth giving room to; the rest are short identifiers.
    ui->libraryView->horizontalHeader()->setSectionResizeMode(LibraryModel::NameColumn, QHeaderView::Stretch);

    connect(ui->searchEdit, &QLineEdit::textChanged, this, [this](const QString& text) { m_proxy->setFilterRegularExpression(text); });
    connect(ui->libraryView, &QTableView::customContextMenuRequested, this, &LibraryDialog::onShowContextMenu);
    connect(ui->libraryView->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] { updateButtonStates(); });
    connect(ui->addButton, &QPushButton::clicked, this, &LibraryDialog::onAddFiles);
    connect(ui->installButton, &QPushButton::clicked, this, &LibraryDialog::onInstall);
    connect(ui->editButton, &QPushButton::clicked, this, &LibraryDialog::onEdit);
    connect(ui->removeButton, &QPushButton::clicked, this, &LibraryDialog::onRemove);
    connect(ui->openFolderButton, &QPushButton::clicked, this, [this] {
        DesktopServices::openPath(APPLICATION->library()->rootDir(), true);
    });
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::close);

    // Offer every instance so the common case is one click.
    auto* instances = APPLICATION->instances();
    for (int i = 0; i < instances->count(); i++) {
        if (auto* instance = instances->at(i)) {
            ui->instanceCombo->addItem(instance->name(), instance->id());
        }
    }

    reload();
}

void LibraryDialog::setTypeFilter(LibraryIndex::Type type)
{
    m_typeFilter = type;
    m_proxy->setTypeFilter(type);
    reload();
}

void LibraryDialog::setFixedInstance(const QString& instanceId)
{
    selectInstance(instanceId);
    m_fixedInstance = true;
    ui->instanceCombo->setEnabled(false);
    ui->instanceLabel->setEnabled(false);
}

void LibraryDialog::selectInstance(const QString& instanceId)
{
    const int index = ui->instanceCombo->findData(instanceId);
    if (index >= 0) {
        ui->instanceCombo->setCurrentIndex(index);
    }
}

LibraryDialog::~LibraryDialog()
{
    delete ui;
}

void LibraryDialog::reload()
{
    m_model->setEntries(APPLICATION->library()->entries(), APPLICATION->library()->rootDir());
    updateButtonStates();
}

void LibraryDialog::updateButtonStates()
{
    const bool hasSelection = !selectedIds().isEmpty();
    const bool hasInstance = ui->instanceCombo->currentIndex() >= 0;
    // Entries of the wrong type are filtered out of the view, so this only guards against a stale
    // selection surviving a type change.
    const bool installable = !m_typeFilter || ui->libraryView->selectionModel()->hasSelection();
    ui->installButton->setEnabled(hasSelection && hasInstance && installable);
    ui->editButton->setEnabled(hasSelection);
    ui->removeButton->setEnabled(hasSelection);
}

QStringList LibraryDialog::selectedIds() const
{
    QStringList ids;
    const auto selection = ui->libraryView->selectionModel()->selectedRows();
    for (const auto& index : selection) {
        const auto id = index.data(LibraryModel::IdRole).toString();
        if (!id.isEmpty()) {
            ids.append(id);
        }
    }
    return ids;
}

void LibraryDialog::showError(const QString& title, const QString& message)
{
    QMessageBox::warning(this, title, message);
}

void LibraryDialog::setAcceptDropsEnabled(bool enabled)
{
    m_acceptDrops = enabled;
    setAcceptDrops(enabled);
}

void LibraryDialog::dragEnterEvent(QDragEnterEvent* event)
{
    if (m_acceptDrops && event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
        return;
    }
    QDialog::dragEnterEvent(event);
}

void LibraryDialog::dropEvent(QDropEvent* event)
{
    if (!m_acceptDrops || !event->mimeData()->hasUrls()) {
        QDialog::dropEvent(event);
        return;
    }

    QStringList paths;
    for (const auto& url : event->mimeData()->urls()) {
        if (url.isLocalFile() && QFileInfo(url.toLocalFile()).isFile()) {
            paths.append(url.toLocalFile());
        }
    }
    event->acceptProposedAction();

    if (!paths.isEmpty()) {
        askTypeAndAdd(paths);
    }
}

void LibraryDialog::onAddFiles()
{
    // Start in the library's own files directory: the desktop portal on some systems refuses to
    // open at all when handed an empty starting directory, which looks like the button doing
    // nothing at all. This is the same workaround the shortcut code uses.
    const QString startDir = FS::PathCombine(APPLICATION->library()->rootDir(), LibraryIndex::typeDirName(LibraryIndex::Type::Mod));
    const QStringList files = QFileDialog::getOpenFileNames(this,
                                                             tr("Add Files to Local Files"),
                                                             QDir(startDir).exists() ? startDir : QString(),
                                                             tr("Resource files (*.jar *.zip);;All files (*)"));
    if (files.isEmpty()) {
        return;
    }
    askTypeAndAdd(files);
}

void LibraryDialog::askTypeAndAdd(const QStringList& files)
{
    // When the dialog was opened for one resource type, there is nothing to ask: the caller
    // already said what these files are.
    if (m_typeFilter) {
        addFiles(files, *m_typeFilter);
        return;
    }

    const QStringList types{ typeName(LibraryIndex::Type::Mod),
                             typeName(LibraryIndex::Type::ResourcePack),
                             typeName(LibraryIndex::Type::ShaderPack),
                             typeName(LibraryIndex::Type::TexturePack) };
    bool accepted = false;
    const auto chosen =
        QInputDialog::getItem(this, tr("Resource Type"), tr("What kind of file are you adding?"), types, 0, false, &accepted);
    if (!accepted) {
        return;
    }
    addFiles(files, typeFromName(chosen));
}

void LibraryDialog::addFiles(const QStringList& files, LibraryIndex::Type type)
{
    auto* library = APPLICATION->library();
    int added = 0;
    QStringList failures;
    for (const auto& path : files) {
        LibraryIndex::Entry entry;
        entry.type = type;
        QString error;
        if (library->add(entry, path, &error)) {
            added++;
        } else {
            failures.append(QObject::tr("%1: %2").arg(QFileInfo(path).fileName(), error));
        }
    }

    reload();

    if (!failures.isEmpty()) {
        showError(tr("Some Files Were Not Added"), tr("Added %n file(s).\n\n", nullptr, added) + failures.join('\n'));
    }

    // When a fixed target was given the user only came here to install, so do that for them.
    if (m_fixedInstance) {
        onInstall();
    }
}

void LibraryDialog::onEdit()
{
    const auto ids = selectedIds();
    if (ids.isEmpty()) {
        return;
    }

    auto* library = APPLICATION->library();
    auto* entry = library->find(ids.first());
    if (!entry) {
        return;
    }

    auto edited = *entry;
    if (!editMetadata(this, edited)) {
        return;
    }

    QString error;
    if (!library->update(edited, &error)) {
        showError(tr("Could Not Save"), error);
        return;
    }
    reload();
}

void LibraryDialog::onRemove()
{
    const auto ids = selectedIds();
    if (ids.isEmpty()) {
        return;
    }

    auto* library = APPLICATION->library();
    auto* confirm = new QMessageBox(QMessageBox::Warning,
                                    tr("Remove From Library"),
                                    ids.size() == 1 ? tr("Remove '%1' from the library?")
                                                      .arg(library->find(ids.first()) ? library->find(ids.first())->details.name : QString())
                                                    : tr("Remove %n selected entries from the library?", nullptr, ids.size()),
                                    QMessageBox::Yes | QMessageBox::No,
                                    this);
    if (confirm->exec() != QMessageBox::Yes) {
        confirm->deleteLater();
        return;
    }
    confirm->deleteLater();

    QStringList failures;
    for (const auto& id : ids) {
        QString error;
        if (!library->remove(id, &error)) {
            failures.append(error);
        }
    }

    reload();

    if (!failures.isEmpty()) {
        showError(tr("Could Not Remove"), failures.join('\n'));
    }
}

void LibraryDialog::onInstall()
{
    const auto ids = selectedIds();
    const auto instanceId = ui->instanceCombo->currentData().toString();
    if (ids.isEmpty() || instanceId.isEmpty()) {
        return;
    }

    auto* base = APPLICATION->instances()->getInstanceById(instanceId);
    auto* instance = dynamic_cast<MinecraftInstance*>(base);
    if (!instance) {
        showError(tr("Cannot Install"), tr("This instance does not use the standard mod folder layout."));
        return;
    }

    auto* library = APPLICATION->library();
    QStringList installed;
    QStringList failures;

    for (const auto& id : ids) {
        const auto* entry = library->find(id);
        if (!entry) {
            continue;
        }
        if (entry->broken) {
            failures.append(QObject::tr("%1: the file is missing from the library").arg(entry->details.name));
            continue;
        }

        auto* folder = folderFor(instance, entry->type);
        if (!folder) {
            failures.append(QObject::tr("%1: unsupported resource type").arg(entry->details.name));
            continue;
        }

        const QString path = library->absoluteFilePath(*entry);
        bool ok = false;
        if (!entry->projectId.isEmpty()) {
            // Writing the provider metadata is what lets the instance offer updates for this entry
            // later, so prefer the same path a direct download takes.
            ModPlatform::IndexedVersion version;
            version.addonId = entry->projectId;
            version.fileId = entry->fileId;
            version.version = entry->details.version;
            version.fileName = QFileInfo(path).fileName();
            folder->installResourceWithFlameMetadata(path, version);
            ok = true;  // Installed asynchronously by the task; failures surface in the log.
        } else {
            ok = folder->installResource(path);
        }

        if (ok) {
            installed.append(entry->details.name);
        } else {
            failures.append(QObject::tr("%1: could not be copied into the instance").arg(entry->details.name));
        }
    }

    if (!installed.isEmpty()) {
        QMessageBox::information(this, tr("Installed"), tr("Installed into '%1':\n%2").arg(instance->name(), installed.join('\n')));
    }
    if (!failures.isEmpty()) {
        showError(tr("Some Entries Were Not Installed"), failures.join('\n'));
    }
}

void LibraryDialog::onShowContextMenu(const QPoint& pos)
{
    const auto index = ui->libraryView->indexAt(pos);
    if (!index.isValid()) {
        return;
    }

    QMenu menu(this);
    menu.addAction(tr("Install into Instance"), this, &LibraryDialog::onInstall);
    menu.addAction(tr("Edit Description..."), this, &LibraryDialog::onEdit);
    menu.addSeparator();
    menu.addAction(tr("Remove From Library"), this, &LibraryDialog::onRemove);
    menu.exec(ui->libraryView->viewport()->mapToGlobal(pos));
}