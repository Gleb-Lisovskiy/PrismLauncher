// SPDX-FileCopyrightText: 2026 Kerilom <89039577+Gleb-Lisovskiy@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include "FileSystem.h"
#include "InstanceTemplate.h"

/* A template is a small JSON document, and the store around it is the only thing that writes the
 * file. What is worth testing is that a template survives a round trip, that a hand-edited file
 * cannot crash the launcher, and that the store's add/update/remove behave like the identity-based
 * operations the UI assumes they are. */
class InstanceTemplateTest : public QObject {
    Q_OBJECT

   private:
    QTemporaryDir m_dir;

    QString uniqueDir() const { return FS::PathCombine(m_dir.path(), QTest::currentTestFunction()); }
    QString storePath() const { return FS::PathCombine(uniqueDir(), "instance_templates.json"); }

    static InstanceTemplate sample()
    {
        InstanceTemplate tpl;
        tpl.id = QStringLiteral("a-template");
        tpl.name = QStringLiteral("My recipe");
        tpl.versionName = QStringLiteral("Forge on 1.20.1");
        tpl.description = QStringLiteral("A recipe with **notes**.");
        tpl.iconKey = QStringLiteral("default");
        tpl.createdAt = QDateTime::fromString(QStringLiteral("2026-01-02T03:04:05"), Qt::ISODate);
        tpl.components = { { QStringLiteral("net.minecraft"), QStringLiteral("1.20.1") },
                           { QStringLiteral("net.minecraftforge"), QStringLiteral("47.1.0") } };
        tpl.files = { { QStringLiteral("mods/sodium.jar"), {} }, { QStringLiteral("shaderpacks/complementary/iris.zip"), {} } };
        tpl.settings.insert(QStringLiteral("OverrideMemory"), true);
        tpl.settings.insert(QStringLiteral("MaxMemAlloc"), 4096);
        return tpl;
    }

    static bool writeFile(const QString& path, const QByteArray& contents)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return false;
        return file.write(contents) == contents.size();
    }

   private slots:
    void roundTripsThroughJson();
    void roundTripsFolders();
    void contentFilesPutsFoldersFirst();
    void omitsEmptyVersionName();
    void versionsOfGroupsByName();
    void migratesLegacyContentTypes();
    void fromJsonToleratesGarbage();
    void readsVersionAndLoader();
    void reportsValidity();
    void addAssignsIdentityAndPersists();
    void addReplacesSameId();
    void addCopiesFiles();
    void addStoresFoldersWithoutCopying();
    void updateChangesInPlace();
    void removeDeletes();
    void removeDeletesPayload();
    void loadOfMissingFileIsEmpty();
    void loadRejectsBrokenFile();
    void loadDropsEntriesWithoutId();
};

void InstanceTemplateTest::roundTripsThroughJson()
{
    const InstanceTemplate original = sample();
    const InstanceTemplate restored = InstanceTemplate::fromJson(original.toJson());

    QCOMPARE(restored.id, original.id);
    QCOMPARE(restored.name, original.name);
    QCOMPARE(restored.versionName, original.versionName);
    QCOMPARE(restored.description, original.description);
    QCOMPARE(restored.iconKey, original.iconKey);
    QCOMPARE(restored.createdAt, original.createdAt);
    QCOMPARE(restored.components.size(), original.components.size());
    QCOMPARE(restored.components.at(0).uid, QStringLiteral("net.minecraft"));
    QCOMPARE(restored.components.at(0).version, QStringLiteral("1.20.1"));
    QCOMPARE(restored.files.size(), original.files.size());
    QCOMPARE(restored.files.at(0).path, QStringLiteral("mods/sodium.jar"));
    // The source path is a save-time convenience and must not leak into the file.
    QVERIFY(restored.files.at(0).source.isEmpty());
    QCOMPARE(restored.files.at(1).path, QStringLiteral("shaderpacks/complementary/iris.zip"));
    QCOMPARE(restored.settings.value(QStringLiteral("OverrideMemory")).toBool(), true);
    QCOMPARE(restored.settings.value(QStringLiteral("MaxMemAlloc")).toInt(), 4096);

    // A source path set at capture time is dropped rather than written to the file.
    InstanceTemplate captured = original;
    captured.files[0].source = QStringLiteral("/somewhere/sodium.jar");
    QVERIFY(InstanceTemplate::fromJson(captured.toJson()).files.at(0).source.isEmpty());
}

