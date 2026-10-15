#!/usr/bin/env python3
"""Builds the fork's Russian catalog from the strings lupdate found.

The fork's strings cannot go into the upstream translation files: those come from a server that
only knows about upstream Prism. They are catalogued separately and installed on top.

Only fully translated entries are written out, because POTranslator returns an empty string for an
entry with no text, which would blank out the widget rather than fall through to the upstream
translator.
"""
import xml.etree.ElementTree as ET
from collections import OrderedDict

TS = "fork.ts"

# msgid -> Russian. A msgid missing here is left out of the catalog entirely.
#
# A key may name a context as "Context|msgid" to translate the string only where the fork wrote it.
# That is needed for a string upstream also uses: a bare key would match every context, overriding
# the translation server's own wording for a string the fork has no business renaming.
RU = {
    # --- the Local Files tab ---
    "Local Files": "Локальные файлы",
    "Get mods from your local files folder or from online mod platforms":
        "Взять моды из папки локальных файлов или с онлайн-платформ",
    "Get resource packs from your local files folder or from online mod platforms":
        "Взять ресурспаки из папки локальных файлов или с онлайн-платформ",
    "Get shader packs from your local files folder or from online mod platforms":
        "Взять шейдерпаки из папки локальных файлов или с онлайн-платформ",
    "Get texture packs from your local files folder or from online mod platforms":
        "Взять текстурпаки из папки локальных файлов или с онлайн-платформ",

    # --- the local files folder setting ---
    "&Local Files:": "&Локальные файлы:",
    "Local Files Folder": "Папка локальных файлов",
    "Folder holding mods, resource packs, texture packs and shader packs you keep across instances. "
    "Anything dropped into it shows up under the 'Local Files' tab of the corresponding download "
    "dialog; a markdown file named after a file, with a .md suffix, describes it.":
        "Папка с модами, ресурспаками, текстурпаками и шейдерпаками, общими для всех инстансов. "
        "Всё, что в неё положено, появляется на вкладке «Локальные файлы» в окне загрузки; файл "
        "markdown с тем же именем и суффиксом .md описывает ресурс.",

    # --- scanning the local files folder ---
    "No such file: %1": "Нет такого файла: %1",
    "Could not create the library directory: %1":
        "Не удалось создать папку библиотеки: %1",
    "Could not copy %1 into the library":
        "Не удалось скопировать %1 в библиотеку",
    "The local files folder cannot be read: %1":
        "Не удалось прочитать папку локальных файлов: %1",

    # --- downloading a platform file into the local files folder ---
    "Save to Local Files": "Сохранить в локальные файлы",
    "Download this version into your local files folder, where it can be installed into any "
    "instance later without downloading it again.":
        "Скачать эту версию в папку локальных файлов, чтобы потом ставить её в любой инстанс без "
        "повторной загрузки.",
    "Install an incompatible version?": "Установить несовместимую версию?",
    "'%1' does not allow downloads from third-party launchers and no project page is known.":
        "«%1» не разрешает загрузку сторонними лаунчерами, и страница проекта неизвестна.",
    #: Upstream says the same thing in ResourceModel, so this one is pinned to the fork's copy.
    "ResourceDownload::ModModel|A network error occurred. Could not load project versions: %1":
        "Ошибка сети. Не удалось загрузить версии проекта: %1",

    # --- the download itself ---
    "Downloading %1": "Загрузка: %1",
    "The file has no download link. Open the project page and add the file from disk.":
        "У файла нет ссылки на загрузку. Откройте страницу проекта и добавьте файл с диска.",
    "Could not create a temporary folder for the download.":
        "Не удалось создать временную папку для загрузки.",
    "The library is not available.": "Библиотека недоступна.",
    "Could not stage the downloaded file.":
        "Не удалось подготовить загруженный файл.",
    "The download failed.": "Загрузка не удалась.",

    # --- copying a local file into an instance ---
    "Copying %1": "Копирование: %1",
    "Could not read %1: %2": "Не удалось прочитать %1: %2",
    "Could not write %1: %2": "Не удалось записать %1: %2",

    # --- instance templates ---
    "Templates": "Шаблоны",
    #: The label next to the template picker at the bottom of the page.
    "TemplatePage|Version selected:": "Выбранная версия:",
    "Save from an instance...": "Сохранить из инстанса...",
    "Add version...": "Добавить версию...",
    "Version:": "Версия:",
    "Select a template to see what it will create.":
        "Выберите шаблон, чтобы увидеть, что он создаст.",
    "It does not carry any of the instance's files.":
        "Не содержит файлов инстанса.",
    "It lays down these files and folders:":
        "Раскладывает эти файлы и папки:",
    "It follows your global settings.": "Использует глобальные настройки.",
    "It also remembers the instance settings you changed.":
        "Также запоминает изменённые настройки инстанса.",
    "Minecraft %1": "Minecraft %1",
    "Unknown Minecraft version": "Неизвестная версия Minecraft",
    "Edit...": "Изменить...",
    "Delete": "Удалить",
    "Refresh": "Обновить",
    "Delete version": "Удалить версию",
    "Delete the version \"%1\"? This cannot be undone.":
        "Удалить версию «%1»? Это действие необратимо.",
    "Could not read the templates file: %1":
        "Не удалось прочитать файл шаблонов: %1",
    "Could not save the template: %1":
        "Не удалось сохранить шаблон: %1",
    "Saving the template...": "Сохранение шаблона...",
    "Cancel": "Отмена",

    # --- saving a template from an instance ---
    "Save as Template": "Сохранить как шаблон",
    "Add version to \"%1\"": "Добавить версию к «%1»",
    "Instance:": "Инстанс:",
    "Name:": "Имя:",
    "Description:": "Описание:",
    "Optional. Markdown is supported.": "Необязательно. Поддерживается markdown.",
    "Remember the instance settings you changed":
        "Запомнить изменённые настройки инстанса",
    "Choose which files and folders to include:":
        "Выберите, какие файлы и папки включить:",
    "These files and folders are copied into the template and laid down in the new instance:":
        "Эти файлы и папки копируются в шаблон и раскладываются в новом инстансе:",
    "Reading the instance...": "Чтение инстанса...",
    "Scanning files... (%1 found)": "Сканирование файлов... (%1 найдено)",
    "Nothing will be copied into the template.":
        "В шаблон ничего не будет скопировано.",
    "The instance settings you changed are remembered.":
        "Изменённые настройки инстанса будут запомнены.",
    "There are no instances to save a template from yet.":
        "Пока нет инстансов, из которых можно сохранить шаблон.",

    # --- editing a saved template ---
    "Edit Template": "Изменить шаблон",

    # --- creating an instance from a template ---
    "Creating instance from template \"%1\"": "Создание инстанса из шаблона «%1»",
    "The template does not remember a Minecraft version. Save it again from an instance.":
        "Шаблон не помнит версию Minecraft. Сохраните его заново из инстанса.",
    "Copying template files... (%1/%2)": "Копирование файлов шаблона... (%1/%2)",

    # --- the templates file ---
    "Could not create the folder for the templates file.":
        "Не удалось создать папку для файла шаблонов.",
    "Could not create the folder for the template's files.":
        "Не удалось создать папку для файлов шаблона.",
    "Could not copy \"%1\" into the template.":
        "Не удалось скопировать «%1» в шаблон.",
    "The templates file is not a JSON object.":
        "Файл шаблонов не является объектом JSON.",
}


