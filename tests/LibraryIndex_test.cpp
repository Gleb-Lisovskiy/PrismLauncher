// SPDX-FileCopyrightText: 2026 Kerilom <89039577+Gleb-Lisovskiy@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include <QTest>
#include <QTemporaryDir>

#include "FileSystem.h"
#include "minecraft/mod/LibraryIndex.h"

class LibraryIndexTest : public QObject {
    Q_OBJECT

   private:
    QTemporaryDir m_dir;

    QString writeSource(const QString& name, const QByteArray& contents)
    {
        const QString path = FS::PathCombine(m_dir.path(), "src-" + name);
        QFile file(path);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(contents);
        }
        return path;
    }

    static LibraryIndex::Entry makeEntry(const QString& modId,
                                        const QString& name,
                                        LibraryIndex::Type type = LibraryIndex::Type::Mod,
                                        const QString& version = "1.0.0")
    {
        LibraryIndex::Entry entry;
        entry.type = type;
        entry.details.mod_id = modId;
        entry.details.name = name;
        entry.details.version = version;
        entry.details.description = "a test mod";
        entry.projectId = "123";
        entry.fileId = "456";
        return entry;
    }

   private slots:
    void testEmptyLibraryIsNotAnError()
    {
        LibraryIndex lib(FS::PathCombine(m_dir.path(), "localfiles"));
        QVERIFY(lib.load());
        QVERIFY(lib.empty());
        QCOMPARE(lib.size(), 0);
    }

    void testAddStoresPayloadAndManifest()
    {
        LibraryIndex lib(FS::PathCombine(m_dir.path(), "localfiles"));
        const QString src = writeSource("thing.jar", "payload-one");

        auto entry = makeEntry("thing", "Thing");
        QString error;
        QVERIFY2(lib.add(entry, src, &error), qPrintable(error));
        QCOMPARE(lib.size(), 1);

        // add() must hand back the identity and path it settled on, otherwise callers have to
        // look them up again.
        QVERIFY(!entry.id.isEmpty());
        QVERIFY(entry.file.startsWith("mods/"));

        const auto* stored = lib.find(entry.id);
        QVERIFY(stored);
        QCOMPARE(stored->details.mod_id, QString("thing"));
        QCOMPARE(stored->details.name, QString("Thing"));
        QCOMPARE(stored->details.description, QString("a test mod"));
        QCOMPARE(stored->projectId, QString("123"));
        QVERIFY(QFileInfo::exists(lib.absoluteFilePath(*stored)));
        QVERIFY(!stored->broken);
    }

    void testSameModDifferentVersionsCoexist()
    {
        LibraryIndex lib(FS::PathCombine(m_dir.path(), "localfiles"));
        QString error;

        auto v1 = makeEntry("thing", "Thing", LibraryIndex::Type::Mod, "1.0.0");
        auto v2 = makeEntry("thing", "Thing", LibraryIndex::Type::Mod, "2.0.0");

        QVERIFY2(lib.add(v1, writeSource("thing-1.0.0.jar", "old"), &error), qPrintable(error));
        QVERIFY2(lib.add(v2, writeSource("thing-2.0.0.jar", "new"), &error), qPrintable(error));

        QCOMPARE(lib.size(), 2);  // Both versions kept, which is the point of a library.
        QVERIFY(v1.id != v2.id);

        // Re-adding the same version replaces rather than duplicating.
        auto v1again = makeEntry("thing", "Thing", LibraryIndex::Type::Mod, "1.0.0");
        QVERIFY2(lib.add(v1again, writeSource("thing-1.0.0.jar", "old"), &error), qPrintable(error));
        QCOMPARE(lib.size(), 2);
        QCOMPARE(v1again.id, v1.id);
    }

    void testRoundTrip()
    {
        const QString root = FS::PathCombine(m_dir.path(), "localfiles");
        const QString src = writeSource("thing.jar", "payload-one");

        QString error;
        {
            LibraryIndex lib(root);
            auto entry = makeEntry("thing", "Thing");
            QVERIFY2(lib.add(entry, src, &error), qPrintable(error));
        }

        LibraryIndex reopened(root);
        QVERIFY(reopened.load());
        QCOMPARE(reopened.size(), 1);

        const auto* entry = reopened.find(LibraryIndex::makeId(LibraryIndex::Type::Mod, makeEntry("thing", "Thing").details, "thing.jar"));
        QVERIFY(entry);
        QCOMPARE(entry->details.mod_id, QString("thing"));
        QCOMPARE(entry->details.version, QString("1.0.0"));
        QCOMPARE(entry->projectId, QString("123"));
        QCOMPARE(entry->fileId, QString("456"));
        QVERIFY(entry->addedAt.isValid());
        QVERIFY(!entry->broken);
    }

    void testReAddingSameVersionWithNewFileNameDropsOldPayload()
    {
        LibraryIndex lib(FS::PathCombine(m_dir.path(), "localfiles"));
        QString error;

        auto first = makeEntry("thing", "Thing");
        QVERIFY2(lib.add(first, writeSource("thing-1.0.0.jar", "version-one"), &error), qPrintable(error));
        const QString firstFile = first.file;

        auto second = makeEntry("thing", "Thing");
        QVERIFY2(lib.add(second, writeSource("thing-1.0.0-patch.jar", "version-two-longer"), &error), qPrintable(error));

        QCOMPARE(lib.size(), 1);  // Same identity, so replaced rather than duplicated.
        QVERIFY(second.file != firstFile);
        QVERIFY(!QFileInfo::exists(FS::PathCombine(lib.rootDir(), firstFile)));  // Orphaned payload removed.
        QVERIFY(QFileInfo::exists(lib.absoluteFilePath(lib.entries().first())));
    }

    void testReAddingUnderTheSameFileNameReusesThePath()
    {
        LibraryIndex lib(FS::PathCombine(m_dir.path(), "localfiles"));
        QString error;

        auto first = makeEntry("thing", "Thing");
        QVERIFY2(lib.add(first, writeSource("thing.jar", "version-one"), &error), qPrintable(error));

        auto second = makeEntry("thing", "Thing");
        QVERIFY2(lib.add(second, writeSource("thing.jar", "version-two"), &error), qPrintable(error));

        QCOMPARE(lib.size(), 1);
        // Same file name means the payload is replaced in place, so there is no orphan to clean up.
        QCOMPARE(second.file, first.file);
        QVERIFY(QFileInfo::exists(lib.absoluteFilePath(lib.entries().first())));
    }

    void testIdsDistinguishTypeModAndVersion()
    {
        const auto one = makeEntry("thing", "Thing", LibraryIndex::Type::Mod, "1.0.0");
        const auto otherType = makeEntry("thing", "Thing", LibraryIndex::Type::ShaderPack, "1.0.0");
        const auto otherMod = makeEntry("other", "Other", LibraryIndex::Type::Mod, "1.0.0");
        const auto otherVersion = makeEntry("thing", "Thing", LibraryIndex::Type::Mod, "2.0.0");
        const auto noVersion = makeEntry("thing", "Thing", LibraryIndex::Type::Mod, "");

        QVERIFY(LibraryIndex::makeId(LibraryIndex::Type::Mod, one.details, "a.jar")
                != LibraryIndex::makeId(LibraryIndex::Type::ShaderPack, otherType.details, "a.jar"));
        QVERIFY(LibraryIndex::makeId(LibraryIndex::Type::Mod, one.details, "a.jar")
                != LibraryIndex::makeId(LibraryIndex::Type::Mod, otherMod.details, "a.jar"));
        QVERIFY(LibraryIndex::makeId(LibraryIndex::Type::Mod, one.details, "a.jar")
                != LibraryIndex::makeId(LibraryIndex::Type::Mod, otherVersion.details, "a.jar"));
        // With a mod id the file name does not matter.
        QCOMPARE(LibraryIndex::makeId(LibraryIndex::Type::Mod, one.details, "a.jar"),
                 LibraryIndex::makeId(LibraryIndex::Type::Mod, one.details, "b.jar"));
        // Without a mod id the file name carries the identity.
        QVERIFY(LibraryIndex::makeId(LibraryIndex::Type::Mod, ModDetails(), "a.jar")
                != LibraryIndex::makeId(LibraryIndex::Type::Mod, ModDetails(), "b.jar"));
        QVERIFY(!LibraryIndex::makeId(LibraryIndex::Type::Mod, noVersion.details, "a.jar").isEmpty());
    }

    void testFallsBackToFileNameWhenNoModId()
    {
        LibraryIndex lib(FS::PathCombine(m_dir.path(), "localfiles"));
        QString error;
        // The identity comes from the source file's name, which is what gets stored.
        const QString src = writeSource("thing.jar", "payload");
        auto entry = makeEntry("", "Unnamed");
        QVERIFY2(lib.add(entry, src, &error), qPrintable(error));
        QCOMPARE(lib.size(), 1);
        QCOMPARE(entry.id, LibraryIndex::makeId(LibraryIndex::Type::Mod, entry.details, QFileInfo(src).fileName()));
    }

    void testRemoveDropsPayloadAndManifest()
    {
        LibraryIndex lib(FS::PathCombine(m_dir.path(), "localfiles"));
        QString error;
        auto entry = makeEntry("thing", "Thing");
        QVERIFY2(lib.add(entry, writeSource("thing.jar", "payload"), &error), qPrintable(error));

        const auto id = lib.entries().first().id;
        const QString file = lib.entries().first().file;
        QVERIFY2(lib.remove(id, &error), qPrintable(error));

        QVERIFY(lib.empty());
        QVERIFY(!QFileInfo::exists(FS::PathCombine(lib.rootDir(), file)));

        LibraryIndex reopened(lib.rootDir());
        QVERIFY(reopened.load());
        QVERIFY(reopened.empty());
    }

    void testMissingPayloadIsFlaggedNotDropped()
    {
        const QString root = FS::PathCombine(m_dir.path(), "localfiles");
        QString error;
        {
            LibraryIndex lib(root);
            auto entry = makeEntry("thing", "Thing");
        QVERIFY2(lib.add(entry, writeSource("thing.jar", "payload"), &error), qPrintable(error));
        }

        LibraryIndex probe(root);
        QVERIFY(probe.load());
        QVERIFY(QFile::remove(probe.absoluteFilePath(probe.entries().first())));

        LibraryIndex reopened(root);
        QVERIFY(reopened.load());
        QCOMPARE(reopened.size(), 1);  // Kept, so the user can see what is broken and re-add it.
        QVERIFY(reopened.entries().first().broken);
    }

    void testEntriesWithUnsafePathsAreRejectedAsBroken()
    {
        const QString root = FS::PathCombine(m_dir.path(), "localfiles");
        QVERIFY(QDir(root).mkpath("."));
        QFile manifest(FS::PathCombine(root, "localfiles.json"));
        QVERIFY(manifest.open(QIODevice::WriteOnly));
        manifest.write(R"({
            "version": 1,
            "entries": [
                { "id": "escape", "type": "mods", "file": "../../etc/passwd", "modId": "escape" },
                { "id": "absolute", "type": "mods", "file": "/etc/passwd", "modId": "absolute" },
                { "id": "outside", "type": "mods", "file": "elsewhere/x.jar", "modId": "outside" },
                { "id": "fine", "type": "mods", "file": "mods/x.jar", "modId": "fine" }
            ]
        })");
        manifest.close();

        LibraryIndex lib(root);
        QVERIFY(lib.load());
        QCOMPARE(lib.size(), 4);
        QVERIFY(lib.find("escape")->broken);
        QVERIFY(lib.find("absolute")->broken);
        QVERIFY(lib.find("outside")->broken);  // Not a known type directory, so not trusted.
    }

    void testMalformedManifestFailsWithoutLosingEntries()
    {
        const QString root = FS::PathCombine(m_dir.path(), "localfiles");
        QVERIFY(QDir(root).mkpath("."));
        QFile manifest(FS::PathCombine(root, "localfiles.json"));
        QVERIFY(manifest.open(QIODevice::WriteOnly));
        manifest.write("{ this is not json");
        manifest.close();

        LibraryIndex lib(root);
        QString error;
        QVERIFY(!lib.load(&error));
        QVERIFY(!error.isEmpty());
        QVERIFY(lib.empty());
    }

    void testIncompleteEntriesAreSkipped()
    {
        const QString root = FS::PathCombine(m_dir.path(), "localfiles");
        QVERIFY(QDir(root).mkpath("."));
        QFile manifest(FS::PathCombine(root, "localfiles.json"));
        QVERIFY(manifest.open(QIODevice::WriteOnly));
        manifest.write(R"({
            "version": 1,
            "entries": [
                { "type": "mods", "file": "mods/a.jar" },
                { "id": "no-file", "type": "mods" },
                { "id": "ok", "type": "shaderpacks", "file": "shaderpacks/b.zip", "modId": "ok" }
            ]
        })");
        manifest.close();

        LibraryIndex lib(root);
        QVERIFY(lib.load());
        QCOMPARE(lib.size(), 1);
        QCOMPARE(lib.entries().first().id, QString("ok"));
        QCOMPARE(lib.entries().first().type, LibraryIndex::Type::ShaderPack);
    }

    void testAllTypesRoundTrip()
    {
        const QString root = FS::PathCombine(m_dir.path(), "localfiles");
        const std::pair<LibraryIndex::Type, QString> cases[] = {
            { LibraryIndex::Type::Mod, "mods" },
            { LibraryIndex::Type::ResourcePack, "resourcepacks" },
            { LibraryIndex::Type::ShaderPack, "shaderpacks" },
            { LibraryIndex::Type::TexturePack, "texturepacks" },
        };

        {
            LibraryIndex lib(root);
            QString error;
            for (const auto& [type, dirName] : cases) {
                auto entry = makeEntry("x-" + dirName, "X", type);
                QVERIFY2(lib.add(entry, writeSource(dirName + ".zip", "data"), &error), qPrintable(error));
                QVERIFY2(entry.file.startsWith(dirName + "/"), qPrintable(entry.file));
            }
            QCOMPARE(lib.size(), 4);
        }

        LibraryIndex reopened(root);
        QVERIFY(reopened.load());
        QCOMPARE(reopened.size(), 4);
        for (const auto& [type, dirName] : cases) {
            const auto details = makeEntry("x-" + dirName, "X", type).details;
            const auto* entry = reopened.find(LibraryIndex::makeId(type, details, dirName + ".zip"));
            QVERIFY(entry);
            QCOMPARE(entry->type, type);
        }
    }

    void testAddRejectsMissingSource()
    {
        LibraryIndex lib(FS::PathCombine(m_dir.path(), "localfiles"));
        QString error;
        auto ghost = makeEntry("ghost", "Ghost");
        QVERIFY(!lib.add(ghost, FS::PathCombine(m_dir.path(), "nope.jar"), &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(lib.empty());
    }
};

QTEST_GUILESS_MAIN(LibraryIndexTest)

#include "LibraryIndex_test.moc"