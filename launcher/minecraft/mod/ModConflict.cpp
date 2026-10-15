// SPDX-FileCopyrightText: 2026 Kerilom <89039577+Gleb-Lisovskiy@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "ModConflict.h"

#include <algorithm>

#include <QHash>
#include <QSet>

#include "Mod.h"

namespace {

const QSet<QString>& knownLoaders()
{
    static const QSet<QString> loaders = { "fabric", "quilt", "forge", "neoforge", "java", "minecraft" };
    return loaders;
}

/** Whether an instance running one loader can load a jar built for another.
 *
 *  Quilt runs Fabric mods, though Fabric does not run Quilt's own; NeoForge is a fork of Forge, so
 *  a Forge mod is not called a mismatch there either. */
bool accepts(const QString& instanceLoader, const QString& modLoader)
{
    if (instanceLoader == modLoader)
        return true;
    if (instanceLoader == "quilt" && modLoader == "fabric")
        return true;
    if (instanceLoader == "neoforge" && modLoader == "forge")
        return true;

    // Generic mods don't belong to a specific loader
    if (modLoader == "java" || modLoader == "minecraft")
        return true;
    return false;
}

/** Whether every loader named is one we know how to reason about. LiteLoader, a datapack and the
 *  rest are left alone rather than guessed at. */
bool allKnown(const QStringList& loaders)
{
    return !loaders.isEmpty() &&
           std::all_of(loaders.begin(), loaders.end(), [](const QString& loader) { return knownLoaders().contains(loader); });
}

QString labelOf(const ModRecord& mod)
{
    return mod.name.isEmpty() ? mod.id : mod.name;
}

/** Identifies a pair of jars regardless of which side of the pair we are looking from. */
QString pairKey(const QString& left, const QString& right)
{
    return left < right ? left + '\n' + right : right + '\n' + left;
}

}  // namespace

ModRecord ModConflicts::recordFrom(const Mod& mod)
{
    ModRecord record;
    record.id = mod.mod_id();
    record.name = mod.name();
    record.fileName = mod.fileinfo().fileName();
    record.dependencies = mod.dependencies();
    record.breaks = mod.details().breaks;
    record.provides = mod.details().provides;
    record.loaders = mod.details().local_loaders;
    record.enabled = mod.enabled();

    if (auto metadata = mod.metadata()) {
        // Where the mod came from usually describes it in more detail than the jar does, and it is
        // the description the mod list already shows.
        for (auto loader : ModPlatform::modLoaderTypesToList(metadata->loaders)) {
            auto name = ModPlatform::getModLoaderAsString(loader);
            if (!name.isEmpty() && !record.loaders.contains(name))
                record.loaders.append(name);
        }
        record.gameVersions = metadata->mcVersions;
    }

    return record;
}

QStringList ModConflicts::loaderNames(ModPlatform::ModLoaderTypes loaders)
{
    QStringList names;
    for (auto loader : ModPlatform::modLoaderTypesToList(loaders)) {
        auto name = ModPlatform::getModLoaderAsString(loader);
        if (!name.isEmpty())
            names.append(name);
    }
    return names;
}

