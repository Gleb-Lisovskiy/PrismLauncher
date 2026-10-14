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

```sh
lupdate -locations none -no-obsolete <the fork's sources> -ts i18n/fork.ts
cd i18n && python3 make-fork-catalog.py
```

The script reads the translations out of the dictionary at the top of `make-fork-catalog.py` and
writes `fork_ru.po`. Run it from the `i18n` directory, next to the `.ts` it reads.
