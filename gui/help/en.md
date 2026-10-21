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