QList<ModProblem> ModConflicts::scan(const QList<ModRecord>& mods, const ModScanContext& context)
{
    QList<ModProblem> problems;

    QHash<QString, QList<const ModRecord*>> byId;
    for (const auto& mod : mods) {
        if (!mod.id.isEmpty())
            byId[mod.id].append(&mod);
    }

    auto disabledWithId = [&byId](const QString& id) -> const ModRecord* {
        for (const auto* mod : byId.value(id)) {
            if (!mod->enabled)
                return mod;
        }
        return nullptr;
    };

    auto enabledWithId = [&byId](const QString& id) -> const ModRecord* {
        for (const auto* mod : byId.value(id)) {
            if (mod->enabled)
                return mod;
        }
        return nullptr;
    };

    QHash<QString, QList<const ModRecord*>> byProvided;
    for (const auto& mod : mods) {
        if (!mod.enabled)
            continue;
        for (const auto& provided : mod.provides) {
            if (!provided.isEmpty())
                byProvided[provided].append(&mod);
        }
    }

    auto enabledProviding = [&byProvided](const QString& id) -> const ModRecord* {
        for (const auto* mod : byProvided.value(id)) {
            if (mod->enabled)
                return mod;
        }
        return nullptr;
    };

    auto disabledProviding = [&byProvided](const QString& id) -> const ModRecord* {
        for (const auto* mod : byProvided.value(id)) {
            if (!mod->enabled)
                return mod;
        }
        return nullptr;
    };

    auto satisfies = [&enabledWithId, &enabledProviding](const QString& id) -> bool {
        if (enabledWithId(id))
            return true;
        if (enabledProviding(id))
            return true;
        return false;
    };

    auto satisfiesDisabled = [&enabledWithId, &enabledProviding, &disabledWithId, &disabledProviding](const QString& id) -> const ModRecord* {
        if (const auto* d = disabledWithId(id)) return d;
        if (const auto* d = disabledProviding(id)) return d;
        return nullptr;
    };

    // Connector is precisely the thing that lets Fabric mods be loaded by a Forge-family instance,
    // so its presence changes what counts as a loader mismatch.
    const bool connector = enabledWithId("connector") != nullptr;
    const bool contextIsForgeFamily = std::any_of(context.loaders.begin(), context.loaders.end(),
                                                  [](const QString& loader) { return loader == "forge" || loader == "neoforge"; });

    // Two jars claiming the same id: the loader will pick one of them and drop the other, and
    // which one it picks is not up to us.
    for (auto it = byId.constBegin(); it != byId.constEnd(); ++it) {
        QStringList jars;
        const ModRecord* first = nullptr;
        for (const auto* mod : it.value()) {
            if (!mod->enabled)
                continue;
            jars.append(mod->fileName);
            if (!first)
                first = mod;
        }
        if (jars.size() < 2)
            continue;

        ModProblem problem;
        problem.kind = ModProblem::Kind::DuplicateModId;
        problem.severity = ModProblem::Severity::Error;
        problem.modName = labelOf(*first);
        problem.modId = it.key();
        problem.fileName = first->fileName;
        problem.note = jars.join(", ");
        problems.append(problem);
    }

    for (const auto& mod : mods) {
        if (!mod.enabled)
            continue;

        QSet<QString> seen;
        for (const auto& dependency : mod.dependencies) {
            if (dependency.isEmpty() || dependency == mod.id || seen.contains(dependency))
                continue;
            seen.insert(dependency);

            if (satisfies(dependency))
                continue;  // the mod is there and switched on, which is all this can tell

            ModProblem problem;
            problem.modName = labelOf(mod);
            problem.modId = mod.id;
            problem.fileName = mod.fileName;
            problem.other = dependency;

            if (const auto* disabled = satisfiesDisabled(dependency)) {
                problem.kind = ModProblem::Kind::DisabledDependency;
                problem.severity = ModProblem::Severity::Warning;
                problem.note = disabled->fileName;
            } else {
                problem.kind = ModProblem::Kind::MissingDependency;
                problem.severity = ModProblem::Severity::Error;
            }

            problems.append(problem);
        }
    }

    QSet<QString> reportedPairs;
    for (const auto& mod : mods) {
        if (!mod.enabled)
            continue;

        for (const auto& forbidden : mod.breaks) {
            if (forbidden.isEmpty() || forbidden == mod.id)
                continue;

            const auto* other = enabledWithId(forbidden);
            if (!other)
                continue;

            const auto pair = pairKey(mod.fileName, other->fileName);
            if (reportedPairs.contains(pair))
                continue;
            reportedPairs.insert(pair);

            ModProblem problem;
            problem.kind = ModProblem::Kind::Incompatible;
            problem.severity = ModProblem::Severity::Error;
            problem.modName = labelOf(mod);
            problem.modId = mod.id;
            problem.fileName = mod.fileName;
            problem.other = labelOf(*other);
            problem.note = other->fileName;
            problems.append(problem);
        }
    }

    if (allKnown(context.loaders)) {
        for (const auto& mod : mods) {
            if (!mod.enabled || !allKnown(mod.loaders))
                continue;

            bool loaded = false;
            for (const auto& instanceLoader : context.loaders) {
                for (const auto& modLoader : mod.loaders) {
                    if (accepts(instanceLoader, modLoader)) {
                        loaded = true;
                        break;
                    }
                }
                if (loaded)
                    break;
            }

            // Connector is the thing that hands Fabric's own loader to a Forge-family instance
            const bool fabricFacing = connector && contextIsForgeFamily &&
                                      std::all_of(mod.loaders.begin(), mod.loaders.end(),
                                                  [](const QString& loader) { return loader == "fabric" || loader == "quilt"; });
            if (loaded || fabricFacing)
                continue;

            ModProblem problem;
            problem.kind = ModProblem::Kind::WrongLoader;
            problem.severity = ModProblem::Severity::Warning;
            problem.modName = labelOf(mod);
            problem.modId = mod.id;
            problem.fileName = mod.fileName;
            problem.other = context.loaders.join(", ");
            problem.note = mod.loaders.join(", ");
            problems.append(problem);
        }
    }

    if (!context.gameVersion.isEmpty()) {
        for (const auto& mod : mods) {
            if (!mod.enabled || mod.gameVersions.isEmpty() || mod.gameVersions.contains(context.gameVersion))
                continue;

            ModProblem problem;
            problem.kind = ModProblem::Kind::WrongGameVersion;
            problem.severity = ModProblem::Severity::Warning;
            problem.modName = labelOf(mod);
            problem.modId = mod.id;
            problem.fileName = mod.fileName;
            problem.other = context.gameVersion;
            problem.note = mod.gameVersions.join(", ");
            problems.append(problem);
        }
    }

    // The things that will stop the game come first; within a severity the order they were found
    // in is kept.
    std::stable_sort(problems.begin(), problems.end(),
                     [](const ModProblem& left, const ModProblem& right) { return left.severity > right.severity; });

    return problems;
}
