[English](BUILD.md) · [Russian](BUILD_RU.md)

# Build

Get sources:

```bash
git clone https://github.com/ndrco/SonShell-Remote.git
cd SonShell-Remote
```

## GUI and tests without SDK

On Ubuntu 24.04:

```bash
sudo apt install build-essential cmake qt6-base-dev qt6-l10n-tools qt6-translations-l10n qt6-image-formats-plugins libimage-exiftool-perl ffmpeg python3
cmake -S . -B build-gui -DCMAKE_BUILD_TYPE=Release
cmake --build build-gui --parallel 4
ctest --test-dir build-gui --output-on-failure
```

GUI without backend can be used for development; the camera needs a backend,
compiled from this project with support for the REMOTE/1 protocol.

## Full build

Get Sony Camera Remote SDK separately, accept Sony's terms and conditions, unpack.
SONY_SDK_DIR points to the directory with `app/` and `external/` (in SDK 2.02 this is RemoteCli).

```bash
sudo apt install libedit-dev libncurses-dev libxml2-dev libssl-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DSONSHELL_BUILD_BACKEND=ON -DSONSHELL_HEADLESS=ON \
  -DSONY_SDK_DIR=/absolute/path/RemoteCli
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
./build/gui/sonshell-remote
```

Headless only excludes a separate OpenCV viewer. Built-in GUI Live View
gets JPEG from SDK. For the legacy CLI viewer: SONSHELL_HEADLESS=OFF, additionally GTK3
and OpenCV from SDK. Original CLI commands - docs/upstream/SONSHELL.md.

## Installation package

On target Ubuntu 24.04 amd64:

```bash
sudo apt install dpkg-dev desktop-file-utils
SONY_SDK_DIR=/absolute/path/RemoteCli ./scripts/package-deb.sh
```

Result: `build-package/sonshell-remote_0.8.4-1ubuntu24.04_amd64.deb`.
The script downloads the Sony license HTML to the build directory (you can supply a local
SONY_SDK_LICENSE_FILE), builds the project, runs tests and CPack DEB.
Dependencies are calculated by dpkg-shlibdeps, the SDK runtime is installed under the private
`/usr/lib/sonshell-remote`, SDK headers are not included in the package. Qt is not copied
installed from the Ubuntu repository. The package does not require an SDK on the target machine.

Do not commit deb/SDK/local license HTML; distribute runtime only
as part of the application with conditions and OSS notices - see THIRD_PARTY.md.

`lrelease` is used only during build. For a non-standard location, specify
`-DQT_LRELEASE_EXECUTABLE=/absolute/path/lrelease`.
