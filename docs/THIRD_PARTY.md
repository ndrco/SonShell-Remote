[English](THIRD_PARTY.md) · [Russian](THIRD_PARTY_RU.md)

# Provenance and third-party components

- **SonShell:** https://github.com/goudvuur/sonshell, Apache-2.0. History is
  preserved through `107f9fa`, the license in LICENSE, and upstream documentation
  under docs/upstream/. The GUI prototype was imported on 2026-10-19.
- **Qt 6:** supplied by Ubuntu system packages under their respective licenses.
- **Sony Camera Remote SDK 2.02.00:** proprietary runtime with separate
  [Sony terms](https://support.d-imaging.sony.co.jp/app/sdk/licenseagreement/en.html).
  The binary library may be incorporated and distributed as part of the camera
  application under those terms; the SDK itself and its headers are not included
  in the source repository. The DEB installs the official license HTML and SDK
  OSS notices under `/usr/share/doc/sonshell-remote/`. End users must accept the
  SDK terms before the first connection, including applicable device-warranty
  provisions and use restrictions. This is an independent application maintained
  by NDRco; Sony does not develop or support it.
- **SDK open-source components:** sdk-oss-notices.md is copied from the SDK's
  README.md. It contains libssh2, OpenSSL and libusb notices; their licenses are
  separate from the application's Apache-2.0 license.
- **G'MIC 3.6.3:** the inherited source archive in ext/ comes from SonShell.
  Its CeCILL/CeCILL-C terms are in COPYING inside the archive. It is an optional
  CLI tool, not Sony SDK or a binary library; the GUI does not require it.
- **ExifTool (Phil Harvey):** https://github.com/exiftool/exiftool, supplied by
  libimage-exiftool-perl. Called as a separate, read-only process. License and
  author details are in `/usr/share/doc/libimage-exiftool-perl/copyright`.
- **FFmpeg:** https://ffmpeg.org/, supplied by the ffmpeg package. A separate
  process decodes posters and video previews. Installed build/codecs and their
  licenses are listed in `/usr/share/doc/ffmpeg/copyright`. FFmpeg libraries and
  ExifTool sources are not copied into or linked with the GUI.

The public release retains the SDK's bundled license and notices. Sony's online
terms were checked on 2026-10-31; the authoritative terms remain those provided
by Sony. Third-party components are not relicensed under Apache-2.0.
