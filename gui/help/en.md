# SonShell Remote — help

## Connect

Enter the camera IPv4 address, SonShell model (for example a6700), username,
password and photo directory. Enable network remote control and configure access
in the camera menu. Connected status requires a camera response. Automatic
reconnection is optional; Disconnect cancels retries.

Remember username and password saves credentials in a private, unencrypted file
on this computer. Clearing the checkbox removes the saved credentials.

## Review the camera card

After shooting, open On camera → Refresh card. Viewing transfers compressed
previews. Shooting and browsing do not download RAW originals. Preview resolution
depends on the camera; 100% refers to the preview.

| Key | Action |
| --- | --- |
| ← / →, Page Up / Page Down | Previous / next visible photo |
| Space | Toggle the Copy mark |
| Delete | Toggle the Reject mark |
| F11 | Enter / leave full screen |
| Esc | Leave full screen |
| F1 | Open help |

Marking works with focus in the card list or preview, including full screen.
Press again to clear a mark. In the search field Space and Delete edit text.
Marking is disabled during copying or deletion. Protected photos cannot be marked
as rejects. In the local gallery these keys do not delete or mark files.

**Delete only marks a reject.** Click Delete rejects…, inspect the detailed list
and confirm to erase photos. The camera deletes every file belonging to a photo,
including RAW+JPEG. Card deletion cannot be undone; local copies are preserved.

## Copy

Choose the destination directory at the top and the file formats. Copy marked
uses copy marks. Copy new skips successful copies recorded in that directory's
import ledger. Copy all transfers all originals again. The latter two commands
use the whole card catalog regardless of search. Existing files are preserved;
additional copies receive suffixes.

Mark all and Mark new respect search. Refreshing the card or reconnecting clears
marks. Stop clears the queue and waits for the SDK response; completed copies
are preserved. 100% means the queue has finished; the result reports errors
separately. A disabled Stop button means there is no operation in progress.

## Live View and AF

Fit, 100%, +/− and the wheel change zoom. Drag to pan. F11 and Esc work while the
viewer has focus.

Camera settings · SDK shows values provided by the camera: focus mode, AF area,
white balance and drive mode. Changing the physical camera mode may change
availability. Use Read settings to refresh manually if necessary.

To choose an AF area, enable Live View, choose a movable AF area such as Spot/Zone
if supported, then enable Select AF point with mouse. Click to set the frame
center; dragging still pans. Clicks outside the image are ignored. Coordinates
outside the permitted AF frame bounds are rejected. Autofocus performs a
half-press. An accepted point does not confirm sharp focus. The application does
not change exposure mode or the physical focus switch to enable this feature.

Studio mode briefly shows a compressed preview after capture, then returns to
Live View. Hold photo pauses the timer. AF point selection is disabled while a
captured photo is displayed.

## On computer · EXIF

The gallery shows JPEG/PNG/RAW and EXIF from the selected directory and its
subdirectories. RAW viewing uses an embedded JPEG through ExifTool; RAW
processing is not provided. Double-click opens a separate window at 100%.
Arrow keys browse the current filtered selection.

## Language and information

Settings → Language selects Russian or English. The choice takes effect on the
next launch without interrupting the current camera session. Help is available
offline. Backend/SDK diagnostics and camera mode names may be in English.

[SonShell project](https://github.com/goudvuur/sonshell) ·
[Sony Camera Remote SDK](https://support.d-imaging.sony.co.jp/app/sdk/en/)

Do not publish passwords, settings, fingerprints or personal photos in bug reports.

## Video and capture dates (0.6)

MP4 clips now appear with the proxy/XML files returned as part of a camera item.
Photos and videos, Photos only and Video only filter visible items and copying.
RAW/JPEG selects photo formats. The still preview of a clip uses the camera
thumbnail; the SDK's larger screennail is available only for photos.

Play streams from the camera without copying the original. Pause, Stop and
seeking to a specified second are available. This is a **silent preview**, limited
only by the received stream's resolution and frame rate; the original audio remains unchanged. SDK/firmware support
and reachable network ports are required. Camera/decoder failures are shown.
Live View stops during streaming and resumes afterwards if previously enabled.
Changing tabs or items stops the stream. Stop playback before copying/deleting.
Space/Delete marking works over the video as well.

**No video data from camera:** control and Live View may work while the firewall
blocks the separate movie stream. Allow incoming **TCP 55001, 55003, 55005** from
the camera IP to the computer IP. The error shows both addresses. Check firewall
rules and network client isolation. The application does not change system rules.
Example for UFW (replace the addresses; administrator privileges required):

```bash
sudo ufw allow proto tcp from 10.0.1.5 to 10.0.1.84 port 55001,55003,55005 comment 'SonShell Remote video'
```

Undo with the same command using `delete allow` instead of `allow`. Keep the firewall enabled.

In full screen, the filename and COPY/REJECT states remain visible over the image.
A separate overlay confirms each key action or explains why a protected item
cannot be marked. Selecting another item clears the old photo while loading.

New copies use **YYYY-MM-DD/Slot_N/original camera hierarchy**. The date is the
camera's local capture date, falling back to valid UTC or Unknown-Date. The
computer's current date is never substituted. Old copies are preserved and still
count towards Copy new. The local gallery shows imported MP4/MOV/M4V, reads the
first frame in the background and plays video on request. Photos and videos with
the same base name remain separate items.
Local playback keeps the original resolution and frame rate without JPEG
re-encoding. The first frame is read at its original size too. The main window's
bottom status line shows the selected file's full camera or local path. Select
and copy it; long paths scroll and are also available in the tooltip.
