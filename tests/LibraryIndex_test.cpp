// SPDX-FileCopyrightText: 2026 Kerilom <89039577+Gleb-Lisovskiy@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include "FileSystem.h"
#include "archive/ArchiveWriter.h"
#include "minecraft/mod/LibraryIndex.h"

/* The library has no manifest to keep in step: the directory is the list. That makes the scan the
 * only thing standing between what the user put on disk and what the dialog shows, so most of what
 * is worth testing is what a scan makes of a folder in various states. */
class LibraryIndexTest : public QObject {
    Q_OBJECT

   private:
    QTemporaryDir m_dir;

    /** A private directory per test method, so a later test cannot see an earlier one's files. */
    QString uniqueDir() const { return FS::PathCombine(m_dir.path(), QTest::currentTestFunction()); }

    QString libraryDir() const { return FS::PathCombine(uniqueDir(), "localfiles"); }

    static bool writeFile(const QString& path, const QByteArray& contents)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            return false;
        }
        return file.write(contents) == contents.size();
    }

    QString writeSource(const QString& name, const QByteArray& contents)
    {
        const QString path = FS::PathCombine(uniqueDir(), "src-" + name);
        return writeFile(path, contents) ? path : QString();
    }

    static QByteArray readAll(const QString& path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return {};
        }
        return file.readAll();
    }

    /** Copies a file the helpers wrote into the library, which is how a hand-placed payload gets
     *  there. */
    static bool place(const QString& source, const QString& destination) { return writeFile(destination, readAll(source)); }

    /** Writes a zip holding the given files, which is enough of a jar for the identification code
     *  to recognise. Returns an empty string on failure. */
    QString writeZip(const QString& fileName, const QList<QPair<QString, QByteArray>>& contents)
    {
        const QString path = FS::PathCombine(uniqueDir(), fileName);
        QDir().mkpath(uniqueDir());

        MMCZip::ArchiveWriter writer(path);
        if (!writer.open()) {
            return {};
        }
        for (const auto& [name, data] : contents) {
            if (!writer.addFile(name, data)) {
                return {};
            }
        }
        if (!writer.close()) {
            return {};
        }
        return path;
    }

    /** A minimal Fabric mod jar, which is as close to a real one as a test needs. */
    QString writeFabricMod(const QString& fileName, const QString& modId, const QString& version, const QString& name)
    {
        QJsonObject object;
        object.insert("schemaVersion", 1);
        object.insert("id", modId);
        object.insert("version", version);
        object.insert("name", name);
        object.insert("description", "a mod that describes itself");
        const auto json = QJsonDocument(object).toJson(QJsonDocument::Indented);
        return writeZip(fileName, { { "fabric.mod.json", json } });
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
        return entry;
    }

    /** The path a file dropped by hand has once scanned. */
    static QString typePath(const LibraryIndex& lib, LibraryIndex::Type type, const QString& fileName)
    {
        return FS::PathCombine(FS::PathCombine(lib.rootDir(), LibraryIndex::typeDirName(type)), fileName);
    }

   private slots:
    void testEmptyLibraryIsNotAnError()
    {
        LibraryIndex lib(libraryDir());
        QVERIFY(lib.rescan());
        QVERIFY(lib.empty());
        QCOMPARE(lib.size(), 0);
    }

    void testAddStoresPayloadAndNotes()
    {
        LibraryIndex lib(libraryDir());
        // A real jar, because the identity of a mod comes out of its metadata and has to survive
        // the next scan: a scan reads the file, it does not remember what add() was told.
        const QString src = writeFabricMod("sodium-0.5.8.jar", "sodium", "0.5.8", "Sodium");
        QVERIFY(!src.isEmpty());

        LibraryIndex::Entry entry;
        entry.type = LibraryIndex::Type::Mod;
        entry.origin.provider = ModPlatform::ResourceProvider::MODRINTH;
        entry.origin.projectId = "AANobbMI";
        entry.origin.fileId = "abc123";

        QString error;
        QVERIFY2(lib.add(entry, src, &error), qPrintable(error));
        QCOMPARE(lib.size(), 1);

        // add() must hand back the identity and path it settled on, otherwise callers have to
        // look them up again.
        QVERIFY(!entry.id.isEmpty());
        QVERIFY(entry.file.startsWith("mods/"));

        const auto* stored = lib.find(entry.id);
        QVERIFY(stored);
        QCOMPARE(stored->details.mod_id, QString("sodium"));
        QCOMPARE(stored->origin.projectId, QString("AANobbMI"));
        QVERIFY(QFileInfo::exists(lib.absoluteFilePath(*stored)));

        // Where it came from is written next to the payload, so it survives a restart without a
        // manifest to hold it.
        QVERIFY(QFileInfo::exists(LibraryIndex::notesPathFor(lib.absoluteFilePath(*stored))));

        LibraryIndex reopened(lib.rootDir());
        QVERIFY(reopened.rescan());
        QCOMPARE(reopened.size(), 1);
        // entries() hands back a copy, so the entry has to be taken by value: binding a reference
        // to an element of the temporary would leave it dangling the moment the statement ends.
        const auto reread = reopened.entries().first();
        QVERIFY(reread.origin.isValid());
        QCOMPARE(reread.origin.provider, ModPlatform::ResourceProvider::MODRINTH);
        QCOMPARE(reread.origin.projectId, QString("AANobbMI"));
        QCOMPARE(reread.origin.fileId, QString("abc123"));
        QCOMPARE(reread.details.mod_id, QString("sodium"));
        QCOMPARE(reread.details.version, QString("0.5.8"));
        QCOMPARE(reread.id, entry.id);  // The identity a caller was handed still finds the entry.
    }

    void testNotesPathIsNextToThePayload()
    {
        QCOMPARE(LibraryIndex::notesPathFor("/library/mods/sodium.jar"), QString("/library/mods/sodium.jar.md"));
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

        // And the folder is clean enough that a scan does not find the old copy again.
        LibraryIndex reopened(lib.rootDir());
        QVERIFY(reopened.rescan());
        QCOMPARE(reopened.size(), 1);
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

    void testFilesDroppedIntoTheFolderArePickedUp()
    {
        const QString root = libraryDir();

        // Simulate a user copying files into the folder by hand instead of using the dialog.
        QVERIFY(writeFile(FS::PathCombine(root, "mods/handmade.jar"), "data"));
        QVERIFY(writeFile(FS::PathCombine(root, "shaderpacks/glow.zip"), "data"));

        LibraryIndex lib(root);
        QVERIFY(lib.rescan());
        QCOMPARE(lib.size(), 2);

        const auto* mod = lib.find(LibraryIndex::makeId(LibraryIndex::Type::Mod, ModDetails(), "handmade.jar"));
        QVERIFY(mod);
        QCOMPARE(mod->file, QString("mods/handmade.jar"));
        QCOMPARE(mod->details.name, QString("handmade"));

        const auto* shader = lib.find(LibraryIndex::makeId(LibraryIndex::Type::ShaderPack, ModDetails(), "glow.zip"));
        QVERIFY(shader);
        QCOMPARE(shader->type, LibraryIndex::Type::ShaderPack);

        // Nothing is written back: there is no manifest to write.
        QVERIFY(!QFileInfo::exists(FS::PathCombine(root, "localfiles.json")));
    }

    void testDeletingAPayloadRemovesItFromTheNextScan()
    {
        LibraryIndex lib(libraryDir());
        QVERIFY(writeFile(typePath(lib, LibraryIndex::Type::Mod, "handmade.jar"), "data"));
        QVERIFY(lib.rescan());
        QCOMPARE(lib.size(), 1);

        QVERIFY(QFile::remove(lib.absoluteFilePath(lib.entries().first())));

        LibraryIndex reopened(lib.rootDir());
        QVERIFY(reopened.rescan());
        QVERIFY(reopened.empty());
    }

    void testPayloadOutsideTypeDirectoriesIsIdentifiedByContents()
    {
        LibraryIndex lib(libraryDir());

        // A jar in a directory that is not one of the four: what it is has to come from opening it.
        const QString jar = writeFabricMod("build-42.jar", "examplemod", "4.2.0", "Example Mod");
        QVERIFY(!jar.isEmpty());
        QVERIFY(place(jar, FS::PathCombine(lib.rootDir(), "inbox/build-42.jar")));

        QVERIFY(lib.rescan());
        QCOMPARE(lib.size(), 1);

        const auto entry = lib.entries().first();
        QCOMPARE(entry.type, LibraryIndex::Type::Mod);
        QCOMPARE(entry.file, QString("inbox/build-42.jar"));
        QCOMPARE(entry.details.mod_id, QString("examplemod"));
    }

    void testFileThatIsNothingUsableIsIgnored()
    {
        LibraryIndex lib(libraryDir());
        QVERIFY(writeFile(FS::PathCombine(lib.rootDir(), "readme.txt"), "just some notes"));

        QVERIFY(lib.rescan());
        QVERIFY(lib.empty());
    }

    void testPlacedTypeDirectoryWinsOverContents()
    {
        LibraryIndex lib(libraryDir());

        // A mod jar put into resourcepacks/ is taken at its word: the placement was deliberate.
        const QString jar = writeFabricMod("thing.jar", "examplemod", "4.2.0", "Example Mod");
        QVERIFY(!jar.isEmpty());
        QVERIFY(place(jar, typePath(lib, LibraryIndex::Type::ResourcePack, "thing.jar")));

        QVERIFY(lib.rescan());
        QCOMPARE(lib.size(), 1);
        QCOMPARE(lib.entries().first().type, LibraryIndex::Type::ResourcePack);
        QCOMPARE(lib.entries().first().file, QString("resourcepacks/thing.jar"));
    }

    void testNotesWithFrontMatterDescribeTheEntry()
    {
        LibraryIndex lib(libraryDir());
        const QString payload = typePath(lib, LibraryIndex::Type::Mod, "sodium.jar");
        QVERIFY(writeFile(payload, "payload"));
        QVERIFY(writeFile(LibraryIndex::notesPathFor(payload),
                          "---\n"
                          "provider: modrinth\n"
                          "project-id: AANobbMI\n"
                          "file-id: abc123\n"
                          "mc-version: 1.20.1, 1.20.2\n"
                          "---\n"
                          "# Sodium\n"
                          "A rendering mod.\n"));

        LibraryIndex reopened(lib.rootDir());
        QVERIFY(reopened.rescan());
        QCOMPARE(reopened.size(), 1);

        const auto entry = reopened.entries().first();
        QVERIFY(entry.origin.isValid());
        QCOMPARE(entry.origin.provider, ModPlatform::ResourceProvider::MODRINTH);
        QCOMPARE(entry.origin.projectId, QString("AANobbMI"));
        QCOMPARE(entry.origin.fileId, QString("abc123"));
        QCOMPARE(entry.details.mcversion, QString("1.20.1, 1.20.2"));
        QCOMPARE(entry.notes, QString("# Sodium\nA rendering mod."));
    }

    void testNotesWithoutFrontMatterIsAllDescription()
    {
        LibraryIndex lib(libraryDir());
        const QString payload = typePath(lib, LibraryIndex::Type::Mod, "sodium.jar");
        QVERIFY(writeFile(payload, "payload"));
        QVERIFY(writeFile(LibraryIndex::notesPathFor(payload), "Just a description.\nSecond line.\n"));

        LibraryIndex reopened(lib.rootDir());
        QVERIFY(reopened.rescan());
        QCOMPARE(reopened.size(), 1);

        const auto entry = reopened.entries().first();
        QVERIFY(!entry.origin.isValid());
        QCOMPARE(entry.notes, QString("Just a description.\nSecond line."));
    }

    void testNotesCanOverrideNameAndVersion()
    {
        LibraryIndex lib(libraryDir());
        const QString payload = typePath(lib, LibraryIndex::Type::Mod, "sodium.jar");
        QVERIFY(writeFile(payload, "payload"));
        QVERIFY(writeFile(LibraryIndex::notesPathFor(payload), "---\nname: Sodium\nversion: 0.5.8\n---\n"));

        LibraryIndex reopened(lib.rootDir());
        QVERIFY(reopened.rescan());
        QCOMPARE(reopened.size(), 1);
        QCOMPARE(reopened.entries().first().details.name, QString("Sodium"));
        QCOMPARE(reopened.entries().first().details.version, QString("0.5.8"));
    }

    void testNotesFileIsNotItselfAPayload()
    {
        LibraryIndex lib(libraryDir());
        QVERIFY(writeFile(typePath(lib, LibraryIndex::Type::Mod, "sodium.jar"), "payload"));
        QVERIFY(writeFile(typePath(lib, LibraryIndex::Type::Mod, "sodium.jar.md"), "notes"));

        QVERIFY(lib.rescan());
        QCOMPARE(lib.size(), 1);  // The .md is notes, not a second entry.
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

        // Entries with no metadata of their own, which is what a pack dropped in by hand looks
        // like: the identity then comes from the file name, which the scan sees unchanged.
        {
            LibraryIndex lib(root);
            QString error;
            for (const auto& [type, dirName] : cases) {
                LibraryIndex::Entry entry;
                entry.type = type;
                const QString src = FS::PathCombine(uniqueDir(), dirName + ".zip");
                QVERIFY(writeFile(src, "data"));
                QVERIFY2(lib.add(entry, src, &error), qPrintable(error));
                QVERIFY2(entry.file.startsWith(dirName + "/"), qPrintable(entry.file));
            }
            QCOMPARE(lib.size(), 4);
        }

        LibraryIndex reopened(root);
        QVERIFY(reopened.rescan());
        QCOMPARE(reopened.size(), 4);
        for (const auto& [type, dirName] : cases) {
            const auto* entry = reopened.find(LibraryIndex::makeId(type, ModDetails(), dirName + ".zip"));
            QVERIFY(entry);
            QCOMPARE(entry->type, type);
            QCOMPARE(entry->file, dirName + "/" + dirName + ".zip");
        }
    }

    void testReadsModMetadataFromTheJarItself()
    {
        // A jar dropped straight into the library has no manifest entry, so everything the entry
        // says about the mod has to come out of the file.
        LibraryIndex lib(libraryDir());
        const QString jar = writeFabricMod("some-random-build-1.jar", "examplemod", "4.2.0", "Example Mod");
        QVERIFY(!jar.isEmpty());
        QVERIFY(place(jar, typePath(lib, LibraryIndex::Type::Mod, "some-random-build-1.jar")));

        QVERIFY(lib.rescan());
        QCOMPARE(lib.size(), 1);

        const auto entry = lib.entries().first();
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
        // Not a zip at all, so there is nothing to read metadata from.
        QVERIFY(writeFile(typePath(lib, LibraryIndex::Type::Mod, "broken-mod.jar"), "this is not a jar"));

        QVERIFY(lib.rescan());
        QCOMPARE(lib.size(), 1);
        QCOMPARE(lib.entries().first().details.name, QString("broken-mod"));
        QVERIFY(lib.entries().first().details.mod_id.isEmpty());
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

    void testAddKeepsTheDescriptionAlreadyInTheNotes()
    {
        LibraryIndex lib(libraryDir());
        const QString src = writeFabricMod("examplemod-4.2.0.jar", "examplemod", "4.2.0", "Example Mod");
        QVERIFY(!src.isEmpty());

        auto entry = makeEntry("examplemod", "Example Mod");
        QVERIFY(lib.add(entry, src));
        QVERIFY(writeFile(LibraryIndex::notesPathFor(lib.absoluteFilePath(entry)), "# My own notes\n"));

        // Re-adding the same version writes the front matter again but must not eat the body.
        auto again = makeEntry("examplemod", "Example Mod");
        QVERIFY(lib.add(again, src));

        LibraryIndex reopened(lib.rootDir());
        QVERIFY(reopened.rescan());
        QCOMPARE(reopened.size(), 1);
        QCOMPARE(reopened.entries().first().notes, QString("# My own notes"));
    }

    void testClashingSameModAndVersionKeepsOneEntry()
    {
        LibraryIndex lib(libraryDir());
        // Two files that both say they are version 4.2.0 of examplemod. A folder people copy into by
        // hand ends up like this, and the scan has to pick one rather than list both.
        const QString first = writeFabricMod("examplemod-4.2.0.jar", "examplemod", "4.2.0", "Example Mod");
        const QString second = writeFabricMod("examplemod-4.2.0-other.jar", "examplemod", "4.2.0", "Example Mod");
        QVERIFY(!first.isEmpty() && !second.isEmpty());

        QVERIFY(place(first, typePath(lib, LibraryIndex::Type::Mod, "examplemod-4.2.0.jar")));
        QVERIFY(place(second, typePath(lib, LibraryIndex::Type::Mod, "examplemod-4.2.0-other.jar")));

        QVERIFY(lib.rescan());
        QCOMPARE(lib.size(), 1);
        QCOMPARE(lib.entries().first().details.mod_id, QString("examplemod"));
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

    void testAddRejectsUnusableFileName()
    {
        LibraryIndex lib(libraryDir());
        QString error;

        // A name that is nothing but characters the filesystem rejects still has to land somewhere
        // rather than fail; the entry keeps a usable identity and the payload is written.
        auto entry = makeEntry("thing", "Thing");
        QVERIFY2(lib.add(entry, writeSource("thing.jar", "payload"), &error), qPrintable(error));
        QVERIFY(!entry.file.isEmpty());
        QVERIFY(QFileInfo::exists(lib.absoluteFilePath(entry)));
    }

    void testRescanUsesTheCacheWithoutLosingChanges()
    {
        LibraryIndex lib(libraryDir());
        const QString payload = typePath(lib, LibraryIndex::Type::Mod, "examplemod.jar");
        const QString jar = writeFabricMod("examplemod.jar", "examplemod", "4.2.0", "Example Mod");
        QVERIFY(!jar.isEmpty());
        QVERIFY(place(jar, payload));

        QVERIFY(lib.rescan());
        QCOMPARE(lib.size(), 1);
        QCOMPARE(lib.entries().first().details.name, QString("Example Mod"));

        // A second scan must find the same thing ...
        QVERIFY(lib.rescan());
        QCOMPARE(lib.size(), 1);
        QCOMPARE(lib.entries().first().details.name, QString("Example Mod"));

        // ... and a changed file must be read again rather than served from the cache.
        const QString replaced = writeFabricMod("other.jar", "examplemod", "5.0.0", "Example Mod Renamed");
        QVERIFY(!replaced.isEmpty());
        QVERIFY(place(replaced, payload));

        QVERIFY(lib.rescan());
        QCOMPARE(lib.size(), 1);
        QCOMPARE(lib.entries().first().details.version, QString("5.0.0"));
        QCOMPARE(lib.entries().first().details.name, QString("Example Mod Renamed"));
    }

    void testTypeDirNamesAreDocumented()
    {
        QCOMPARE(LibraryIndex::typeDirName(LibraryIndex::Type::Mod), QString("mods"));
        QCOMPARE(LibraryIndex::typeDirName(LibraryIndex::Type::ResourcePack), QString("resourcepacks"));
        QCOMPARE(LibraryIndex::typeDirName(LibraryIndex::Type::ShaderPack), QString("shaderpacks"));
        QCOMPARE(LibraryIndex::typeDirName(LibraryIndex::Type::TexturePack), QString("texturepacks"));
    }

    /** The LocalFilesDir setting is a bare folder name by default and an absolute path once the user
     *  browses for one; both have to end up pointing at the folder the user meant. */
    void testSettingResolvesRelativeAndAbsolutePaths()
    {
        const QString base = uniqueDir();
        const QString dataRoot = FS::PathCombine(base, "data");

        // A bare name lives under the launcher's data folder.
        QCOMPARE(LibraryIndex::resolveRootDir(dataRoot, "localfiles"), FS::PathCombine(dataRoot, "localfiles"));

        // An absolute path is used as it is: joining it onto the data folder would bury the chosen
        // folder inside it, which is what made the setting look broken.
        const QString absolute = FS::PathCombine(base, "elsewhere", "chosen-folder");
        const QString resolved = LibraryIndex::resolveRootDir(dataRoot, absolute);
        QVERIFY(QDir::isAbsolutePath(resolved));
        QCOMPARE(resolved, QDir::cleanPath(absolute));
        QVERIFY(!resolved.startsWith(dataRoot));

        // A cleared setting must not quietly turn the whole data folder into the library.
        QCOMPARE(LibraryIndex::resolveRootDir(dataRoot, ""), FS::PathCombine(dataRoot, "localfiles"));
    }
};

QTEST_GUILESS_MAIN(LibraryIndexTest)

#include "LibraryIndex_test.moc"