void InstanceTemplateTest::roundTripsFolders()
{
    InstanceTemplate tpl;
    tpl.id = QStringLiteral("with-folders");
    tpl.name = QStringLiteral("Folders");
    tpl.components = { { QStringLiteral("net.minecraft"), QStringLiteral("1.20.1") } };
    // A folder keeps its flag, and a folder is allowed to sit at the top of the game directory.
    tpl.files = { { QStringLiteral("mods"), {}, true },
                  { QStringLiteral("mods/mappet"), {}, true },
                  { QStringLiteral("mods/mappet/friends.jar"), {}, false } };

    const InstanceTemplate restored = InstanceTemplate::fromJson(tpl.toJson());
    QCOMPARE(restored.files.size(), tpl.files.size());
    QVERIFY(restored.files.at(0).isDir);
    QCOMPARE(restored.files.at(0).path, QStringLiteral("mods"));
    QVERIFY(restored.files.at(1).isDir);
    QCOMPARE(restored.files.at(1).path, QStringLiteral("mods/mappet"));
    QVERIFY(!restored.files.at(2).isDir);
    QCOMPARE(restored.files.at(2).path, QStringLiteral("mods/mappet/friends.jar"));
}

void InstanceTemplateTest::contentFilesPutsFoldersFirst()
{
    const QString root = FS::PathCombine(uniqueDir(), "minecraft");
    // The names are chosen so that plain alphabetical order would interleave the folders and the
    // files: alpha.txt sorts before every folder, and the empty folder sorts last.
    QVERIFY(writeFile(FS::PathCombine(root, "alpha.txt"), QByteArrayLiteral("a")));
    QVERIFY(writeFile(FS::PathCombine(root, "beta", "inside.txt"), QByteArrayLiteral("b")));
    QVERIFY(writeFile(FS::PathCombine(root, "mods", "sodium.jar"), QByteArrayLiteral("c")));
    QVERIFY(QDir().mkpath(FS::PathCombine(root, "zeta")));
    // A leading dot marks a hidden entry on Unix and the launcher's lists leave hidden entries out,
    // so the scan does too. Whether this one actually counts as hidden is a platform question and
    // is settled below, because Windows hides by attribute rather than by a dotted name.
    const QString dotted = QStringLiteral(".hidden");
    QVERIFY(writeFile(FS::PathCombine(root, dotted), QByteArrayLiteral("h")));

    const auto files = InstanceTemplate::contentFiles(root);

    QStringList paths;
    for (const auto& file : files)
        paths << (file.isDir ? file.path + QLatin1Char('/') : file.path);

    if (!QFileInfo(FS::PathCombine(root, dotted)).isHidden())
        QVERIFY(paths.removeOne(dotted));  // Not hidden here, so it is an ordinary file the scan lists.

    // Folders first in name order, then files, with each folder followed by what is inside it.
    QCOMPARE(paths, QStringList({ QStringLiteral("beta/"), QStringLiteral("beta/inside.txt"), QStringLiteral("mods/"),
                                  QStringLiteral("mods/sodium.jar"), QStringLiteral("zeta/"), QStringLiteral("alpha.txt") }));

    // A folder carries no source to copy from; a file points at where it sits.
    QVERIFY(files.at(0).source.isEmpty());
    QCOMPARE(files.at(1).source, FS::PathCombine(root, "beta", "inside.txt"));
}

void InstanceTemplateTest::omitsEmptyVersionName()
{
    // A version label is optional, and a template without one must not grow a "version-name": ""
    // that a previous launcher would have to ignore.
    InstanceTemplate tpl = sample();
    tpl.versionName.clear();

    const QJsonObject json = tpl.toJson();
    QVERIFY(!json.contains(QStringLiteral("version-name")));
    QVERIFY(InstanceTemplate::fromJson(json).versionName.isEmpty());
}

