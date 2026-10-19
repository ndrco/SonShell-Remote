# SonShell Remote: правила разработки

- Общение и документация — по-русски. C++17, Qt 6, два пробела, snake_case, RAII.
- gui/ — Qt, src/ — backend SonShell, cmake/ — SDK/build glue, tests/ — проверки без камеры, docs/ — решения и планы.
- Sony SDK, библиотеки, снимки, пароли, fingerprints и настройки не добавлять в Git. SDK задаётся через SONY_SDK_DIR.
- Сохранять CLI. GUI использует docs/PROTOCOL.md; процесс не означает подключённую камеру.
- Изменения фиксировать в CHANGELOG.md, планы — docs/ROADMAP.md, решения — docs/ARCHITECTURE.md. Коммитить логические этапы.
- Проверять CMake build и CTest offscreen; SDK изменения также сборкой backend. Аппаратные проверки отделять от симуляции.
- Не менять исходный прототип в Downloads и глобальные настройки/сервисы.
