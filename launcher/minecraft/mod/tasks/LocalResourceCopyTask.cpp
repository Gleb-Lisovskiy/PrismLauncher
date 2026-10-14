// SPDX-FileCopyrightText: 2026 Kerilom <89039577+Gleb-Lisovskiy@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "LocalResourceCopyTask.h"

#include <QFile>
#include <QFileInfo>

namespace {
/** Chunk size for the copy: large enough that a hundred megabyte mod is not a hundred thousand
 *  iterations, small enough that the reported progress visibly moves. */
constexpr qint64 kChunkSize = 1024 * 1024;
}  // namespace

LocalResourceCopyTask::LocalResourceCopyTask(const QString& sourcePath, const QString& destinationPath, QObject* parent)
    : Task(false), m_source(sourcePath), m_destination(destinationPath)
{
    setAbortable(true);
    setProgress(0, 100);
}

bool LocalResourceCopyTask::abort()
{
    // Only raise the flag. Task::abort() emits the aborted signal straight away, which is wrong from
    // in here: the copy is still running and would carry on to emit succeeded afterwards. The loop
    // notices the flag on its next chunk, cleans up and emits aborted itself.
    m_aborted = true;
    return canAbort();
}

void LocalResourceCopyTask::executeTask()
{
    setStatus(tr("Copying %1").arg(QFileInfo(m_source).fileName()));

    QFile source(m_source);
    if (!source.open(QIODevice::ReadOnly)) {
        emitFailed(tr("Could not read %1: %2").arg(m_source, source.errorString()));
        return;
    }

    QFile destination(m_destination);
    if (!destination.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emitFailed(tr("Could not write %1: %2").arg(m_destination, destination.errorString()));
        return;
    }

    const auto total = qMax<qint64>(source.size(), 1);
    qint64 copied = 0;

    auto giveUp = [&](const QString& reason) {
        destination.close();
        QFile::remove(m_destination);  // A half-written jar is worse than no jar.
        emitFailed(reason);
    };

    while (true) {
        if (m_aborted) {
            destination.close();
            QFile::remove(m_destination);
            emitAborted();
            return;
        }

        const auto chunk = source.read(kChunkSize);
        if (chunk.isEmpty()) {
            break;
        }
        if (destination.write(chunk) != chunk.size()) {
            giveUp(tr("Could not write %1: %2").arg(m_destination, destination.errorString()));
            return;
        }

        copied += chunk.size();
        setProgress(static_cast<int>(100 * copied / total), 100);
    }

    if (!destination.flush()) {
        giveUp(tr("Could not write %1: %2").arg(m_destination, destination.errorString()));
        return;
    }
    destination.close();

    setProgress(100, 100);
    emitSucceeded();
}