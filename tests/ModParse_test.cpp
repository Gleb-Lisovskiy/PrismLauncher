// SPDX-FileCopyrightText: 2026 Kerilom <89039577+Gleb-Lisovskiy@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include <QTest>

#include "minecraft/mod/tasks/LocalModParseTask.h"

/* A jar's own metadata is where the game version it was built for lives, and the conflict check
 * reads it from there. Each loader writes that down differently, so each reader is asked here what
 * it makes of the shape that loader uses. */
class ModParseTest : public QObject {
    Q_OBJECT

   private slots:
    /** This is the shape a mod built for 1.20.1 carries when it is dropped into a 1.21.1 instance;
     *  "minecraft" must be read as the game version, not as a mod that has to be installed. */
    void readsTheGameVersionFabricAsksFor()
    {
        const auto details = ModUtils::ReadFabricModInfo(R"json({
            "schemaVersion": 1,
            "id": "mappet",
            "version": "1.0.0",
            "depends": {
                "fabricloader": ">=0.16.14",
                "minecraft": "~1.20.1",
                "java": ">=17",
                "fabric-api": "*",
                "blockbuster": ">=2.7.2-1.20.1"
            }
        })json");

        QCOMPARE(details.game_versions.size(), 1);
        QCOMPARE(details.game_versions.value(0), QStringLiteral("~1.20.1"));
        QCOMPARE(details.dependencies.size(), 1);
        QCOMPARE(details.dependencies.value(0), QStringLiteral("blockbuster"));
    }

    void readsAGameVersionFabricGivesAsAList()
    {
        const auto details = ModUtils::ReadFabricModInfo(R"json({
            "schemaVersion": 1,
            "id": "create",
            "version": "1.0.0",
            "depends": { "minecraft": [">=1.20.1", "<1.21"] }
        })json");

        QCOMPARE(details.game_versions.size(), 2);
        QCOMPARE(details.game_versions.value(0), QStringLiteral(">=1.20.1"));
        QCOMPARE(details.game_versions.value(1), QStringLiteral("<1.21"));
    }

    void readsTheGameVersionForgeAsksFor()
    {
        // Written out by hand rather than as a raw string: moc trips over ")" inside one.
        const QByteArray contents =
            "modLoader = \"javafml\"\n"
            "loaderVersion = \"[47,)\"\n"
            "license = \"MIT\"\n"
            "\n"
            "[[mods]]\n"
            "modId = \"create\"\n"
            "version = \"1.0.0\"\n"
            "\n"
            "[[dependencies.create]]\n"
            "modId = \"minecraft\"\n"
            "mandatory = true\n"
            "versionRange = \"[1.20.1,1.21)\"\n"
            "\n"
            "[[dependencies.create]]\n"
            "modId = \"forge\"\n"
            "mandatory = true\n"
            "versionRange = \"[47,)\"\n";

        const auto details = ModUtils::ReadMCModTOML(contents);

        QCOMPARE(details.game_versions.size(), 1);
        QCOMPARE(details.game_versions.value(0), QStringLiteral("[1.20.1,1.21)"));
        QVERIFY(details.dependencies.isEmpty());
    }

    void readsTheGameVersionFromMcmodInfo()
    {
        const auto details = ModUtils::ReadMCModInfo(R"json([{
            "modid": "mappet",
            "name": "Mappet",
            "version": "1.0.0",
            "mcversion": "1.12.2"
        }])json");

        QCOMPARE(details.game_versions.size(), 1);
        QCOMPARE(details.game_versions.value(0), QStringLiteral("1.12.2"));
    }

    /** A mod built from unprocessed sources leaves the placeholder in, and it is not a version. */
    void ignoresAGameVersionThatIsStillAPlaceholder()
    {
        const auto details = ModUtils::ReadMCModInfo(R"json([{
            "modid": "mappet",
            "version": "1.0.0",
            "mcversion": "${mcversion}"
        }])json");

        QVERIFY(details.game_versions.isEmpty());
    }
};

QTEST_GUILESS_MAIN(ModParseTest)

#include "ModParse_test.moc"