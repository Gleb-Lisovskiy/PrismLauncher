// SPDX-FileCopyrightText: 2026 Kerilom <89039577+Gleb-Lisovskiy@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "ModConflict.h"

#include <algorithm>

#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include "Mod.h"
#include "Version.h"

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

/** Compares two Minecraft-like versions, using the same ordering the launcher uses for versions. */
int compareVersions(const QString& left, const QString& right)
{
    const auto order = Version(left) <=> Version(right);
    return order < 0 ? -1 : (order > 0 ? 1 : 0);
}

QStringList versionParts(const QString& version)
{
    return version.split('.', Qt::SkipEmptyParts);
}

/** Whether a plain version covers the one the instance runs. A part the metadata spells out has to
 *  match, a wildcard part ("x" or "*") stands for anything, and a version the metadata cuts short
 *  ("1.20" for "1.20.1") is read as the start of that line. */
bool plainVersionCovers(const QString& wanted, const QString& gameVersion)
{
    const auto wantedParts = versionParts(wanted);
    const auto actualParts = versionParts(gameVersion);
    if (wantedParts.isEmpty())
        return true;

    for (int i = 0; i < wantedParts.size(); ++i) {
        const auto part = wantedParts.at(i).trimmed().toLower();
        if (part.isEmpty() || part == "x" || part == "*")
            continue;
        if (i >= actualParts.size() || part != actualParts.at(i).trimmed().toLower())
            return false;
    }
    return true;
}

/** The first version that is past the line a constraint starting at \a version covers, for the
 *  part at \a index: "1.20.1" with index 1 becomes "1.21.0". */
QString versionPast(const QString& version, int index)
{
    auto parts = versionParts(version);
    if (index >= parts.size())
        return {};

    bool ok = false;
    const auto number = parts.at(index).toInt(&ok);
    if (!ok)
        return {};

    parts[index] = QString::number(number + 1);
    for (int i = index + 1; i < parts.size(); ++i)
        parts[i] = "0";
    return parts.join('.');
}

/** Whether one version the metadata wrote down covers the one the instance runs.
 *
 *  Every loader spells this out differently: a plain version ("1.20.1"), a wildcard ("1.20.x"), a
 *  Maven range ("[1.20.1,1.21)"), a tilde or caret constraint ("~1.20.1"), or a set of comparisons
 *  (">=1.20.1 <1.21"). Anything that cannot be read as one of those is taken to cover the version,
 *  because refusing to answer is not the same as answering "no". */
bool coversGameVersion(const QString& expression, const QString& gameVersion)
{
    const auto trimmed = expression.simplified();
    if (trimmed.isEmpty())
        return true;

    // Fabric spells "either of these" with "||".
    const auto alternatives = trimmed.split(QLatin1String("||"), Qt::SkipEmptyParts);
    if (alternatives.size() > 1) {
        return std::any_of(alternatives.begin(), alternatives.end(),
                           [&gameVersion](const QString& alternative) { return coversGameVersion(alternative, gameVersion); });
    }

    // Maven ranges: "[1.20.1,1.21)" holds 1.20.1 but not 1.21.
    const bool opensRange = trimmed.startsWith('[') || trimmed.startsWith('(');
    const bool closesRange = trimmed.endsWith(']') || trimmed.endsWith(')');
    if (opensRange && closesRange && trimmed.size() > 2) {
        const auto bounds = trimmed.mid(1, trimmed.size() - 2).split(',');
        const auto lower = bounds.value(0).trimmed();
        const auto upper = bounds.value(1).trimmed();

        if (!lower.isEmpty()) {
            const int order = compareVersions(gameVersion, lower);
            if (order < 0 || (order == 0 && !trimmed.startsWith('[')))
                return false;
        }
        // A bound with a wildcard in it cannot be ordered, so only the lower one is asked about.
        const bool upperIsWild = upper.contains('x') || upper.contains('X') || upper.contains('*');
        if (!upper.isEmpty() && !upperIsWild) {
            const int order = compareVersions(gameVersion, upper);
            if (order > 0 || (order == 0 && !trimmed.endsWith(']')))
                return false;
        }
        return true;
    }

    // Comparisons and plain versions can be listed together, separated by spaces or commas.
    // ">=1.20.1 <1.21" and "1.20.x" are the same idea written differently.
    const auto tokens = trimmed.split(QRegularExpression("[\\s,]+"), Qt::SkipEmptyParts);
    return std::all_of(tokens.begin(), tokens.end(), [&gameVersion](const QString& token) {
        // Longest operator first, so ">=" is not read as ">".
        static const QStringList operators = { ">=", "<=", ">", "<", "=", "~", "^" };
        for (const auto& op : operators) {
            if (!token.startsWith(op))
                continue;

            const auto version = token.mid(op.size()).trimmed();
            if (version.isEmpty())
                return true;
            if (op == ">=")
                return compareVersions(gameVersion, version) >= 0;
            if (op == "<=")
                return compareVersions(gameVersion, version) <= 0;
            if (op == ">")
                return compareVersions(gameVersion, version) > 0;
            if (op == "<")
                return compareVersions(gameVersion, version) < 0;
            if (op == "=")
                return compareVersions(gameVersion, version) == 0;

            // "~1.20.1" covers the 1.20 line and "^1.20.1" everything up to the next major
            // version, the way npm reads them.
            const auto parts = versionParts(version);
            const auto ceiling = versionPast(version, (op == "~" && parts.size() > 1) ? 1 : 0);
            if (ceiling.isEmpty())
                return true;
            if (compareVersions(gameVersion, version) < 0)
                return false;
            return compareVersions(gameVersion, ceiling) < 0;
        }

        return plainVersionCovers(token, gameVersion);
    });
}

