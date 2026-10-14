// SPDX-FileCopyrightText: 2026 Kerilom <89039577+Gleb-Lisovskiy@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QUrl>

#include "minecraft/mod/LibraryIndex.h"
#include "net/Download.h"
#include "tasks/Task.h"

/** Downloads a file into the library, recording where it came from.
 *
 *  The download goes to a temporary file first and is only moved into place once it has completed,
 *  so an aborted or failed transfer cannot leave a half-written payload in the library. The notes
 *  file naming the project is written by LibraryIndex::add, after the payload itself is in place. */
class LibraryDownloadTask : public Task {
    Q_OBJECT

   public:
    LibraryDownloadTask(QUrl url, LibraryIndex::Entry entry, LibraryIndex* library, QObject* parent = nullptr);
    ~LibraryDownloadTask() override;

    void executeTask() override;
    bool canAbort() const override { return m_download && m_download->isRunning(); }

   private:
    void onSucceeded();
    void onFailed();
    void cleanUp();

    QUrl m_url;
    LibraryIndex::Entry m_entry;
    LibraryIndex* m_library = nullptr;
    QString m_tempPath;
    Net::Download::Ptr m_download;
};