void InstanceTemplateTest::versionsOfGroupsByName()
{
    InstanceTemplateStore store(storePath());

    InstanceTemplate first = sample();
    first.id = QStringLiteral("first");
    first.name = QStringLiteral("My recipe");
    first.versionName = QStringLiteral("Forge");
    store.add(first);

    InstanceTemplate second = sample();
    second.id = QStringLiteral("second");
    second.name = QStringLiteral("My recipe");
    second.versionName = QStringLiteral("Fabric");
    store.add(second);

    InstanceTemplate other = sample();
    other.id = QStringLiteral("other");
    other.name = QStringLiteral("Something else");
    store.add(other);

    // The name is what groups versions, so the two sharing one come back together and in order.
    const auto versions = store.versionsOf(QStringLiteral("My recipe"));
    QCOMPARE(versions.size(), 2);
    QCOMPARE(versions.at(0).id, QStringLiteral("first"));
    QCOMPARE(versions.at(1).id, QStringLiteral("second"));

    QCOMPARE(store.versionsOf(QStringLiteral("Something else")).size(), 1);
    QVERIFY(store.versionsOf(QStringLiteral("nothing here")).isEmpty());
}

void InstanceTemplateTest::migratesLegacyContentTypes()
{
    // Older templates named a content folder in "type" and kept the path relative to it. The path is
    // now relative to the game directory, so the old type becomes its first component.
    QJsonObject json;
    json.insert("files", QJsonArray{ QJsonObject{ { "type", "mods" }, { "path", "sodium.jar" } },
                                     QJsonObject{ { "type", "config" }, { "dir", true } } });

    const InstanceTemplate tpl = InstanceTemplate::fromJson(json);

    QCOMPARE(tpl.files.size(), 2);
    QCOMPARE(tpl.files.at(0).path, QStringLiteral("mods/sodium.jar"));
    QVERIFY(!tpl.files.at(0).isDir);
    QCOMPARE(tpl.files.at(1).path, QStringLiteral("config"));
    QVERIFY(tpl.files.at(1).isDir);
}

void InstanceTemplateTest::fromJsonToleratesGarbage()
{
    QJsonObject json;
    // Half-finished component entries, paths that name nothing, a stray value, and a missing icon
    // must all be survivable.
    json.insert("components", QJsonArray{ QJsonObject{ { "uid", "net.minecraft" } }, QJsonObject{ { "version", "1.20" } },
                                          QStringLiteral("not an object") });
    json.insert("files", QJsonArray{ QJsonObject{ { "path", "mods/keep.jar" } }, QJsonObject{ { "path", "config" }, { "dir", true } },
                                     QJsonObject{ { "path", "" } }, QJsonObject{ { "dir", true } }, QStringLiteral("junk") });
    json.insert("settings", QJsonValue::Null);

    const InstanceTemplate tpl = InstanceTemplate::fromJson(json);

    QCOMPARE(tpl.components.size(), 0);  // every entry was missing a half
    QCOMPARE(tpl.files.size(), 2);
    QCOMPARE(tpl.files.at(0).path, QStringLiteral("mods/keep.jar"));
    QVERIFY(!tpl.files.at(0).isDir);
    QCOMPARE(tpl.files.at(1).path, QStringLiteral("config"));
    QVERIFY(tpl.files.at(1).isDir);
    QVERIFY(tpl.settings.isEmpty());
    QVERIFY(tpl.description.isEmpty());
    QCOMPARE(tpl.iconKey, QStringLiteral("default"));
}

