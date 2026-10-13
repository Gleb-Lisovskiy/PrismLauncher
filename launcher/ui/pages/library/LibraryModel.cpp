// SPDX-FileCopyrightText: 2026 Kerilom <89039577+Gleb-Lisovskiy@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "LibraryModel.h"

#include <QFileInfo>

#include "FileSystem.h"

namespace {
QString typeDisplayName(LibraryIndex::Type type)
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

QString sourceDisplayName(const LibraryIndex::Entry& entry)
{
    if (entry.projectId.isEmpty()) {
        return QObject::tr("Local file");
    }
    return ModPlatform::ProviderCapabilities::readableName(entry.provider);
}
}  // namespace

LibraryModel::LibraryModel(QObject* parent) : QAbstractTableModel(parent) {}

void LibraryModel::setEntries(const QList<LibraryIndex::Entry>& entries, QString rootDir)
{
    beginResetModel();
    m_entries = entries;
    m_root = std::move(rootDir);
    endResetModel();
}

LibraryIndex::Entry LibraryModel::entryAt(int row) const
{
    return m_entries.value(row);
}

int LibraryModel::rowForId(const QString& id) const
{
    for (int i = 0; i < m_entries.size(); i++) {
        if (m_entries[i].id == id) {
            return i;
        }
    }
    return -1;
}

int LibraryModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_entries.size());
}

int LibraryModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : NUM_COLUMNS;
}

QVariant LibraryModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size()) {
        return {};
    }

    const auto& entry = m_entries[index.row()];

    if (role == IdRole) {
        return entry.id;
    }
    if (role == EntryRole) {
        return QVariant::fromValue(entry);
    }
    if (role == Qt::ToolTipRole) {
        return entry.details.description.isEmpty() ? entry.details.name : entry.details.description;
    }
    if (role != Qt::DisplayRole) {
        return {};
    }

    switch (index.column()) {
        case NameColumn:
            // A library entry without a name is usually a file whose metadata could not be
            // parsed, so fall back to the file name rather than showing an empty cell.
            return entry.details.name.isEmpty() ? QFileInfo(entry.file).fileName() : entry.details.name;
        case VersionColumn:
            return entry.details.version;
        case TypeColumn:
            return typeDisplayName(entry.type);
        case ModIdColumn:
            return entry.details.mod_id;
        case SizeColumn: {
            if (entry.broken || m_root.isEmpty()) {
                return {};
            }
            return QFileInfo(FS::PathCombine(m_root, entry.file)).size();
        }
        case SourceColumn:
            return sourceDisplayName(entry);
        case StatusColumn:
            return entry.broken ? tr("File is missing") : tr("Ready");
        default:
            return {};
    }
}

QVariant LibraryModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }

    switch (section) {
        case NameColumn:
            return tr("Name");
        case VersionColumn:
            return tr("Version");
        case TypeColumn:
            return tr("Type");
        case ModIdColumn:
            return tr("Mod ID");
        case SizeColumn:
            return tr("Size");
        case SourceColumn:
            return tr("Source");
        case StatusColumn:
            return tr("Status");
        default:
            return {};
    }
}

auto LibraryModel::FilterProxy::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const -> bool
{
    const auto filter = filterRegularExpression().pattern().trimmed();
    if (filter.isEmpty()) {
        return true;
    }

    // Name and mod ID are what a user types to find something, so match across the leading
    // columns rather than only the first one.
    for (int col = 0; col <= ModIdColumn; col++) {
        const auto value = sourceModel()->data(sourceModel()->index(sourceRow, col, sourceParent), Qt::DisplayRole).toString();
        if (value.contains(filter, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}