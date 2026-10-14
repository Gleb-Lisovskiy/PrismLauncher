// SPDX-FileCopyrightText: 2026 Kerilom <89039577+Gleb-Lisovskiy@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include <QTemporaryDir>
#include <QTest>

#include "FileSystem.h"
#include "minecraft/mod/LibraryIndex.h"
#include "modplatform/library/LibraryAPI.h"

/* The page filters reject a version whose version_type or mcVersion does not line up with the
 * instance, and they reject it silently: the project shows up in the list and the version box then
 * reports that no matching version was found. A jar on disk records neither field, so the provider
 * has to fill them in, which is exactly the kind of thing that gets lost in a refactor. */
class LibraryVersions : public QObject {
    Q_OBJECT

   private:
    QTemporaryDir m_dir;

    QString uniqueDir() const { return FS::PathCombine(m_dir.path(), QTest::currentTestFunction()); }

    /** A library holding one mod dropped straight into the folder, which is how a file the user
     *  copied there by hand looks: named after the file, with no mod id, version or game version. */
    QString libraryDir() const { return FS::PathCombine(uniqueDir(), "localfiles"); }

    bool makeLibrary(LibraryIndex& lib)
    {
        lib = LibraryIndex(libraryDir());
        const QString modsDir = FS::PathCombine(lib.rootDir(), LibraryIndex::typeDirName(LibraryIndex::Type::Mod));
        if (!QDir().mkpath(modsDir)) {
            return false;
        }

        QFile file(FS::PathCombine(modsDir, "thing-1.0.0.jar"));
        if (!file.open(QIODevice::WriteOnly)) {
            return false;
        }
        file.write("not a real jar; discovery only cares that a file is there");
        file.close();

        return lib.load();
    }

    /** The single version the provider offers for that library entry. */
    static ModPlatform::IndexedVersion versionOf(LibraryAPI& api, LibraryIndex& lib)
    {
        auto pack = std::make_shared<ModPlatform::IndexedPack>();
        pack->addonId = lib.entries().first().id;

        ResourceAPI::Callback<QVector<ModPlatform::IndexedVersion>> callbacks{};
        QVector<ModPlatform::IndexedVersion> captured;
        callbacks.on_succeed = [&captured](auto& list) { captured = list; };

        api.getProjectVersions({ pack, {}, {}, ModPlatform::ResourceType::Mod }, std::move(callbacks));

        // The provider answers on the next event loop turn rather than inline.
        QCoreApplication::processEvents();
        return captured.isEmpty() ? ModPlatform::IndexedVersion() : captured.first();
    }

   private slots:
    void testVersionCarriesTheReleaseType()
    {
        LibraryIndex lib(libraryDir());
        QVERIFY(makeLibrary(lib));

        LibraryAPI api(&lib, LibraryIndex::Type::Mod, "1.20.1");
        const auto version = versionOf(api, lib);

        QVERIFY(!version.fileName.isEmpty());
        QCOMPARE(version.version_type, ModPlatform::IndexedVersionType::Release);
    }

    void testVersionCarriesTheInstancesMinecraftVersion()
    {
        LibraryIndex lib(libraryDir());
        QVERIFY(makeLibrary(lib));

        LibraryAPI api(&lib, LibraryIndex::Type::Mod, "1.20.1");
        QCOMPARE(versionOf(api, lib).mcVersion, QStringList{ "1.20.1" });
    }

    void testEntryThatDeclaresAVersionWinsOverTheInstance()
    {
        LibraryIndex lib(libraryDir());
        QVERIFY(makeLibrary(lib));

        auto entry = lib.entries().first();
        entry.details.mcversion = "1.12.2, 1.16.5";
        QVERIFY(lib.update(entry));

        LibraryAPI api(&lib, LibraryIndex::Type::Mod, "1.20.1");
        QCOMPARE(versionOf(api, lib).mcVersion, (QStringList{ "1.12.2", "1.16.5" }));
    }

    void testDownloadUrlIsAPathNotAnUrl()
    {
        LibraryIndex lib(libraryDir());
        QVERIFY(makeLibrary(lib));

        LibraryAPI api(&lib, LibraryIndex::Type::Mod, "1.20.1");
        const auto version = versionOf(api, lib);

        // The install step copies this file rather than downloading it, which only works because
        // this is a path. A QNetworkRequest would have rejected it as an unknown protocol.
        QCOMPARE(version.downloadUrl, lib.absoluteFilePath(lib.entries().first()));
        QVERIFY(!version.downloadUrl.startsWith("http"));
    }

    void testEntriesOfAnotherTypeAreNotOffered()
    {
        LibraryIndex lib(libraryDir());
        QVERIFY(makeLibrary(lib));

        LibraryAPI mods(&lib, LibraryIndex::Type::Mod, "1.20.1");
        LibraryAPI shaders(&lib, LibraryIndex::Type::ShaderPack, "1.20.1");

        QCOMPARE(versionOf(mods, lib).fileName, QString("thing-1.0.0.jar"));
        QVERIFY(versionOf(shaders, lib).fileName.isEmpty());
    }
};

QTEST_GUILESS_MAIN(LibraryVersions)

#include "LibraryVersions_test.moc"