void InstanceTemplateTest::readsVersionAndLoader()
{
    const InstanceTemplate tpl = sample();

    QCOMPARE(tpl.minecraftVersion(), QStringLiteral("1.20.1"));
    QCOMPARE(tpl.loaderId(), QStringLiteral("net.minecraftforge"));
    QCOMPARE(tpl.loaderVersion(), QStringLiteral("47.1.0"));
    QCOMPARE(InstanceTemplate::loaderBrand(QStringLiteral("net.minecraftforge")), QStringLiteral("Forge"));
    QVERIFY(InstanceTemplate::loaderBrand(QStringLiteral("not.a.loader")).isEmpty());

    // A template with no loader reports none rather than guessing.
    InstanceTemplate vanilla;
    vanilla.components = { { QStringLiteral("net.minecraft"), QStringLiteral("1.20.1") } };
    QVERIFY(vanilla.loaderId().isEmpty());
    QVERIFY(vanilla.loaderVersion().isEmpty());
}

void InstanceTemplateTest::reportsValidity()
{
    InstanceTemplate tpl;
    QVERIFY(!tpl.isValid());

    tpl.name = QStringLiteral("Named");
    QVERIFY(!tpl.isValid());  // a name without components cannot create anything

    tpl.components = { { QStringLiteral("net.minecraft"), QStringLiteral("1.20.1") } };
    QVERIFY(tpl.isValid());

    tpl.name.clear();
    QVERIFY(!tpl.isValid());
}

void InstanceTemplateTest::addAssignsIdentityAndPersists()
{
    InstanceTemplateStore store(storePath());

    InstanceTemplate tpl = sample();
    tpl.id.clear();
    tpl.createdAt = QDateTime();

    const QString id = store.add(tpl);
    QVERIFY(!id.isEmpty());
    QCOMPARE(store.templates().size(), 1);

    const auto* stored = store.find(id);
    QVERIFY(stored);
    QVERIFY(stored->createdAt.isValid());  // the store stamps a time when there is none

    // A second store opened on the same file sees what the first one wrote.
    InstanceTemplateStore reopened(storePath());
    QString error;
    QVERIFY(reopened.load(&error));
    QVERIFY(error.isEmpty());
    QCOMPARE(reopened.templates().size(), 1);
    QCOMPARE(reopened.templates().first().name, tpl.name);
    QCOMPARE(reopened.templates().first().id, id);
}

void InstanceTemplateTest::addReplacesSameId()
{
    InstanceTemplateStore store(storePath());

    InstanceTemplate first = sample();
    first.id = QStringLiteral("fixed");
    store.add(first);

    InstanceTemplate second = first;
    second.name = QStringLiteral("Renamed");
    store.add(second);

    // The identifier is the identity, so re-adding it must not pile up a second template.
    QCOMPARE(store.templates().size(), 1);
    QCOMPARE(store.find(QStringLiteral("fixed"))->name, QStringLiteral("Renamed"));
}

void InstanceTemplateTest::addCopiesFiles()
{
    const QString source = FS::PathCombine(uniqueDir(), "instance", "sodium.jar");
    QVERIFY(writeFile(source, QByteArrayLiteral("mod bytes")));

    InstanceTemplateStore store(storePath());

    InstanceTemplate tpl = sample();
    tpl.id.clear();
    tpl.files = { { QStringLiteral("mods/sodium.jar"), source } };

    const QString id = store.add(tpl);
    QVERIFY(!id.isEmpty());

    const auto* stored = store.find(id);
    QVERIFY(stored);
    QCOMPARE(stored->files.size(), 1);
    // The source is only a save-time handle; once the payload is staged it is cleared.
    QVERIFY(stored->files.first().source.isEmpty());

    const QString copied = FS::PathCombine(store.payloadDir(id), "mods", "sodium.jar");
    QVERIFY(QFileInfo::exists(copied));
    QFile file(copied);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArrayLiteral("mod bytes"));
}

