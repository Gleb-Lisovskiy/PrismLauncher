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

`lupdate` has to be pointed at the files the fork touched, not the whole tree: it copies every
translatable string it finds into `fork.ts`, and a string that upstream also uses would then be
pulled into the catalog for a context the fork has no business translating.

```sh
/usr/lib/qt6/bin/lupdate -locations none -no-obsolete -no-ui-lines \
    launcher/InstanceTemplate.cpp \
    launcher/minecraft/TemplateInstanceCreationTask.cpp \
    launcher/minecraft/mod/LibraryIndex.cpp \
    launcher/minecraft/mod/tasks/LibraryDownloadTask.cpp \
    launcher/minecraft/mod/tasks/LocalResourceCopyTask.cpp \
    launcher/ui/dialogs/ResourceDownloadDialog.cpp \
    launcher/ui/dialogs/EditTemplateDialog.cpp \
    launcher/ui/dialogs/EditTemplateDialog.ui \
    launcher/ui/dialogs/SaveTemplateDialog.cpp \
    launcher/ui/dialogs/SaveTemplateDialog.ui \
    launcher/ui/pages/global/LauncherPage.cpp \
    launcher/ui/pages/global/LauncherPage.ui \
    launcher/ui/pages/instance/ModFolderPage.cpp \
    launcher/ui/pages/instance/ResourcePackPage.cpp \
    launcher/ui/pages/instance/ShaderPackPage.cpp \
    launcher/ui/pages/instance/TexturePackPage.cpp \
    launcher/ui/pages/modplatform/library/LocalResourcePage.h \
    launcher/ui/pages/modplatform/ModModel.cpp \
    launcher/ui/pages/modplatform/ResourceModel.cpp \
    launcher/ui/pages/modplatform/ResourcePackPage.h \
    launcher/ui/pages/modplatform/ResourcePage.cpp \
    launcher/ui/pages/modplatform/ResourcePage.ui \
    launcher/ui/pages/modplatform/ShaderPackPage.h \
    launcher/ui/pages/modplatform/TemplatePage.cpp \
    launcher/ui/pages/modplatform/TemplatePage.ui \
    launcher/ui/pages/modplatform/TexturePackPage.h \
    -ts i18n/fork.ts
cd i18n && python3 make-fork-catalog.py
```

The script reads the translations out of the dictionary at the top of `make-fork-catalog.py` and
writes `fork_ru.po`. Run it from the `i18n` directory, next to the `.ts` it reads. A dictionary key
may be written `"Context|msgid"` to translate a string only where the fork wrote it.
