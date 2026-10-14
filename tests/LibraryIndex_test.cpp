// SPDX-FileCopyrightText: 2026 Kerilom <89039577+Gleb-Lisovskiy@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include <QTemporaryDir>
#include <QTest>

#include "FileSystem.h"
#include "archive/ArchiveWriter.h"
#include "minecraft/mod/LibraryIndex.h"

class LibraryIndexTest : public QObject {
    Q_OBJECT

   private:
    QTemporaryDir m_dir;

    /** A private directory per test method.
     *
     *  m_dir is shared by every test in the class, and load() now discovers files that were placed
     *  in the type directories without going through add(). Without a per-test root, leftovers
     *  from an earlier test would show up in the next one's counts. */
    QString uniqueDir() const { return FS::PathCombine(m_dir.path(), QTest::currentTestFunction()); }

    QString libraryDir() const { return FS::PathCombine(uniqueDir(), "localfiles"); }

    QString writeSource(const QString& name, const QByteArray& contents)
    {
        QDir().mkpath(uniqueDir());
        const QString path = FS::PathCombine(uniqueDir(), "src-" + name);
        QFile file(path);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(contents);
        }
        return path;
    }

    /** Writes a minimal Fabric mod jar, which is as close to a real one as a test needs.
     *
     *  Returns an empty string on failure. The metadata lives in fabric.mod.json, exactly where a
     *  real loader would look for it. */
    QString writeFabricMod(const QString& fileName, const QString& modId, const QString& version, const QString& name)
    {
        QJsonObject object;
        object.insert("schemaVersion", 1);
        object.insert("id", modId);
        object.insert("version", version);
        object.insert("name", name);
        object.insert("description", "a mod that describes itself");
        const auto json = QJsonDocument(object).toJson(QJsonDocument::Indented);

        const QString path = FS::PathCombine(uniqueDir(), fileName);
        QDir().mkpath(uniqueDir());
        MMCZip::ArchiveWriter writer(path);
        if (!writer.open()) {
            return {};
        }
        if (!writer.addFile("fabric.mod.json", json)) {
            return {};
        }
        if (!writer.close()) {
            return {};
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
        LibraryIndex lib(libraryDir());
        QVERIFY(lib.load());
        QVERIFY(lib.empty());
        QCOMPARE(lib.size(), 0);
    }

    void testAddStoresPayloadAndManifest()
    {
        LibraryIndex lib(libraryDir());
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
        LibraryIndex lib(libraryDir());
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
        const QString root = libraryDir();
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
        LibraryIndex lib(libraryDir());
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
        LibraryIndex lib(libraryDir());
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

        QVERIFY(LibraryIndex::makeId(LibraryIndex::Type::Mod, one.details, "a.jar") !=
                LibraryIndex::makeId(LibraryIndex::Type::ShaderPack, otherType.details, "a.jar"));
        QVERIFY(LibraryIndex::makeId(LibraryIndex::Type::Mod, one.details, "a.jar") !=
                LibraryIndex::makeId(LibraryIndex::Type::Mod, otherMod.details, "a.jar"));
        QVERIFY(LibraryIndex::makeId(LibraryIndex::Type::Mod, one.details, "a.jar") !=
                LibraryIndex::makeId(LibraryIndex::Type::Mod, otherVersion.details, "a.jar"));
        // With a mod id the file name does not matter.
        QCOMPARE(LibraryIndex::makeId(LibraryIndex::Type::Mod, one.details, "a.jar"),
                 LibraryIndex::makeId(LibraryIndex::Type::Mod, one.details, "b.jar"));
        // Without a mod id the file name carries the identity.
        QVERIFY(LibraryIndex::makeId(LibraryIndex::Type::Mod, ModDetails(), "a.jar") !=
                LibraryIndex::makeId(LibraryIndex::Type::Mod, ModDetails(), "b.jar"));
        QVERIFY(!LibraryIndex::makeId(LibraryIndex::Type::Mod, noVersion.details, "a.jar").isEmpty());
    }

    void testFallsBackToFileNameWhenNoModId()
    {
        LibraryIndex lib(libraryDir());
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
        LibraryIndex lib(libraryDir());
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
        const QString root = libraryDir();
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

    void testFilesDroppedIntoTheFolderArePickedUp()
    {
        const QString root = libraryDir();

        // Simulate a user copying a jar into the folder by hand instead of using the dialog.
        QVERIFY(QDir(FS::PathCombine(root, "mods")).mkpath("."));
        QVERIFY(QDir(FS::PathCombine(root, "shaderpacks")).mkpath("."));
        for (const auto& [dirName, fileName] : { std::pair{ "mods", "handmade.jar" }, std::pair{ "shaderpacks", "glow.zip" } }) {
            QFile file(FS::PathCombine(FS::PathCombine(root, dirName), fileName));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("data");
        }

        LibraryIndex lib(root);
        QVERIFY(lib.load());
        QCOMPARE(lib.size(), 2);

        const auto* mod = lib.find(LibraryIndex::makeId(LibraryIndex::Type::Mod, ModDetails(), "handmade.jar"));
        QVERIFY(mod);
        QCOMPARE(mod->file, QString("mods/handmade.jar"));
        QCOMPARE(mod->details.name, QString("handmade"));
        QVERIFY(!mod->broken);

        const auto* shader = lib.find(LibraryIndex::makeId(LibraryIndex::Type::ShaderPack, ModDetails(), "glow.zip"));
        QVERIFY(shader);
        QCOMPARE(shader->type, LibraryIndex::Type::ShaderPack);

        // And the discovery is written back, so it does not happen again on every load.
        LibraryIndex reopened(root);
        QVERIFY(reopened.load());
        QCOMPARE(reopened.size(), 2);
        QVERIFY(QFileInfo::exists(reopened.manifestPath()));
    }

    void testDiscoveryDoesNotDuplicateOrResurrectRemovedEntries()
    {
        LibraryIndex lib(libraryDir());
        QString error;
        auto entry = makeEntry("thing", "Thing");
        QVERIFY2(lib.add(entry, writeSource("thing.jar", "payload"), &error), qPrintable(error));

        const QString id = lib.find(LibraryIndex::makeId(LibraryIndex::Type::Mod, entry.details, "thing.jar"))->id;
        QVERIFY2(lib.remove(id, &error), qPrintable(error));

        // remove() deletes the payload too, so a reload must not resurrect it.
        LibraryIndex reopened(lib.rootDir());
        QVERIFY(reopened.load());
        QVERIFY(reopened.empty());
    }

    void testEntriesWithUnsafePathsAreRejectedAsBroken()
    {
        const QString root = libraryDir();
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
        const QString root = libraryDir();
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
        const QString root = libraryDir();
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
        const QString root = libraryDir();
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

    void testReadsModMetadataFromTheJarItself()
    {
        // A jar dropped straight into the library has no manifest entry, so everything the entry
        // says about the mod has to come out of the file.
        LibraryIndex lib(libraryDir());
        QVERIFY(lib.load());

        const QString modsDir = FS::PathCombine(lib.rootDir(), LibraryIndex::typeDirName(LibraryIndex::Type::Mod));
        QVERIFY(QDir().mkpath(modsDir));
        const QString jar = writeFabricMod("some-random-build-1.jar", "examplemod", "4.2.0", "Example Mod");
        QVERIFY(!jar.isEmpty());
        QVERIFY(QFile::copy(jar, FS::PathCombine(modsDir, "some-random-build-1.jar")));

        LibraryIndex reopened(lib.rootDir());
        QVERIFY(reopened.load());
        QCOMPARE(reopened.size(), 1);

        const auto& entry = reopened.entries().first();
        QCOMPARE(entry.details.mod_id, QString("examplemod"));
        QCOMPARE(entry.details.version, QString("4.2.0"));
        QCOMPARE(entry.details.name, QString("Example Mod"));
        QCOMPARE(entry.details.description, QString("a mod that describes itself"));

        // The id is derived from the mod id, so renaming the file does not change the identity.
        QCOMPARE(entry.id, LibraryIndex::makeId(LibraryIndex::Type::Mod, entry.details, "anything-else.jar"));
    }

    void testUnreadableModKeepsTheFileNameAsItsName()
    {
        LibraryIndex lib(libraryDir());
        QVERIFY(lib.load());

        const QString modsDir = FS::PathCombine(lib.rootDir(), LibraryIndex::typeDirName(LibraryIndex::Type::Mod));
        QVERIFY(QDir().mkpath(modsDir));
        // Not a zip at all, so there is nothing to read metadata from.
        QFile junk(FS::PathCombine(modsDir, "broken-mod.jar"));
        QVERIFY(junk.open(QIODevice::WriteOnly));
        junk.write("this is not a jar");
        junk.close();

        LibraryIndex reopened(lib.rootDir());
        QVERIFY(reopened.load());
        QCOMPARE(reopened.size(), 1);
        QCOMPARE(reopened.entries().first().details.name, QString("broken-mod"));
        QVERIFY(reopened.entries().first().details.mod_id.isEmpty());
    }

    void testAddReadsModMetadataWhenTheCallerSuppliesNone()
    {
        LibraryIndex lib(libraryDir());
        const QString src = writeFabricMod("examplemod-4.2.0.jar", "examplemod", "4.2.0", "Example Mod");
        QVERIFY(!src.isEmpty());

        // The usual path when a user drops a file in: no description at all.
        LibraryIndex::Entry entry;
        entry.type = LibraryIndex::Type::Mod;

        QString error;
        QVERIFY2(lib.add(entry, src, &error), qPrintable(error));
        QCOMPARE(entry.details.mod_id, QString("examplemod"));
        QCOMPARE(entry.details.version, QString("4.2.0"));
        QCOMPARE(entry.details.name, QString("Example Mod"));
    }

    void testAddKeepsMetadataTheCallerSupplied()
    {
        LibraryIndex lib(libraryDir());
        const QString src = writeFabricMod("examplemod-4.2.0.jar", "examplemod", "4.2.0", "Example Mod");
        QVERIFY(!src.isEmpty());

        // A file that came from a platform carries what the platform said, which is what the user
        // will recognise. The jar's own metadata must not overwrite it.
        auto entry = makeEntry("examplemod", "Example Mod", LibraryIndex::Type::Mod, "9.9.9");

        QString error;
        QVERIFY2(lib.add(entry, src, &error), qPrintable(error));
        QCOMPARE(entry.details.version, QString("9.9.9"));
    }

    void testReadsMetadataForEntriesAnOlderManifestDescribedPoorly()
    {
        LibraryIndex lib(libraryDir());
        QString error;

        // An entry with nothing but a file name, which is what an older manifest held.
        auto entry = makeEntry("", "examplemod-4.2.0.jar", LibraryIndex::Type::Mod, "");
        const QString src = writeFabricMod("examplemod-4.2.0.jar", "examplemod", "4.2.0", "Example Mod");
        QVERIFY(!src.isEmpty());
        QVERIFY2(lib.add(entry, src, &error), qPrintable(error));

        // Reopen, having meanwhile emptied the description the way the old code left it.
        auto stale = lib.entries().first();
        stale.details = ModDetails();
        stale.details.name = "examplemod-4.2.0";
        {
            LibraryIndex rewrite(lib.rootDir());
            QVERIFY(rewrite.load());
            stale.id = rewrite.entries().first().id;
            QVERIFY2(rewrite.update(stale, &error), qPrintable(error));
        }

        LibraryIndex reopened(lib.rootDir());
        QVERIFY(reopened.load());
        QCOMPARE(reopened.size(), 1);
        QCOMPARE(reopened.entries().first().details.mod_id, QString("examplemod"));
        QCOMPARE(reopened.entries().first().details.version, QString("4.2.0"));
        QCOMPARE(reopened.entries().first().details.name, QString("Example Mod"));

        // And the repaired description is written back, so it is only read from the jar once.
        LibraryIndex again(lib.rootDir());
        QVERIFY(again.load());
        QCOMPARE(again.entries().first().details.mod_id, QString("examplemod"));
    }

    void testAddRejectsMissingSource()
    {
        LibraryIndex lib(libraryDir());
        QString error;
        auto ghost = makeEntry("ghost", "Ghost");
        QVERIFY(!lib.add(ghost, FS::PathCombine(m_dir.path(), "nope.jar"), &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(lib.empty());
    }
};

QTEST_GUILESS_MAIN(LibraryIndexTest)

#include "LibraryIndex_test.moc"