// SPDX-FileCopyrightText: 2026 Kerilom <89039577+Gleb-Lisovskiy@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QAbstractTableModel>
#include <QSortFilterProxyModel>

#include "minecraft/mod/LibraryIndex.h"

/** Table model over a LibraryIndex.
 *
 *  The model keeps its own copy of the entries rather than pointing into the index, so that a
 *  save that fails or a concurrent reload cannot leave the view holding dangling references. Rows
 *  are identified by the entry id, not by position, so sorting and filtering stay correct when the
 *  underlying list is replaced.
 */
class LibraryModel : public QAbstractTableModel {
    Q_OBJECT

   public:
    enum Column { NameColumn, VersionColumn, TypeColumn, ModIdColumn, SizeColumn, SourceColumn, StatusColumn, NUM_COLUMNS };
    enum Roles { IdRole = Qt::UserRole + 1, EntryRole };

    explicit LibraryModel(QObject* parent = nullptr);

    /** Replaces the contents with a snapshot of the given index.
     *
     *  The root is needed to resolve payloads for the size column; entries store only a path
     *  relative to it so that the library directory stays movable. */
    void setEntries(const QList<LibraryIndex::Entry>& entries, QString rootDir);

    [[nodiscard]] const QList<LibraryIndex::Entry>& entries() const { return m_entries; }

    [[nodiscard]] LibraryIndex::Entry entryAt(int row) const;
    /** Row of an entry id, or -1. */
    [[nodiscard]] int rowForId(const QString& id) const;

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    auto data(const QModelIndex& index, int role = Qt::DisplayRole) const -> QVariant override;
    auto headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const -> QVariant override;

    /** Filters on name, mod id and description, case-insensitively. */
    class FilterProxy : public QSortFilterProxyModel {
       public:
        explicit FilterProxy(QObject* parent = nullptr) : QSortFilterProxyModel(parent) { setDynamicSortFilter(true); }

       protected:
        auto filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const -> bool override;
    };

   private:
    QList<LibraryIndex::Entry> m_entries;
    QString m_root;
};

Q_DECLARE_METATYPE(LibraryIndex::Entry)