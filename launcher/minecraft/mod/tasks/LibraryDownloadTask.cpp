// SPDX-FileCopyrightText: 2026 Kerilom <89039577+Gleb-Lisovskiy@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "LibraryDownloadTask.h"

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include "Application.h"
#include "FileSystem.h"

LibraryDownloadTask::LibraryDownloadTask(QUrl url, LibraryIndex::Entry entry, LibraryIndex* library, QObject* parent)
    : Task(true), m_url(std::move(url)), m_entry(std::move(entry)), m_library(library)
{
    setObjectName("LibraryDownloadTask");
    if (parent) {
        setParent(parent);
    }
}

LibraryDownloadTask::~LibraryDownloadTask()
{
    cleanUp();
}

void LibraryDownloadTask::cleanUp()
{
    if (!m_tempPath.isEmpty()) {
        QFile::remove(m_tempPath);
        m_tempPath.clear();
    }
}

void LibraryDownloadTask::executeTask()
{
    if (m_url.isEmpty() || !m_url.isValid()) {
        emitFailed(tr("The file has no download link. Open the project page and add the file from disk."));
        return;
    }

    m_tempPath = QDir(QDir::tempPath()).absoluteFilePath(QString("library-%1").arg(QString::number(reinterpret_cast<quintptr>(this))));
    if (!FS::ensureFolderPathExists(QFileInfo(m_tempPath).absolutePath())) {
        emitFailed(tr("Could not create a temporary folder for the download."));
        return;
    }

    setStatus(tr("Downloading %1").arg(m_entry.details.name.isEmpty() ? m_url.fileName() : m_entry.details.name));
    setProgress(0, 100);

    m_download = Net::Download::makeFile(m_url, m_tempPath);
    connect(m_download.get(), &Task::succeeded, this, &LibraryDownloadTask::onSucceeded);
    connect(m_download.get(), &Task::failed, this, &LibraryDownloadTask::onFailed);

    m_download->start();
}

void LibraryDownloadTask::onSucceeded()
{
    if (!m_library) {
        emitFailed(tr("The library is not available."));
        return;
    }

    // The manifest keys the payload by file name, so give the temporary file the name it will have
    // in the library rather than letting the download invent one.
    const QString fileName = m_entry.file.isEmpty() ? m_url.fileName()
                                                     : QFileInfo(m_entry.file).fileName();
    const QString staged = m_tempPath + "." + (fileName.isEmpty() ? QString("download") : fileName);
    if (!QFile::rename(m_tempPath, staged)) {
        emitFailed(tr("Could not stage the downloaded file."));
        return;
    }
    m_tempPath = staged;

    QString error;
    if (!m_library->add(m_entry, staged, &error)) {
        cleanUp();
        emitFailed(error);
        return;
    }

    emitSucceeded();
}

void LibraryDownloadTask::onFailed()
{
    cleanUp();
    emitFailed(m_download ? m_download->failReason() : tr("The download failed."));
}