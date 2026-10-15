// SPDX-FileCopyrightText: 2026 Kerilom <89039577+Gleb-Lisovskiy@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "SaveTemplateDialog.h"
#include "ui_SaveTemplateDialog.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QHash>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include "Application.h"
#include "InstanceList.h"
#include "Markdown.h"
#include "minecraft/MinecraftInstance.h"

namespace {

/** The captured files, grouped by the top-level folder they will be laid down in. */
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

/** The short "Minecraft 1.20.1 · Forge" line a version is named after when it has no name of its own. */
QString gameSummary(const InstanceTemplate& tpl)
{
    const QString minecraftVersion = tpl.minecraftVersion();
    QString game = minecraftVersion.isEmpty() ? QCoreApplication::translate("SaveTemplateDialog", "Unknown Minecraft version")
                                              : QCoreApplication::translate("SaveTemplateDialog", "Minecraft %1").arg(minecraftVersion);
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

}  // namespace

SaveTemplateDialog::SaveTemplateDialog(QWidget* parent) : SaveTemplateDialog({}, parent) {}

SaveTemplateDialog::SaveTemplateDialog(const QString& familyName, QWidget* parent)
    : QDialog(parent), ui(new Ui::SaveTemplateDialog), m_familyName(familyName)
{
    ui->setupUi(this);

    if (!m_familyName.isEmpty()) {
        setWindowTitle(tr("Add version to \"%1\"").arg(m_familyName));
        // The version belongs to an existing template, so its name is not this dialog's to change.
        ui->nameEdit->setReadOnly(true);
        ui->nameEdit->setText(m_familyName);
        m_nameTouched = true;
    }

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
    connect(ui->versionEdit, &QLineEdit::textEdited, this, [this] { m_versionTouched = true; });
    connect(ui->descriptionEdit, &QPlainTextEdit::textChanged, this, &SaveTemplateDialog::updatePreview);
    connect(ui->settingsCheck, &QCheckBox::toggled, this, &SaveTemplateDialog::updatePreview);
    connect(ui->filesTree, &QTreeWidget::itemChanged, this, &SaveTemplateDialog::updatePreview);
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &SaveTemplateDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &SaveTemplateDialog::reject);

