[English](README.md) · [Russian](README_RU.md)

# SonShell Remote

<a href="https://github.com/ndrco"><img src="gui/assets/ndrco.png" alt="NDRco" width="96" align="right"></a>

A Qt 6 desktop application for controlling Sony cameras on Linux: Live View,
remote capture, camera settings, photo and video review, selection and import.
Developed and maintained by [NDRco](https://github.com/ndrco), based on
[SonShell](https://github.com/goudvuur/sonshell) and the
[Sony Camera Remote SDK](https://support.d-imaging.sony.co.jp/app/sdk/en/).

**Version 0.8.4 — in development.** The tested installation target is Ubuntu 24.04
amd64. Hardware checks have been performed on a Sony ILCE-6700; available commands
depend on the camera model, firmware and shooting mode.

![SonShell Remote: Live View and camera controls](docs/screenshots/sonshell-remote.png)

**Shoot without automatically transferring RAW → review the camera card →
select or reject captures → import all, new or selected files.**

## Features

- Embedded Live View with zoom, panning and full screen; separate image viewer
  with Fit and 100% views.
- Remote shutter, autofocus and video recording. Click-to-focus moves the AF
  point and triggers focusing when supported by the current camera mode.
- ISO, shutter speed, aperture and exposure compensation: mouse dials, wheel,
  supported-value lists and manual entry. ISO AUTO fits in the field.
- Focus, white balance, drive mode, photo/video recording settings and stabilization
  exposed according to the camera's SDK capabilities.
- Connection state, automatic reconnection and optional startup connection using
  saved credentials. Credentials are saved only when the checkbox is enabled.
- Camera-card browser for photos and videos, independent copy/reject marks,
  format filters, thumbnails and confirmed deletion.
- Original JPEG previews; original video playback through a temporary cache.
  Large originals are downloaded on request, with progress and cancellation.
- Import all, new or selected captures; choose RAW/JPEG and the destination.
  Files are stored under `YYYY-MM-DD/Slot_N/original hierarchy`.
- SHA256 verification, a persistent import journal and resumable queues.
  An interrupted individual file is transferred again from the beginning.
- Local gallery with thumbnails, search, sorting, RAW+JPEG grouping and EXIF.
  RAW viewing uses its embedded JPEG.
- Optional studio review for 2–10 seconds after capture, followed by Live View.
  Choose a compressed preview or an original JPEG from the card; RAW is not
  automatically transferred.
- Previous/next file buttons, video seeking by ±5 seconds, keyboard navigation,
  full-screen selection overlays, download progress and cancellation.
- Settings menu for connection, directory and preview options; offline help (F1)
  and English/Russian interfaces. English is the default; a saved language is retained.

On the camera card, **Space** toggles a copy mark and **Delete** toggles a reject
mark. Deletion requires a separate confirmation. Use **←/→**, **Page Up/Page Down**
to browse, **F11** for full screen and **Esc** to return.

## Install

Download the Ubuntu 24.04 amd64 package from
[GitHub Releases](https://github.com/ndrco/SonShell-Remote/releases/latest):

```bash
sudo apt update
sudo apt install ./sonshell-remote_0.8.4-1ubuntu24.04_amd64.deb
```

The DEB includes the application backend and Sony runtime libraries. Ubuntu
installs Qt, ExifTool and FFmpeg dependencies through apt; no compiler or separate
SDK installation is needed on the target machine. Sony SDK terms apply separately
and are presented before the first camera connection.

See [installation and removal](docs/INSTALL.md) and the [user guide](docs/USER_GUIDE.md).

## Build from source

```bash
git clone https://github.com/ndrco/SonShell-Remote.git
cd SonShell-Remote
```

The GUI and tests can be built without Sony SDK. Camera control requires a backend
built with a separately obtained SDK. The development baseline is C++17, Qt 6.4.2
and Sony SDK 2.02.00. See [build instructions](docs/BUILD.md).

## Current limitations

- Other Ubuntu versions and ARM64 require separate builds and installation checks.
- One camera over IPv4/Wi-Fi/LAN; USB discovery and multiple cameras are not yet
  exposed in the GUI.
- RAW development is not implemented; viewing uses an embedded JPEG.
- Full-resolution studio review requires a JPEG on the card, including RAW+JPEG.
  RAW-only uses a compressed preview. Physical bursts need further hardware testing.
- Main video playback downloads the complete original before starting. Optional
  SDK streaming can fail to deliver video frames for some ILCE-6700 clips.
- Video preview has no audio; copying preserves the original file in full.
- Automated tests do not replace acceptance testing on each camera and configuration.

## Documentation

- [User guide](docs/USER_GUIDE.md) · [Russian](docs/USER_GUIDE_RU.md)
- [Installation](docs/INSTALL.md) · [Build](docs/BUILD.md)
- [Architecture](docs/ARCHITECTURE.md) · [Protocol](docs/PROTOCOL.md)
- [Localization](docs/LOCALIZATION.md) · [Test results](docs/TESTING.md)
- [Changelog](CHANGELOG.md) · [Contributing](CONTRIBUTING.md)

`gui/` contains the Qt application; `src/` the backend and CLI; `cmake/` the SDK
and packaging integration; `tests/` camera-free checks; `docs/` project documents.

The SonShell history is preserved through `107f9fa`. Application source is
licensed under [Apache-2.0](LICENSE). Sony SDK and third-party components retain
their own terms; see [third-party notices](docs/THIRD_PARTY.md). This is an
independent project, not a Sony product or support service.
