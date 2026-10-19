# SonShell Remote

Настольное приложение для управления камерами Sony под Linux на Qt 6.
Основано на [SonShell](https://github.com/goudvuur/sonshell) (Apache-2.0) и
[Sony Camera Remote SDK](https://support.d-imaging.sony.co.jp/app/sdk/en/).

История SonShell сохранена до `107f9fa`. Тестовый GUI перенесён из локального
прототипа; исходная папка не изменяется. SDK устанавливается отдельно и в Git
не входит. Локальные версии: SDK 2.02.00, Qt 6.4.2.

- [Сборка и запуск](docs/BUILD.md)
- [План развития](docs/ROADMAP.md)
- [Архитектура](docs/ARCHITECTURE.md)
- [Проверки](docs/TESTING.md)
- [Изменения](CHANGELOG.md)

| Каталог | Назначение |
|---|---|
| gui/ | Qt: интерфейс, сеанс камеры, настройки, изображения |
| src/ | SonShell CLI и SDK |
| cmake/ | Сборка backend и размещение runtime SDK |
| tests/ | Проверки без физической камеры |
| docs/ | Документация и план развития |
| docs/upstream/ | Оригинальная документация SonShell |
| scripts/, tools/, config/ | Унаследованные hooks, генераторы и примеры |

Неофициальный проект. Доступность команд зависит от модели, прошивки, режима
камеры и версии SDK. Реальные аппаратные проверки указываются отдельно.
