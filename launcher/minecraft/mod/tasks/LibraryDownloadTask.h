// SPDX-FileCopyrightText: 2026 Kerilom <89039577+Gleb-Lisovskiy@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QUrl>

#include "minecraft/mod/LibraryIndex.h"
#include "net/Download.h"
#include "tasks/Task.h"

/** Downloads a file into the library and records it in the manifest.
 *
 *  The download goes to a temporary file first and is only moved into place once it has completed,
 *  so an aborted or failed transfer cannot leave a half-written payload that the manifest would
 *  then point at. The manifest is written last: if that step fails the payload is removed again,
 *  because an unreferenced file is easier to spot than an entry with no file.
 */
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