void InstanceTemplateTest::addStoresFoldersWithoutCopying()
{
    const QString source = FS::PathCombine(uniqueDir(), "instance", "sodium.jar");
    QVERIFY(writeFile(source, QByteArrayLiteral("mod bytes")));

    InstanceTemplateStore store(storePath());

    InstanceTemplate tpl = sample();
    tpl.id.clear();
    // The folder has no source: it is remembered, not copied. The file next to it still is.
    tpl.files = { { QStringLiteral("mods/mappet"), {}, true }, { QStringLiteral("mods/sodium.jar"), source, false } };

    const QString id = store.add(tpl);
    QVERIFY(!id.isEmpty());

    const auto* stored = store.find(id);
    QVERIFY(stored);
    QCOMPARE(stored->files.size(), 2);
    QVERIFY(stored->files.at(0).isDir);
    QVERIFY(!stored->files.at(1).isDir);

    // Nothing was copied for the folder, but the file landed next to where it would be.
    QVERIFY(!QFileInfo::exists(FS::PathCombine(store.payloadDir(id), "mods", "mappet")));
    QVERIFY(QFileInfo::exists(FS::PathCombine(store.payloadDir(id), "mods", "sodium.jar")));
}

void InstanceTemplateTest::updateChangesInPlace()
{
    InstanceTemplateStore store(storePath());

    InstanceTemplate tpl = sample();
    tpl.id = QStringLiteral("fixed");
    store.add(tpl);

    InstanceTemplate changed = tpl;
    changed.name = QStringLiteral("Changed");
    QVERIFY(store.update(changed));
    QCOMPARE(store.templates().size(), 1);
    QCOMPARE(store.find(QStringLiteral("fixed"))->name, QStringLiteral("Changed"));

    // Updating something that is not there is a no-op, not a silent insert.
    InstanceTemplate ghost = tpl;
    ghost.id = QStringLiteral("missing");
    QVERIFY(!store.update(ghost));
    QCOMPARE(store.templates().size(), 1);
}

void InstanceTemplateTest::removeDeletes()
{
    InstanceTemplateStore store(storePath());

    InstanceTemplate tpl = sample();
    tpl.id = QStringLiteral("fixed");
    store.add(tpl);

    QVERIFY(store.remove(QStringLiteral("fixed")));
    QVERIFY(store.empty());
    QVERIFY(!store.remove(QStringLiteral("fixed")));
}

void InstanceTemplateTest::removeDeletesPayload()
{
    const QString source = FS::PathCombine(uniqueDir(), "instance", "sodium.jar");
    QVERIFY(writeFile(source, QByteArrayLiteral("mod bytes")));

    InstanceTemplateStore store(storePath());

    InstanceTemplate tpl = sample();
    tpl.id = QStringLiteral("fixed");
    tpl.files = { { QStringLiteral("mods/sodium.jar"), source } };
    store.add(tpl);

    const QString dir = store.payloadDir(QStringLiteral("fixed"));
    QVERIFY(QFileInfo::exists(dir));

    QVERIFY(store.remove(QStringLiteral("fixed")));
    QVERIFY(!QFileInfo::exists(dir));
}

void InstanceTemplateTest::loadOfMissingFileIsEmpty()
{
    InstanceTemplateStore store(storePath());

    QVERIFY(store.load());
    QVERIFY(store.empty());
}

void InstanceTemplateTest::loadRejectsBrokenFile()
{
    QVERIFY(writeFile(storePath(), "{ this is not json"));

    InstanceTemplateStore store(storePath());
    QString error;
    QVERIFY(!store.load(&error));
    QVERIFY(!error.isEmpty());
    QVERIFY(store.empty());
}

void InstanceTemplateTest::loadDropsEntriesWithoutId()
{
    QVERIFY(writeFile(storePath(), R"({"format-version":1,"templates":[)"
                                   R"({"name":"no id, unusable"},)"
                                   R"({"id":"keep","name":"Kept","components":[{"uid":"net.minecraft","version":"1.20"}]})"
                                   R"(]})"));

    InstanceTemplateStore store(storePath());
    QVERIFY(store.load());
    QCOMPARE(store.templates().size(), 1);
    QCOMPARE(store.templates().first().name, QStringLiteral("Kept"));
    QVERIFY(store.find(QStringLiteral("keep")));
}

QTEST_GUILESS_MAIN(InstanceTemplateTest)

#include "InstanceTemplate_test.moc"
