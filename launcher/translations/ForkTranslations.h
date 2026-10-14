// SPDX-FileCopyrightText: 2026 Kerilom <89039577+Gleb-Lisovskiy@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QString>

#include <memory>

class POTranslator;

/** The translations for the strings this fork added.
 *
 *  They cannot live in the upstream catalogs: those are downloaded from a translation server that
 *  only knows about upstream Prism, and it would overwrite anything shipped here on the next
 *  language refresh. So they are kept in their own catalog, which is installed as an extra
 *  translator on top of whatever the server provided. Only the strings the server does not know
 *  about need to be listed, and an entry with no translation is left out entirely - POTranslator
 *  answers an empty string for one, which would blank the widget instead of falling through.
 */
class ForkTranslations {
   public:
    ForkTranslations();
    ~ForkTranslations();

    /** Installs the catalog for the given language, if there is one for it.
     *
     *  Called whenever the selected language changes, so switching languages in the settings picks
     *  the fork's strings up as well. */
    void load(const QString& language);

   private:
    /** Where the catalogs are looked for, in order.
     *
     *  The application directory covers an installed build and this repository's build tree; the
     *  data directory covers a launcher that only ever reads its own resources from there. */
    [[nodiscard]] QStringList searchPaths() const;

    std::unique_ptr<POTranslator> m_translator;
};