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
        tpl.iconKey = QStringLiteral("default");
        tpl.createdAt = QDateTime::fromString(QStringLiteral("2026-01-02T03:04:05"), Qt::ISODate);
        tpl.components = { { QStringLiteral("net.minecraft"), QStringLiteral("1.20.1") },
                           { QStringLiteral("net.minecraftforge"), QStringLiteral("47.1.0") } };
        tpl.libraryEntries = { QStringLiteral("sodium@1"), QStringLiteral("iris@2") };
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
    void fromJsonToleratesGarbage();
    void readsVersionAndLoader();
    void reportsValidity();
    void addAssignsIdentityAndPersists();
    void addReplacesSameId();
    void updateChangesInPlace();
    void removeDeletes();
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
    QCOMPARE(restored.iconKey, original.iconKey);
    QCOMPARE(restored.createdAt, original.createdAt);
    QCOMPARE(restored.components.size(), original.components.size());
    QCOMPARE(restored.components.at(0).uid, QStringLiteral("net.minecraft"));
    QCOMPARE(restored.components.at(0).version, QStringLiteral("1.20.1"));
    QCOMPARE(restored.libraryEntries, original.libraryEntries);
    QCOMPARE(restored.settings.value(QStringLiteral("OverrideMemory")).toBool(), true);
    QCOMPARE(restored.settings.value(QStringLiteral("MaxMemAlloc")).toInt(), 4096);
}

void InstanceTemplateTest::fromJsonToleratesGarbage()
{
    QJsonObject json;
    // Half-finished component entries, a stray value, and a missing icon must all be survivable.
    json.insert("components", QJsonArray{ QJsonObject{ { "uid", "net.minecraft" } }, QJsonObject{ { "version", "1.20" } },
                                          QStringLiteral("not an object") });
    json.insert("library", QJsonArray{ QStringLiteral("keep"), QString(), 5 });
    json.insert("settings", QJsonValue::Null);

    const InstanceTemplate tpl = InstanceTemplate::fromJson(json);

    QCOMPARE(tpl.components.size(), 0);  // every entry was missing a half
    QCOMPARE(tpl.libraryEntries, QStringList{ QStringLiteral("keep") });
    QVERIFY(tpl.settings.isEmpty());
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
