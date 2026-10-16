// SPDX-FileCopyrightText: 2026 Kerilom <89039577+Gleb-Lisovskiy@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include <QTest>

#include "minecraft/mod/ModConflict.h"

/* The scan reads what the mods say about themselves and nothing else, so it can be fed made-up
 * mods and asked what it makes of them. That is the whole of the feature's brain: the dialog only
 * puts the answers on screen. */
class ModConflictTest : public QObject {
    Q_OBJECT

   private:
    /** A mod as the parse tasks would have described it, with a file name to match its id. */
    static ModRecord makeMod(const QString& id, const QString& name = {})
    {
        ModRecord mod;
        mod.id = id;
        mod.name = name.isEmpty() ? id : name;
        mod.fileName = id + ".jar";
        return mod;
    }

    static const ModProblem* firstOf(const QList<ModProblem>& problems, ModProblem::Kind kind)
    {
        for (const auto& problem : problems) {
            if (problem.kind == kind) {
                return &problem;
            }
        }
        return nullptr;
    }

    static int countOf(const QList<ModProblem>& problems, ModProblem::Kind kind)
    {
        int count = 0;
        for (const auto& problem : problems) {
            count += problem.kind == kind ? 1 : 0;
        }
        return count;
    }

   private slots:
    void findsNothingInAHealthyPack()
    {
        auto mods = QList<ModRecord>{ makeMod("create"), makeMod("sodium") };
        mods[0].dependencies = { "sodium" };
        mods[1].loaders = { "fabric" };
        mods[1].gameVersions = { "1.20.1" };

        ModScanContext context;
        context.gameVersion = "1.20.1";
        context.loaders = { "fabric" };

        QVERIFY(ModConflicts::scan(mods, context).isEmpty());
    }

    void reportsAMissingDependency()
    {
        auto mods = QList<ModRecord>{ makeMod("create", "Create") };
        mods[0].dependencies = { "fabric-api" };

        const auto problems = ModConflicts::scan(mods, ModScanContext{});

        QCOMPARE(problems.size(), 1);
        QVERIFY(problems.first().kind == ModProblem::Kind::MissingDependency);
        QVERIFY(problems.first().severity == ModProblem::Severity::Error);
        QCOMPARE(problems.first().modName, QStringLiteral("Create"));
        QCOMPARE(problems.first().other, QStringLiteral("fabric-api"));
    }

    void reportsADependencyThatIsSwitchedOff()
    {
        auto mods = QList<ModRecord>{ makeMod("create", "Create"), makeMod("fabric-api", "Fabric API") };
        mods[0].dependencies = { "fabric-api" };
        mods[1].enabled = false;

        const auto problems = ModConflicts::scan(mods, ModScanContext{});

        QCOMPARE(problems.size(), 1);
        QVERIFY(problems.first().kind == ModProblem::Kind::DisabledDependency);
        QVERIFY(problems.first().severity == ModProblem::Severity::Warning);
        QCOMPARE(problems.first().note, QStringLiteral("fabric-api.jar"));
    }

    void saysNothingWhenTheDependencyIsThere()
    {
        auto mods = QList<ModRecord>{ makeMod("create"), makeMod("fabric-api") };
        mods[0].dependencies = { "fabric-api" };

        QVERIFY(ModConflicts::scan(mods, ModScanContext{}).isEmpty());
    }

    /** Forge mod ids are matched without regard to case, so "Optifine" finds "optifine". */
    void matchesADependencyWhateverItsCase()
    {
        auto mods = QList<ModRecord>{ makeMod("advancedshader", "AdvancedShader"), makeMod("optifine", "OptiFine") };
        mods[0].dependencies = { "Optifine" };

        QVERIFY(ModConflicts::scan(mods, ModScanContext{}).isEmpty());
    }

    void matchesAProvidedIdWhateverItsCase()
    {
        auto mods = QList<ModRecord>{ makeMod("advancedshader", "AdvancedShader"), makeMod("optifine", "OptiFine") };
        mods[0].dependencies = { "Optifine" };
        mods[1].provides = { "optifine" };
        mods[1].id = "of";

        QVERIFY(ModConflicts::scan(mods, ModScanContext{}).isEmpty());
    }

