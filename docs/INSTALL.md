[English](INSTALL.md) · [Russian](INSTALL_RU.md)

# Installation on a clean Ubuntu system

Release 0.8.4 targets **Ubuntu 24.04 LTS Desktop, amd64** (Intel/AMD 64-bit).
You need the DEB, an internet connection for Ubuntu dependencies and a compatible
Sony camera. A compiler, source checkout or separate SDK installation is not required.

Download the package from [GitHub Releases](https://github.com/ndrco/SonShell-Remote/releases/latest):

```bash
sudo apt update
sudo apt install ./sonshell-remote_0.8.4-1ubuntu24.04_amd64.deb
```

Start **SonShell Remote** from the application menu or run `sonshell-remote`.
The package contains the GUI, backend and private Sony runtime libraries. apt
installs Qt, ExifTool, FFmpeg and other system dependencies. Run the application
as a normal user. English is the default; Settings → Language offers Russian.
An existing saved language preference is preserved during an upgrade.

## First connection

1. Connect the camera and computer to the same network. Enable PC Remote and
   Access Authentication for Wi-Fi/LAN on the camera; obtain its IPv4 and credentials.
2. Open Settings → Connection and enter the IPv4, model (for example `a6700`),
   username and password. Choose the photo directory in Settings → Directory and files.
3. Read and accept the Sony SDK terms before the first connection.
4. Enable credential storage if desired, then click Connect.

See Sony's [supported models and interfaces](https://support.d-imaging.sony.co.jp/app/sdk/en/).
The GUI currently uses IPv4/Wi-Fi/LAN. USB discovery is not exposed in the GUI.
The connection log helps diagnose camera and network errors.

## Network for SDK video playback

Optional SDK Movie Playback accepts separate inbound TCP connections on 55001
(video), 55003 (audio) and 55005 (metadata). A working control connection or Live
View does not prove these ports are reachable. Error messages show the camera
address and the computer address selected by the route. Check router client
isolation as well. Main video playback uses a downloaded original instead.

Example UFW rule for camera `10.0.1.5` and computer `10.0.1.84`:

```bash
sudo ufw allow proto tcp from 10.0.1.5 to 10.0.1.84 port 55001,55003,55005 comment 'SonShell Remote video'
```

Use your actual addresses. This administrator command allows only that camera
and those ports; update it if DHCP addresses change. The application and package
do not change firewall rules. To undo this exact rule:

```bash
sudo ufw delete allow proto tcp from 10.0.1.5 to 10.0.1.84 port 55001,55003,55005
```

## Remove or downgrade

```bash
sudo apt remove sonshell-remote
```

Photos and settings remain. To remove application settings, delete
`~/.config/SonShellRemote/settings.ini`; this also deletes stored credentials.
To downgrade, install the earlier package with
`sudo apt install --allow-downgrades ./sonshell-remote_<version>_amd64.deb`.
The installer does not alter global service settings.

## Other Ubuntu versions and architectures

This binary is built for Ubuntu 24.04 amd64. Ubuntu 22.04, 26.04 and ARM need
separate builds using a compatible SDK and clean-install testing. glibc, Qt and
DEB dependency names differ, so the binary packages are not interchangeable.
Recorded verification results are in [TESTING.md](TESTING.md).
