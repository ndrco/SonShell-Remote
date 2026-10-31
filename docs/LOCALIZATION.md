[English](LOCALIZATION.md) · [Russian](LOCALIZATION_RU.md)

# Interface languages

English is the source and default language. Settings → Language saves English or
Russian for the next launch without interrupting the current session. Existing
saved preferences are retained; missing or invalid settings fall back to English.
Changing the application language does not change system locale or services.

GUI messages use `QCoreApplication::translate("App", ...)` with English source
strings. `gui/i18n/sonshell_RU.ts` is the Qt Linguist Russian catalog. CMake runs
lrelease from qt6-l10n-tools and embeds `sonshell_RU.qm` in application resources.
The target computer needs no translation tools. Russian Qt system buttons use
qt6-translations-l10n, an explicit DEB dependency. Backend/SDK diagnostics and
camera mode names may remain English; protocol commands and JSON keys are stable.

After changing messages:

```bash
/usr/lib/qt6/bin/lupdate gui/main.cpp gui/src -no-obsolete -ts gui/i18n/sonshell_RU.ts
python3 tools/check_translations.py
```

Preserve `%1`, `%2`, `%p%` and spaces around dynamic text. CTest checks catalog
coverage, unfinished translations and placeholder counts. Do not translate
protocol commands, JSON keys, parameter identifiers or file formats.

Offline help is stored in `gui/help/HELP.md` and `gui/help/HELP_RU.md`. Resource
aliases `:/help/en.md` and `:/help/ru.md` remain stable. Documentation uses English
filenames and `_RU.md` Russian counterparts, with links between languages.

Adding another language requires a catalog and help text, CMake/RCC resources,
SettingsStore/LanguageSupport allow-list entries, a menu entry and runtime tests.
Complete the translation and verify the screens before exposing it in the menu.