    void ignoresAModDependingOnItself()
    {
        auto mods = QList<ModRecord>{ makeMod("create") };
        mods[0].dependencies = { "create" };

        QVERIFY(ModConflicts::scan(mods, ModScanContext{}).isEmpty());
    }

    void reportsTwoJarsClaimingTheSameId()
    {
        auto mods = QList<ModRecord>{ makeMod("create", "Create"), makeMod("create", "Create (old)") };
        mods[0].fileName = "create-1.0.jar";
        mods[1].fileName = "create-0.9.jar";

        const auto problems = ModConflicts::scan(mods, ModScanContext{});

        QCOMPARE(problems.size(), 1);
        QVERIFY(problems.first().kind == ModProblem::Kind::DuplicateModId);
        QVERIFY(problems.first().severity == ModProblem::Severity::Error);
        QCOMPARE(problems.first().modId, QStringLiteral("create"));
        QCOMPARE(problems.first().note, QStringLiteral("create-1.0.jar, create-0.9.jar"));
    }

    void ignoresASecondJarThatIsSwitchedOff()
    {
        auto mods = QList<ModRecord>{ makeMod("create"), makeMod("create") };
        mods[1].enabled = false;

        QVERIFY(ModConflicts::scan(mods, ModScanContext{}).isEmpty());
    }

    void reportsAModThatCannotBeLoadedBesideAnother()
    {
        auto mods = QList<ModRecord>{ makeMod("optifine", "OptiFine"), makeMod("sodium", "Sodium") };
        mods[1].breaks = { "optifine" };

        const auto problems = ModConflicts::scan(mods, ModScanContext{});

        QCOMPARE(problems.size(), 1);
        QVERIFY(problems.first().kind == ModProblem::Kind::Incompatible);
        QVERIFY(problems.first().severity == ModProblem::Severity::Error);
        QCOMPARE(problems.first().modName, QStringLiteral("Sodium"));
        QCOMPARE(problems.first().other, QStringLiteral("OptiFine"));
        QCOMPARE(problems.first().note, QStringLiteral("optifine.jar"));
    }

    /** The same pair disliking each other from both sides is one problem, not two. */
    void reportsAMutualDislikeOnce()
    {
        auto mods = QList<ModRecord>{ makeMod("optifine", "OptiFine"), makeMod("sodium", "Sodium") };
        mods[0].breaks = { "sodium" };
        mods[1].breaks = { "optifine" };

        QCOMPARE(countOf(ModConflicts::scan(mods, ModScanContext{}), ModProblem::Kind::Incompatible), 1);
    }

    void ignoresABreakOnAModThatIsNotThere()
    {
        auto mods = QList<ModRecord>{ makeMod("sodium", "Sodium") };
        mods[0].breaks = { "optifine" };

        QVERIFY(ModConflicts::scan(mods, ModScanContext{}).isEmpty());
    }

    void reportsAJarBuiltForAnotherLoader()
    {
        auto mods = QList<ModRecord>{ makeMod("create", "Create") };
        mods[0].loaders = { "fabric" };

        ModScanContext context;
        context.loaders = { "neoforge" };

        const auto problems = ModConflicts::scan(mods, context);

        QCOMPARE(problems.size(), 1);
        QVERIFY(problems.first().kind == ModProblem::Kind::WrongLoader);
        QVERIFY(problems.first().severity == ModProblem::Severity::Warning);
        QCOMPARE(problems.first().note, QStringLiteral("fabric"));
        QCOMPARE(problems.first().other, QStringLiteral("neoforge"));
    }

    void leavesFabricModsAloneWhenConnectorIsInstalled()
    {
        auto mods = QList<ModRecord>{ makeMod("create", "Create"), makeMod("connectormod", "Connector") };
        mods[0].loaders = { "fabric" };

        ModScanContext context;
        context.loaders = { "neoforge" };

        QVERIFY(ModConflicts::scan(mods, context).isEmpty());
    }

