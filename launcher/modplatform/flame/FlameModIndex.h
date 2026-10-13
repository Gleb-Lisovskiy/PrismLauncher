//
// Created by timoreo on 16/01/2022.
//

#pragma once

#include "modplatform/ModIndex.h"

#include "BaseInstance.h"

namespace FlameMod {

void loadIndexedPack(ModPlatform::IndexedPack& m, QJsonObject& obj);
void loadURLs(ModPlatform::IndexedPack& m, QJsonObject& obj);
void loadBody(ModPlatform::IndexedPack& m);
void loadIndexedPackVersions(ModPlatform::IndexedPack& pack, QJsonArray& arr);
ModPlatform::IndexedVersion loadIndexedPackVersion(QJsonObject& obj, bool load_changelog = false);

/** Builds the forgecdn URL for a file from its id and name.
 *
 * CurseForge omits downloadUrl from API responses for projects that do not allow downloads
 * through third-party launchers, but the CDN layout is deterministic:
 * https://edge.forgecdn.net/files/<first 4 digits of the id>/<rest>/<file name>
 * Returns an empty string if the inputs cannot produce a usable URL. */
QString guessDownloadUrl(const QString& fileId, const QString& fileName);
}  // namespace FlameMod
