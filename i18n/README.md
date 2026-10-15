# Translations for the strings this fork added

The launcher downloads its translations from a translation server that only knows about upstream
Prism, so everything this fork added has no entry there and would stay in English in an otherwise
translated interface.

`fork_<lang>.po` holds those strings and nothing else. `ForkTranslations` installs it as an extra
translator on top of the one from the server, so the two coexist: whatever the fork knows wins,
everything else falls through.

Two things worth knowing before editing:

- **Only fully translated entries belong in the file.** `POTranslator` answers an empty string for
  an entry with no text, and Qt takes that at face value, which blanks the widget instead of
  falling through to the upstream translator. A partial catalog is worse than none.
- **Contexts come from the code.** A `tr()` inside a non-`QObject` class lands in the `QObject`
  context, and the strings of a `.ui` file land in the form's class name rather than the dialog's.
  `tests/ForkTranslations_test.cpp` reads the shipped file and catches contexts that have drifted.

## Regenerating

Run `i18n/update-fork-catalog.sh`. It points `lupdate` at the files the fork touched, not the whole
tree, and then runs `make-fork-catalog.py`. `lupdate` replaces the strings in `fork.ts` with what it
finds, so it has to be given every fork file in one go; adding a new fork file means adding it to
the script's list, or its strings would silently drop out of the catalog. A string that upstream
also uses must not be pulled in for a context the fork has no business translating, which is why the
list is the fork's own files rather than the tree.

The generator reads the translations out of the dictionary at the top of `make-fork-catalog.py` and
writes `fork_ru.po`. Run it from the `i18n` directory, next to the `.ts` it reads. A dictionary key
may be written `"Context|msgid"` to translate a string only where the fork wrote it.