    /** Older builds of Sinytra Connector used a shorter mod id. */
    void leavesFabricModsAloneWhenConnectorReportsTheOldId()
    {
        auto mods = QList<ModRecord>{ makeMod("create", "Create"), makeMod("connector", "Connector") };
        mods[0].loaders = { "fabric" };

        ModScanContext context;
        context.loaders = { "neoforge" };

        QVERIFY(ModConflicts::scan(mods, context).isEmpty());
    }

    /** Connector does not make a Forge mod at home in a Fabric instance. */
    void stillReportsThemWhenThereIsNoForgeToConnectTo()
    {
        auto mods = QList<ModRecord>{ makeMod("create", "Create"), makeMod("connectormod", "Connector") };
        mods[0].loaders = { "forge" };

        ModScanContext context;
        context.loaders = { "fabric" };

        QCOMPARE(countOf(ModConflicts::scan(mods, context), ModProblem::Kind::WrongLoader), 1);
    }

    /** A jar that ships no loader metadata at all is a plain one: it does not need Java, and it is
     *  not told off for having none. */
    void leavesAPlainJarAlone()
    {
        auto mods = QList<ModRecord>{ makeMod("plain", "Plain Jar") };
        mods[0].loaders = { "java" };

        ModScanContext context;
        context.loaders = { "neoforge" };

        QVERIFY(ModConflicts::scan(mods, context).isEmpty());
    }

    /** Quilt runs Fabric mods, so a Fabric jar is at home in a Quilt instance. */
    void acceptsFabricModsInAQuiltInstance()
    {
        auto mods = QList<ModRecord>{ makeMod("sodium", "Sodium") };
        mods[0].loaders = { "fabric" };

        ModScanContext context;
        context.loaders = { "quilt" };

        QVERIFY(ModConflicts::scan(mods, context).isEmpty());
    }

    /** A jar that says nothing about its loader is never called a mismatch, and neither is one
     *  built for a loader we have no opinion about. */
    void keepsQuietWhenTheLoaderIsUnknown()
    {
        auto mods = QList<ModRecord>{ makeMod("create"), makeMod("sodium"), makeMod("apple", "AppleSkin") };
        mods[0].loaders = { "fabric" };
        mods[1].loaders = { "liteloader" };
        mods[2].loaders = { "fabric", "liteloader" };

        ModScanContext context;
        context.loaders = { "neoforge" };

        QCOMPARE(countOf(ModConflicts::scan(mods, context), ModProblem::Kind::WrongLoader), 1);
        QCOMPARE(firstOf(ModConflicts::scan(mods, context), ModProblem::Kind::WrongLoader)->modId, QStringLiteral("create"));
    }

    void reportsAGameVersionTheModDoesNotCover()
    {
        auto mods = QList<ModRecord>{ makeMod("create", "Create"), makeMod("sodium", "Sodium") };
        mods[0].gameVersions = { "1.19.2" };
        mods[1].gameVersions = { "1.20.1" };

        ModScanContext context;
        context.gameVersion = "1.20.1";

        const auto problems = ModConflicts::scan(mods, context);

        QCOMPARE(problems.size(), 1);
        QVERIFY(problems.first().kind == ModProblem::Kind::WrongGameVersion);
        QCOMPARE(problems.first().modName, QStringLiteral("Create"));
        QCOMPARE(problems.first().note, QStringLiteral("1.19.2"));
    }

    /** A missing dependency will stop the game, a mod built for another loader may well not, and
     *  the list is meant to be read from the top. */
    void putsTheThingsThatStopTheGameFirst()
    {
        auto mods = QList<ModRecord>{ makeMod("create", "Create") };
        mods[0].dependencies = { "fabric-api" };
        mods[0].loaders = { "fabric" };

        ModScanContext context;
        context.loaders = { "neoforge" };

        const auto problems = ModConflicts::scan(mods, context);

        QCOMPARE(problems.size(), 2);
        QVERIFY(problems.at(0).severity == ModProblem::Severity::Error);
        QVERIFY(problems.at(1).severity == ModProblem::Severity::Warning);
    }
};

QTEST_GUILESS_MAIN(ModConflictTest)

#include "ModConflict_test.moc"
