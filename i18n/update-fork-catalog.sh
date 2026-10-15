#!/usr/bin/env sh
# Regenerates fork.ts and fork_ru.po from the code.
#
# lupdate has to be pointed at every file the fork touched in one go: it replaces the strings in
# the .ts with the ones it finds, so a file left off the list would drop its strings from the
# catalog. Keep this list in step with the fork's sources, not the whole tree: a string that
# upstream also uses would otherwise be pulled into the catalog for a context the fork has no
# business translating.
set -e

cd "$(dirname "$0")/.."

/usr/lib/qt6/bin/lupdate -locations none -no-obsolete -no-ui-lines \
    launcher/InstanceTemplate.cpp \
    launcher/minecraft/TemplateInstanceCreationTask.cpp \
    launcher/minecraft/mod/LibraryIndex.cpp \
    launcher/minecraft/mod/tasks/LibraryDownloadTask.cpp \
    launcher/minecraft/mod/tasks/LocalResourceCopyTask.cpp \
    launcher/ui/dialogs/ResourceDownloadDialog.cpp \
    launcher/ui/dialogs/EditTemplateDialog.cpp \
    launcher/ui/dialogs/EditTemplateDialog.ui \
    launcher/ui/dialogs/ModConflictsDialog.cpp \
    launcher/ui/dialogs/ModConflictsDialog.ui \
    launcher/ui/dialogs/NewInstanceDialog.cpp \
    launcher/ui/dialogs/SaveTemplateDialog.cpp \
    launcher/ui/dialogs/SaveTemplateDialog.ui \
    launcher/ui/pages/global/LauncherPage.cpp \
    launcher/ui/pages/global/LauncherPage.ui \
    launcher/ui/pages/instance/ExternalResourcesPage.ui \
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
