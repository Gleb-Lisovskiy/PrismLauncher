// SPDX-FileCopyrightText: 2026 Kerilom <89039577+Gleb-Lisovskiy@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include <QTest>

#include "translations/POTranslator.h"

/* The fork's strings are catalogued separately and installed on top of the ones downloaded from the
 * translation server. POTranslator answers an empty string for an entry that has no text, which
 * Qt takes at face value and shows as a blank widget, so a catalog with holes in it is worse than
 * no catalog at all. These read the real shipped file rather than a fixture, because a catalog that
 * parses but whose contexts have drifted from the code fails silently at runtime instead. */
class ForkTranslationsCatalog : public QObject {
    Q_OBJECT

   private:
    /** Locates the catalog the way the build lays it out: next to the launcher executable. */
    static QString catalogPath() { return QDir(QCoreApplication::applicationDirPath()).filePath("translations/fork_ru.po"); }

   private slots:
    void initTestCase()
    {
        QVERIFY2(QFileInfo::exists(catalogPath()), qPrintable("No fork catalog next to the test binary: " + catalogPath()));
    }

    void testCatalogParses()
    {
        POTranslator translator(catalogPath());
        QVERIFY(!translator.isEmpty());
    }

    void testEntriesAreTranslated()
    {
        POTranslator translator(catalogPath());

        // A handful of strings from each part of the fork, to catch a catalog that loads but has
        // empty translations.
        const QVector<QPair<QByteArray, QByteArray>> cases{
            { "LibraryDownloadTask", "Downloading %1" },
            { "LocalResourceCopyTask", "Copying %1" },
            { "QObject", "The local files folder cannot be read: %1" },
            { "ModFolderPage", "Get mods from your local files folder or from online mod platforms" },
            { "ResourcePage", "Save to Local Files" },  // the .ui form, not the page class
            { "LauncherPage", "&Local Files:" },
            { "ResourceDownload::ResourcePage", "'%1' does not allow downloads from third-party launchers and no project page is known." },
            { "ResourceDownload::LocalModPage", "Local Files" },
            { "ResourceDownload::LocalResourcePackPage", "Local Files" },
            { "ResourceDownload::LocalShaderPackPage", "Local Files" },
            { "ResourceDownload::LocalTexturePackPage", "Local Files" },
            { "ModConflictsDialog", "Check Again" },
            { "ModConflictsDialog", "\"%1\" needs the mod \"%2\", which is not installed." },
            { "ExternalResourcesPage", "Check for &Conflicts" },
        };

        for (const auto& [context, source] : cases) {
            const auto translated = translator.translate(context.constData(), source.constData(), nullptr, -1);
            QVERIFY2(!translated.isEmpty(), qPrintable(QString("Untranslated: %1|%2").arg(context, source)));
            QVERIFY2(translated != QString::fromUtf8(source), qPrintable(QString("Left in English: %1|%2").arg(context, source)));
        }
    }

    void testUnknownStringsAreNotAnswered()
    {
        POTranslator translator(catalogPath());

        // The catalog must not pretend to cover the whole interface: anything it does not mention
        // has to come back empty so the upstream translator gets its turn.
        QVERIFY(translator.translate("LibraryDialog", "a string nobody wrote", nullptr, -1).isEmpty());
        QVERIFY(translator.translate("NoSuchContext", "Library", nullptr, -1).isEmpty());
    }

    void testPlaceholdersSurvive()
    {
        POTranslator translator(catalogPath());

        // Losing a %1 would silently produce a message with the filename glued to the sentence.
        QCOMPARE(translator.translate("QObject", "No such file: %1", nullptr, -1).toUtf8(), QByteArray("Нет такого файла: %1"));
        QCOMPARE(translator.translate("LibraryDownloadTask", "Downloading %1", nullptr, -1).toUtf8(), QByteArray("Загрузка: %1"));
    }
};

QTEST_GUILESS_MAIN(ForkTranslationsCatalog)

#include "ForkTranslations_test.moc"