/** Whether any of the versions a mod declares covers the one the instance runs. */
bool anyCoversGameVersion(const QStringList& versions, const QString& gameVersion)
{
    return std::any_of(versions.begin(), versions.end(),
                       [&gameVersion](const QString& version) { return coversGameVersion(version, gameVersion); });
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

    // A jar that was not downloaded from a platform still knows which versions it was built for,
    // so fall back on what its own metadata says.
    if (record.gameVersions.isEmpty())
        record.gameVersions = mod.details().game_versions;

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

    // Mod ids are matched the way the game matches them: without regard to case. A dependency on
    // "Optifine" is satisfied by a mod whose id is "optifine".
    QHash<QString, QList<const ModRecord*>> byId;
    for (const auto& mod : mods) {
        if (!mod.id.isEmpty())
            byId[mod.id.toLower()].append(&mod);
    }

    auto disabledWithId = [&byId](const QString& id) -> const ModRecord* {
        for (const auto* mod : byId.value(id.toLower())) {
            if (!mod->enabled)
                return mod;
        }
        return nullptr;
    };

    auto enabledWithId = [&byId](const QString& id) -> const ModRecord* {
        for (const auto* mod : byId.value(id.toLower())) {
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
                byProvided[provided.toLower()].append(&mod);
        }
    }

    auto enabledProviding = [&byProvided](const QString& id) -> const ModRecord* {
        for (const auto* mod : byProvided.value(id.toLower())) {
            if (mod->enabled)
                return mod;
        }
        return nullptr;
    };

    auto disabledProviding = [&byProvided](const QString& id) -> const ModRecord* {
        for (const auto* mod : byProvided.value(id.toLower())) {
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

    auto satisfiesDisabled = [&disabledWithId, &disabledProviding](const QString& id) -> const ModRecord* {
        if (const auto* d = disabledWithId(id)) return d;
        if (const auto* d = disabledProviding(id)) return d;
        return nullptr;
    };

    // Connector is precisely the thing that lets Fabric mods be loaded by a Forge-family instance,
    // so its presence changes what counts as a loader mismatch. Sinytra Connector calls itself
    // "connectormod" in its metadata; the shorter "connector" is kept for older builds.
    static const QStringList connectorIds = { "connectormod", "connector" };
    const bool connector = std::any_of(connectorIds.begin(), connectorIds.end(),
                                       [&](const QString& id) { return enabledWithId(id) != nullptr; });
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
        problem.modId = first->id;
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
            if (!mod.enabled || mod.gameVersions.isEmpty() || anyCoversGameVersion(mod.gameVersions, context.gameVersion))
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
