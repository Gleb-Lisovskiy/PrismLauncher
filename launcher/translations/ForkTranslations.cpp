// SPDX-FileCopyrightText: 2026 Kerilom <89039577+Gleb-Lisovskiy@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "ForkTranslations.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include "Application.h"
#include "FileSystem.h"
#include "POTranslator.h"

ForkTranslations::ForkTranslations() = default;
ForkTranslations::~ForkTranslations() = default;

QStringList ForkTranslations::searchPaths() const
{
    return { FS::PathCombine(QCoreApplication::applicationDirPath(), "translations"),
             FS::PathCombine(QCoreApplication::applicationDirPath(), "share", "translations"),
             FS::PathCombine(APPLICATION->dataRoot(), "translations") };
}

void ForkTranslations::load(const QString& language)
{
    // Whatever was loaded before goes first, so switching back and forth does not stack translators.
    m_translator.reset();

    if (language.isEmpty() || language == "en") {
        return;  // English is the source language; there is nothing to translate from.
    }

    for (const auto& dir : searchPaths()) {
        const auto path = FS::PathCombine(dir, "fork_" + language + ".po");
        if (!QFileInfo::exists(path)) {
            continue;
        }

        auto translator = std::make_unique<POTranslator>(path);
        if (translator->isEmpty()) {
            qWarning() << "Fork translation catalog is unreadable:" << path;
            continue;
        }

        // Installed after the server's translator, so it is consulted first.
        if (!QCoreApplication::installTranslator(translator.get())) {
            qWarning() << "Failed to install fork translation catalog:" << path;
            continue;
        }

        qDebug() << "Loaded fork translation catalog for" << language << "from" << path;
        m_translator = std::move(translator);
        return;
    }

    qDebug() << "No fork translation catalog for" << language;
}