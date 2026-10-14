// SPDX-FileCopyrightText: 2026 Kerilom <89039577+Gleb-Lisovskiy@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "FileSystem.h"
#include "minecraft/mod/tasks/LocalResourceCopyTask.h"

/* The copy stands in for the network job when a resource comes out of the user's own library, so
 * that it can go through the same queue and show up in the review box like anything else. Getting it
 * wrong either way is invisible until a file ends up half-written or not at all. */
class LocalResourceCopy : public QObject {
    Q_OBJECT

   private:
    QTemporaryDir m_dir;

    QString uniqueDir() const { return FS::PathCombine(m_dir.path(), QTest::currentTestFunction()); }

    static QString writeFile(const QString& path, const QByteArray& contents)
    {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            return {};
        }
        file.write(contents);
        file.close();
        return path;
    }

    /** Runs the task to completion and returns what it ended up as. */
    static bool run(Task::Ptr task, bool* aborted = nullptr)
    {
        QEventLoop loop;
        bool failed = false;
        bool didAbort = false;
        QObject::connect(task.get(), &Task::succeeded, &loop, [&loop] { loop.quit(); });
        QObject::connect(task.get(), &Task::failed, &loop, [&loop, &failed] {
            failed = true;
            loop.quit();
        });
        QObject::connect(task.get(), &Task::aborted, &loop, [&loop, &didAbort] {
            didAbort = true;
            loop.quit();
        });

        task->start();
        if (!task->isFinished()) {
            loop.exec();
        }
        if (aborted) {
            *aborted = didAbort;
        }
        return !failed && !didAbort;
    }

   private slots:
    void testCopiesTheWholeFile()
    {
        QDir().mkpath(uniqueDir());
        const QByteArray payload(3 * 1024 * 1024 + 17, 'x');  // Spans several chunks.
        const auto source = writeFile(FS::PathCombine(uniqueDir(), "source.jar"), payload);
        const auto destination = FS::PathCombine(uniqueDir(), "target.jar");

        Task::Ptr task(new LocalResourceCopyTask(source, destination));
        QVERIFY(run(task));

        QVERIFY(QFileInfo::exists(destination));
        QFile copied(destination);
        QVERIFY(copied.open(QIODevice::ReadOnly));
        QCOMPARE(copied.readAll(), payload);
    }

    void testOverwritesAnExistingFile()
    {
        QDir().mkpath(uniqueDir());
        const auto source = writeFile(FS::PathCombine(uniqueDir(), "source.jar"), QByteArray("new contents"));
        const auto destination = writeFile(FS::PathCombine(uniqueDir(), "target.jar"), QByteArray("much older and longer"));

        Task::Ptr task(new LocalResourceCopyTask(source, destination));
        QVERIFY(run(task));

        QFile copied(destination);
        QVERIFY(copied.open(QIODevice::ReadOnly));
        QCOMPARE(copied.readAll(), QByteArray("new contents"));
    }

    void testMissingSourceLeavesNothingBehind()
    {
        QDir().mkpath(uniqueDir());
        const auto destination = FS::PathCombine(uniqueDir(), "target.jar");

        Task::Ptr task(new LocalResourceCopyTask(FS::PathCombine(uniqueDir(), "gone.jar"), destination));
        QVERIFY(!run(task));
        QVERIFY(!QFileInfo::exists(destination));
    }

    void testAbortLeavesNoPartialFile()
    {
        QDir().mkpath(uniqueDir());
        const QByteArray payload(8 * 1024 * 1024, 'y');
        const auto source = writeFile(FS::PathCombine(uniqueDir(), "source.jar"), payload);
        const auto destination = FS::PathCombine(uniqueDir(), "target.jar");

        auto task = Task::Ptr(new LocalResourceCopyTask(source, destination));
        QObject::connect(task.get(), &Task::progress, task.get(), [task] { task->abort(); });

        bool aborted = false;
        run(task, &aborted);

        // A jar cut in half would be picked up by the loader and fail much later, somewhere else.
        QVERIFY(aborted);
        QVERIFY(!QFileInfo::exists(destination));
    }
};

QTEST_GUILESS_MAIN(LocalResourceCopy)

#include "LocalResourceCopy_test.moc"