// SPDX-FileCopyrightText: 2026 Kerilom <89039577+Gleb-Lisovskiy@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include <QTest>
#include <QUrl>

#include "modplatform/flame/FlameModIndex.h"

/* The forgecdn layout is /files/<first 4 digits of the file id>/<remaining digits>/<file name>.
 * CurseForge hands out downloadUrl for most projects but withholds it for others, and those files
 * have to stay installable - hence the derivation. */
class FlameDownloadUrl : public QObject {
    Q_OBJECT

   private slots:
    void testRealFile()
    {
        // DOOMED | UNKNOWN CASUALTY, whose API response carries downloadUrl: null.
        QCOMPARE(FlameMod::guessDownloadUrl("8893161", "doomedmatu-0.4.7.3.jar"),
                 QString("https://edge.forgecdn.net/files/8893/161/doomedmatu-0.4.7.3.jar"));
    }

    void testUrlIsUsable()
    {
        // A URL missing its scheme makes Qt fail with ProtocolUnknownError before the request is
        // even sent, and nothing upstream would notice. Assert the parts that make it usable.
        const QUrl url(FlameMod::guessDownloadUrl("8893161", "doomedmatu-0.4.7.3.jar"));
        QVERIFY(url.isValid());
        QCOMPARE(url.scheme(), QString("https"));
        QCOMPARE(url.host(), QString("edge.forgecdn.net"));
        QVERIFY(url.path().startsWith("/files/8893/161/"));
        QCOMPARE(url.toString(), QString("https://edge.forgecdn.net/files/8893/161/doomedmatu-0.4.7.3.jar"));
    }

    void testIdLengthSplit()
    {
        QCOMPARE(FlameMod::guessDownloadUrl("100", "a.jar"), QString(""));
        QCOMPARE(FlameMod::guessDownloadUrl("10000", "a.jar"), QString("https://edge.forgecdn.net/files/1000/0/a.jar"));
        QCOMPARE(FlameMod::guessDownloadUrl("123456789", "a.jar"), QString("https://edge.forgecdn.net/files/1234/56789/a.jar"));
    }

    void testEmptyFileName()
    {
        QCOMPARE(FlameMod::guessDownloadUrl("8893161", ""), QString(""));
    }

    void testFileNameIsEscaped()
    {
        QCOMPARE(FlameMod::guessDownloadUrl("8893161", "my mod #1.jar"),
                 QString("https://edge.forgecdn.net/files/8893/161/my%20mod%20%231.jar"));
    }
};

QTEST_GUILESS_MAIN(FlameDownloadUrl)

#include "FlameDownloadUrl_test.moc"