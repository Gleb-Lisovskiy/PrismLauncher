// SPDX-FileCopyrightText: 2026 Kerilom <89039577+Gleb-Lisovskiy@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QObject>
#include <QString>

#include <atomic>

#include "tasks/Task.h"

/** Copies a resource that is already on disk into an instance folder.
 *
 *  Stands in for the download in ResourceDownloadTask when a file comes from the user's own library
 *  rather than from a platform. Queuing a network request for a local path would fail with
 *  ProtocolUnknownError, and installing at selection time instead would bypass the review step
 *  entirely - leaving nothing to cancel and nothing to see in the list of what is being installed.
 */
class LocalResourceCopyTask : public Task {
    Q_OBJECT
   public:
    LocalResourceCopyTask(const QString& sourcePath, const QString& destinationPath, QObject* parent = nullptr);

    void executeTask() override;
    bool abort() override;

   private:
    /** Set from abort(), which runs on the caller's thread while the copy runs on this one. */
    std::atomic_bool m_aborted = false;

    QString m_source;
    QString m_destination;
};