    // Capturing the instance walks its folders and reads its component list, which for a large pack
    // takes long enough to be worth showing. Doing it here would run before the dialog is even on
    // screen, so it is queued for the first turn of the event loop instead: the window appears at
    // once and the progress window lands on top of it.
    ui->preview->setHtml(QStringLiteral("<p>%1</p>").arg(tr("Reading the instance...")));
    QTimer::singleShot(0, this, &SaveTemplateDialog::onInstanceChanged);
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

namespace {

/** Gathers the entries a checked tree stands for.
 *
 *  Only leaves are taken: a checked folder without children is an empty folder the template has to
 *  remember, while a folder with children is recreated by the files inside it. Leaving a folder out
 *  when it has children also avoids storing the same path twice. */
void collectChecked(QTreeWidgetItem* item, QList<InstanceTemplate::File>& out)
{
    if (!item)
        return;
    if (item->childCount() == 0) {
        if (item->checkState(0) == Qt::Checked && item->data(0, Qt::UserRole).isValid())
            out.append(item->data(0, Qt::UserRole).value<InstanceTemplate::File>());
        return;
    }
    for (int i = 0; i < item->childCount(); i++)
        collectChecked(item->child(i), out);
}

}  // namespace

InstanceTemplate SaveTemplateDialog::fromDialog(bool applyChecks) const
{
    InstanceTemplate tpl = m_everything;
    tpl.name = m_familyName.isEmpty() ? ui->nameEdit->text().trimmed() : m_familyName;
    tpl.versionName = ui->versionEdit->text().trimmed();
    tpl.description = ui->descriptionEdit->toPlainText().trimmed();
    if (applyChecks) {
        if (!ui->settingsCheck->isChecked())
            tpl.settings.clear();

        QList<InstanceTemplate::File> selected;
        for (int i = 0; i < ui->filesTree->topLevelItemCount(); i++)
            collectChecked(ui->filesTree->topLevelItem(i), selected);
        tpl.files = selected;
    }
    return tpl;
}

InstanceTemplate SaveTemplateDialog::templateToSave() const
{
    return fromDialog(true);
}

void SaveTemplateDialog::onInstanceChanged()
{
    auto* instance = selectedInstance();

    // Follow the instance's name until the user types one of their own.
    if (instance && !m_nameTouched)
        ui->nameEdit->setText(instance->name());

    m_hasComponents = false;
    m_everything = InstanceTemplate();
    ui->settingsCheck->setEnabled(false);

    // Adding items fires itemChanged for every check state we set, and each one would rebuild the
    // HTML preview over the whole selection. Holding the signals until the tree is built keeps
    // populating a large instance linear instead of quadratic.
    ui->filesTree->blockSignals(true);
    ui->filesTree->clear();

    if (instance) {
        QProgressDialog progress(tr("Reading the instance..."), tr("Cancel"), 0, 0, this);
        progress.setWindowModality(Qt::WindowModal);
        progress.setMinimumDuration(0);
        progress.setValue(0);
        // Reading the component list happens before the files are walked and can be slow on its own;
        // painting the window first keeps the launcher from looking wedged for that stretch.
        QCoreApplication::processEvents();
        auto captured = InstanceTemplate::captureFrom(*instance, {}, [&progress](int seen) {
            progress.setLabelText(tr("Scanning files... (%1 found)").arg(seen));
            QCoreApplication::processEvents();
            return !progress.wasCanceled();
        });
        if (progress.wasCanceled()) {
            ui->filesTree->blockSignals(false);
            updatePreview();
            updateDialogState();
            return;
        }
        progress.close();
        m_everything = captured;
    }

    m_hasComponents = !m_everything.components.isEmpty();
    ui->settingsCheck->setEnabled(!m_everything.settings.isEmpty());

    // Follow what the instance actually is until the user names the version themselves; the label
    // is free-form, so the summary is only ever a starting point.
    if (!m_versionTouched)
        ui->versionEdit->setText(gameSummary(m_everything));

    buildFilesTree();

    ui->filesTree->blockSignals(false);
    updatePreview();
    updateDialogState();
}

void SaveTemplateDialog::buildFilesTree()
{
    // The tree mirrors the instance's game directory. The capture walks folders before their
    // contents, and lists a folder's own folders before its files, so the tree comes out with
    // folders above files at every level without any sorting of its own.
    QHash<QString, QTreeWidgetItem*> byPath;
    for (const auto& file : m_everything.files) {
        const int slash = file.path.lastIndexOf(QLatin1Char('/'));
        QTreeWidgetItem* parent = nullptr;
        QString name = file.path;
        if (slash >= 0) {
            parent = byPath.value(file.path.left(slash), nullptr);
            if (parent)
                name = file.path.mid(slash + 1);
        }

        auto* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(ui->filesTree);
        item->setText(0, name);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        // A folder ticks its contents along with it; a file has nothing under it.
        if (file.isDir)
            item->setFlags(item->flags() | Qt::ItemIsAutoTristate);
        item->setCheckState(0, Qt::Checked);
        item->setData(0, Qt::UserRole, QVariant::fromValue(file));
        byPath.insert(file.path, item);
    }
}

void SaveTemplateDialog::updatePreview()
{
    auto* instance = selectedInstance();
    if (!instance) {
        ui->preview->setHtml(QStringLiteral("<p>%1</p>").arg(tr("There are no instances to save a template from yet.")));
        return;
    }

    const InstanceTemplate tpl = fromDialog(true);

    QStringList html;
    if (!tpl.versionName.isEmpty())
        html << QStringLiteral("<h3>%1</h3>").arg(tpl.versionName.toHtmlEscaped());
    html << QStringLiteral("<p>%1</p>").arg(gameSummary(tpl).toHtmlEscaped());
    if (!tpl.description.isEmpty())
        html << markdownToHTML(tpl.description);

    if (!tpl.files.isEmpty()) {
        html << QStringLiteral("<p>%1</p>")
                    .arg(tr("These files and folders are copied into the template and laid down in the new instance:"));
        html << filesHtml(tpl.files);
    } else {
        html << QStringLiteral("<p>%1</p>").arg(tr("Nothing will be copied into the template."));
    }

    if (!tpl.settings.isEmpty())
        html << QStringLiteral("<p>%1</p>").arg(tr("The instance settings you changed are remembered."));

    ui->preview->setHtml(html.join(QString()));
}

void SaveTemplateDialog::updateDialogState()
{
    // Without a component list the template cannot create anything, so it is not worth saving.
    const InstanceTemplate tpl = fromDialog(true);
    const bool hasSelectedFiles = !tpl.files.isEmpty();
    const bool ok = selectedInstance() != nullptr && m_hasComponents && !ui->nameEdit->text().trimmed().isEmpty() && hasSelectedFiles;
    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(ok);
}