def main():
    root = ET.parse(TS).getroot()
    out = OrderedDict()
    used = set()
    for context in root.findall("context"):
        cname = context.findtext("name")
        for message in context.findall("message"):
            source = message.findtext("source")
            # A context-pinned key wins over the plain one, so the same words can be translated in
            # one place while the translation server keeps naming them everywhere else.
            pinned = cname + "|" + source
            if pinned in RU:
                out[(cname, source)] = RU[pinned]
                used.add(pinned)
                continue
            if source in RU:
                out[(cname, source)] = RU[source]
                used.add(source)

    lines = [
        "# Russian translations for the strings this fork added.",
        "#",
        "# Generated by i18n/make-fork-catalog.py from lupdate output; edit the dictionary there",
        "# rather than this file. Loaded on top of the upstream catalog, so it only needs to cover",
        "# what the translation server does not know about.",
        'msgid ""',
        'msgstr ""',
        '"Project-Id-Version: PrismLauncher fork\\n"',
        '"MIME-Version: 1.0\\n"',
        '"Content-Type: text/plain; charset=UTF-8\\n"',
        '"Content-Transfer-Encoding: 8bit\\n"',
        "",
    ]
    for (cname, source), translation in sorted(out.items()):
        lines.append('msgctxt "%s"' % cname.replace('"', '\\"'))
        lines.append('msgid "%s"' % source.replace("\\", "\\\\").replace('"', '\\"')
                     .replace("\n", "\\n").replace("\t", "\\t"))
        lines.append('msgstr "%s"' % translation.replace("\\", "\\\\").replace('"', '\\"')
                     .replace("\n", "\\n").replace("\t", "\\t"))
        lines.append("")

    with open("fork_ru.po", "w", encoding="utf-8") as out_file:
        out_file.write("\n".join(lines))

    missing = sorted(set(RU) - used)
    print(f"{len(out)} entries written to fork_ru.po")
    if missing:
        print("no source string found for:", *missing, sep="\n  ")


if __name__ == "__main__":
    main()
