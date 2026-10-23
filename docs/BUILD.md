# Сборка

## GUI и тесты без SDK

На Ubuntu 24.04:

```bash
sudo apt install build-essential cmake qt6-base-dev qt6-l10n-tools qt6-translations-l10n qt6-image-formats-plugins libimage-exiftool-perl ffmpeg python3
cmake -S . -B build-gui -DCMAKE_BUILD_TYPE=Release
cmake --build build-gui --parallel 4
ctest --test-dir build-gui --output-on-failure
```

GUI без backend можно использовать для разработки; для камеры нужен backend,
собранный из этого проекта с поддержкой протокола REMOTE/1.

## Полная сборка

Получить Sony Camera Remote SDK отдельно, принять условия Sony, распаковать.
SONY_SDK_DIR указывает на каталог с `app/` и `external/` (в SDK 2.02 это RemoteCli).

```bash
sudo apt install libedit-dev libncurses-dev libxml2-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DSONSHELL_BUILD_BACKEND=ON -DSONSHELL_HEADLESS=ON \
  -DSONY_SDK_DIR=/absolute/path/RemoteCli
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
./build/gui/sonshell-remote
```

Headless исключает только отдельный OpenCV viewer. Встроенный GUI Live View
получает JPEG из SDK. Для старого CLI viewer: HEADLESS=OFF, дополнительно GTK3
и OpenCV из SDK. Оригинальные CLI команды — docs/upstream/SONSHELL.md.

## Установочный пакет

На целевой Ubuntu 24.04 amd64:

```bash
sudo apt install dpkg-dev desktop-file-utils
SONY_SDK_DIR=/absolute/path/RemoteCli ./scripts/package-deb.sh
```

Результат: `build-package/sonshell-remote_0.6.0-1ubuntu24.04_amd64.deb`.
Скрипт скачивает HTML лицензии Sony в build-каталог (можно передать локальный
SONY_SDK_LICENSE_FILE), собирает проект, выполняет тесты и CPack DEB.
Зависимости вычисляет dpkg-shlibdeps, runtime SDK помещается в приватный
`/usr/lib/sonshell-remote`, заголовки SDK в пакет не входят. Qt не копируется,
устанавливается из репозитория Ubuntu. Пакет не требует SDK на целевой машине.

Не коммитить deb/SDK/локальные license HTML; распространять runtime только
как часть приложения с условиями и OSS notices — см. THIRD_PARTY.md.

`lrelease` используется только при сборке. Для нестандартного расположения задай
`-DQT_LRELEASE_EXECUTABLE=/absolute/path/lrelease`.
