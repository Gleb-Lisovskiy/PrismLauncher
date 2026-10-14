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
RU = {
    # --- the tab and the library window ---
    "Local Files": "Локальные файлы",
    "Library": "Библиотека",
    "Search:": "Поиск:",
    "Filter by name, mod ID or version":
        "Фильтр по имени, mod ID или версии",
    "Files here are stored once and can be installed into any instance.":
        "Файлы хранятся здесь в единственном экземпляре и ставятся в любой инстанс.",
    "Install into:": "Установить в:",
    "Add Files...": "Добавить файлы...",
    "Install Selected": "Установить выбранные",
    "Edit Description...": "Изменить описание...",
    "Edit Description": "Изменить описание",
    "Remove": "Удалить",
    "Open Folder": "Открыть папку",
    "Remove From Library": "Убрать из библиотеки",
    "Install into Instance": "Установить в инстанс",
    "Installed": "Установлено",
    "Resource Type": "Тип ресурса",
    "What kind of file are you adding?": "Что за файлы вы добавляете?",
    "Resource files (*.jar *.zip);;All files (*)":
        "Файлы ресурсов (*.jar *.zip);;Все файлы (*)",
    "Some Files Were Not Added": "Некоторые файлы не добавлены",
    "Remove '%1' from the library?": "Убрать «%1» из библиотеки?",
    "Remove %n selected entries from the library?":
        "Убрать %n выбранных записей из библиотеки?",
    "Added %n file(s).\n\n": "Добавлено файлов: %n.\n\n",
    "Cannot Install": "Не удаётся установить",
    "Could Not Remove": "Не удалось убрать",
    "Could Not Save": "Не удалось сохранить",
    "Could Not Install": "Не удалось установить",
    "Some Entries Were Not Installed": "Некоторые записи не установлены",
    "Installed into '%1':\n%2": "Установлено в «%1»:\n%2",
    "This instance does not use the standard mod folder layout.":
        "В этом инстансе нет стандартной папки модов.",
    "This instance does not use the standard resource folders.":
        "В этом инстансе нет стандартных папок ресурсов.",
    "The file is missing from the library: %1":
        "Файл отсутствует в библиотеке: %1",
    "Could not copy %1 into the instance.":
        "Не удалось скопировать %1 в инстанс.",
    "File is missing": "Файл отсутствует",

    # --- library index errors ---
    "%1: %2": "%1: %2",
    "%1: the file is missing from the library":
        "%1: файл отсутствует в библиотеке",
    "%1: could not be copied into the instance":
        "%1: не удалось скопировать в инстанс",
    "%1: unsupported resource type": "%1: неподдерживаемый тип ресурса",
    "No such file: %1": "Нет такого файла: %1",
    "Could not create the library directory: %1":
        "Не удалось создать папку библиотеки: %1",
    "Could not copy %1 into the library":
        "Не удалось скопировать %1 в библиотеку",
    "No such library entry: %1": "Нет такой записи в библиотеке: %1",

    # --- downloading into the library ---
    "Downloading %1": "Загрузка: %1",
    "The file has no download link. Open the project page and add the file from disk.":
        "У файла нет ссылки на загрузку. Откройте страницу проекта и добавьте файл с диска.",
    "Could not create a temporary folder for the download.":
        "Не удалось создать временную папку для загрузки.",
    "The library is not available.": "Библиотека недоступна.",
    "Could not stage the downloaded file.":
        "Не удалось подготовить загруженный файл.",
    "The download failed.": "Загрузка не удалась.",

    # --- library table columns ---
    "Mod ID": "Mod ID",
    "Version": "Версия",
    "Size": "Размер",
    "Type": "Тип",
    "Name": "Имя",
    "Source": "Источник",
    "Status": "Статус",
    "Ready": "Готово",
    "Mod": "Мод",
    "Resource pack": "Ресурспак",
    "Shader pack": "Шейдерпак",
    "Texture pack": "Текстурпак",
    "Local file": "Локальный файл",
    "&Name:": "&Имя:",
    "&Version:": "&Версия:",
    "&Description:": "&Описание:",

    # --- instance page tooltips ---
    "Get mods from your local files folder or from online mod platforms":
        "Взять моды из папки локальных файлов или с онлайн-платформ",
    "Get resource packs from your local files folder or from online mod platforms":
        "Взять ресурспаки из папки локальных файлов или с онлайн-платформ",
    "Get shader packs from your local files folder or from online mod platforms":
        "Взять шейдерпаки из папки локальных файлов или с онлайн-платформ",

    # --- resource page ---
    "Added to Library": "Добавлено в библиотеку",
    "'%1' is now in your library. Open it from the Library button in the toolbar to install it "
    "into other instances.":
        "«%1» теперь в библиотеке. Откройте его кнопкой «Библиотека» на панели, чтобы "
        "установить в другие инстансы.",
    "Install an incompatible version?": "Установить несовместимую версию?",
    "'%1' does not allow downloads from third-party launchers and no project page is known.":
        "«%1» не разрешает загрузку сторонними лаунчерами, и страница проекта неизвестна.",
}


def main():
    root = ET.parse(TS).getroot()
    out = OrderedDict()
    for context in root.findall("context"):
        cname = context.findtext("name")
        for message in context.findall("message"):
            source = message.findtext("source")
            translation = RU.get(source)
            if translation is None:
                continue
            out[(cname, source)] = translation

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

    missing = sorted(set(RU) - {s for _, s in out})
    print(f"{len(out)} entries written to fork_ru.po")
    if missing:
        print("no source string found for:", *missing, sep="\n  ")


if __name__ == "__main__":
    main()