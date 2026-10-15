// SPDX-FileCopyrightText: 2026 Kerilom <89039577+Gleb-Lisovskiy@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include "modplatform/ModIndex.h"

class Mod;

/** Something about a set of installed mods that an instance will not be happy with, or that the
 *  launcher has reason to doubt.
 *
 *  The kinds are the ones a modpack builder keeps running into: a mod that needs something that is
 *  not there, the same mod shipped twice, a mod that says outright that it cannot be loaded beside
 *  another one, and metadata that does not match the instance the mod was put into. */
struct ModProblem {
    enum class Kind {
        MissingDependency,   // a mod needs another one which is not installed at all
        DisabledDependency,  // ...which is installed, but switched off
        DuplicateModId,      // two enabled jars claim the same mod id
        Incompatible,        // a mod says it cannot be loaded beside another one
        WrongLoader,         // a jar is built for a loader the instance does not run
        WrongGameVersion,    // a jar is not marked as working with the instance's game version
    };

    enum class Severity {
        Warning,  // the game will start, and may well be fine
        Error,    // the game will refuse to load, or the mod will not be there
    };

    Kind kind = Kind::MissingDependency;
    Severity severity = Severity::Warning;

    /** The mod the problem is about, as the mod list calls it. */
    QString modName;
    /** Its mod id, or the file name when its metadata gave no id. */
    QString modId;
    /** The jar this came from, relative to the mods folder. */
    QString fileName;
    /** The other side of the problem: the missing dependency, the competing jar, the mod this one
     *  cannot be loaded beside. Empty when there is no other side. */
    QString other;
    /** Anything else worth reading, such as the versions or loaders involved. Empty when there is
     *  nothing more to say. */
    QString note;
};

/** What a scan needs to know about one installed mod.
 *
 *  Filled in from a Mod, but kept apart from it so that the scan needs no model, no instance and
 *  no running application. */
struct ModRecord {
    QString id;
    QString name;
    QString fileName;
    /** Mod IDs this mod needs. */
    QStringList dependencies;
    /** Mod IDs this mod cannot be loaded beside. */
    QStringList breaks;
    /** Mod IDs and aliases this mod stands in for. */
    QStringList provides;
    /** The loaders the jar is built for, in lowercase. Empty when unknown, in which case nothing
     *  is assumed about it. */
    QStringList loaders;
    /** The game versions its metadata covers. Empty when unknown. */
    QStringList gameVersions;
    bool enabled = true;
};

/** The instance the mods are installed into. */
struct ModScanContext {
    /** The instance's game version. Empty when it could not be worked out. */
    QString gameVersion;
    /** The loaders the instance runs, in lowercase. Empty when unknown. */
    QStringList loaders;
};

namespace ModConflicts {
/** Reads one installed mod: what its own metadata says about it, plus what the launcher knows
 *  about where it came from. */
ModRecord recordFrom(const Mod& mod);

/** Turns the loaders an instance runs into the names used by ModRecord. */
QStringList loaderNames(ModPlatform::ModLoaderTypes loaders);

/** Everything wrong with a set of mods.
 *
 *  Only what the metadata proves is reported: a mod that says nothing about its loader is never
 *  called a mismatch, and a pack whose mods carry no metadata at all comes back clean rather than
 *  full of guesses. */
QList<ModProblem> scan(const QList<ModRecord>& mods, const ModScanContext& context);
}  // namespace ModConflicts
