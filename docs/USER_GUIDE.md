[English](USER_GUIDE.md) · [Russian](USER_GUIDE_RU.md)

# Working with the application

## Connection and settings

Open “Settings → Connection” (Ctrl+,) to set the address and access.
“Directory and files” sets the import location, “Preview” — photo/video quality
from the card and studio showing. Connect, status and control remain in the main window
camera. "Connection Log" in the menu shows a hidden log. Changes
are saved automatically; the settings window does not interrupt the connection.

The username and password are saved by checkbox in a private file (0600), without encryption.
Unchecking the checkbox immediately deletes saved fields; without him they are left only
in the memory of the current window to reconnect. The settings are in
`~/.config/SonShellRemote/settings.ini`; the settings of the original prototype are not changed.

The status is determined by SDK events. If there is a break, the control is turned off, repeats
in 3/6/12/24/30 s. "Disable" cancels them. Stop if the password is incorrect
repeats, correct the fields and connect again. The first connection requires
familiarization with the terms and conditions of the Sony SDK.

As of 0.8, “Connect on startup” is enabled by default. If username/password
saved and the SDK conditions have already been accepted, the window connects itself after opening.
In case of incomplete/incorrect settings, an explanation remains without a modal window.
Connect manually and save access so that the next launch will connect itself.
Unchecking this option disables this behavior; manual connection/disconnection and closing
cancel the delayed start. "Reconnect automatically" separately controls
repeats after the break. Manual disabling stops them in the current session.

The connect button is highlighted in blue, enlarged, and has a visible frame when
keyboard focus. The name changes to "Disconnect" when the session is active.

## Shooting and working with the camera card

On the left in “Camera Settings · SDK”, expand “Photo Recording” or “Video Recording”.
RAW/JPEG/HEIF, JPEG/HEIF formats are available for photos, quality, size, proportions,
RAW compression and stabilization; for video - format, frequency, bitrate/color and stabilization.
These are the settings for recording new files with the camera. Available values and locking
gives SDK taking into account the model/mode. With read-only, the current value remains visible;
an unsupported field shows "Not Available".

First select JPEG/HEIF, then the photo format from the updated list. For video
First select the format, then the frequency and bitrate. After each field change
waiting for dependencies to be re-read; the previous list is not used for the next one
records. The application does not toggle other properties or restore them
automatically when connected. If the camera itself changes dependent values,
the interface shows the resulting state. SDK value names can be
in English; the full signature is available in the tooltip.

The originals are not downloaded upon shutter release. The Auto Sync setting from 0.2/0.3 is no longer
switches on transfer even after reconnection. The CLI retains the same commands
but the GUI copies originals only when explicitly requested in the “In Camera” tab.

1. Shoot a series, open “On camera” and click “Refresh card”. The list contains frames
   from supported slots; the inaccessible card is shown below and in the journal.
   A second slot missing from the camera design is not considered an error.
2. Select a line. By default, the original JPEG is opened, including RAW+JPEG;
   RAW is not transmitted. "Photo: Quick Preview" returns a compressed SDK image.
   Visited frames receive a thumbnail in the list. ←/→ and Page Up/Page Down
   scroll through visible frames, including full screen; search limits the order.
   Quick presses select the last frame requested; loading the previous one
   original is cancelled. 100% refers to the real pixels of the source shown,
   Dimensions are indicated in the signature. For RAW-only, a compressed SDK preview remains until
   "Original · ... MiB" is clearly not pressed. Then RAW is downloaded and the best one is extracted
   embedded JPEG: This is not a manifestation of sensory data. Double click/F11
   expands view, Esc returns.
3. Space switches the “Copy” mark, Delete – “Defect”. Works in the list and
   card viewer, including full screen; pressing again removes the mark.
   In the search field, the keys edit text. Delete does not immediately delete anything.
   For copying also use the first column. “Mark all” and “Mark new”
   take into account the text filter. "Uncheck" clears the first column.
4. Select RAW+JPEG/HEIF, RAW only or JPEG only. "Copy marked"
   processes marks; “Copy all” and “Copy new” - the entire card catalog,
   regardless of the text filter. “Copy all” transfers all files again;
   existing copies are preserved, new ones receive suffixes. "Copy new"
   and “Copy marked” skip confirmed copies. RAW+JPEG can
   Only files of the same format should be new.
5. The destination directory is selected from the top; it can be changed while connected
   camera when there is no operation. The application saves the camera hierarchy under `YYYY-MM-DD/Slot_1/`
   and `YYYY-MM-DD/Slot_2/`, so matching names of different slots do not conflict.

“New” are determined by the journal `.sonshell-imports.json` in the selected directory.
The entry is created only after a successful SDK response, size check and atomic
publications. The presence/size of the local file is checked; deleted or cropped
the file becomes new again. Changing directory or losing a log does not mean that
the files have already been copied. Existing foreign file is not overwritten: copy
will receive the suffix `_1`, `_2`, etc. Quick status does not re-read the content;
file changes while maintaining size are detected by the SHA256 check button.

“Original · ... MiB” opens the selected photo or video in a temporary cache.
Loading and decoding occur in the background; progress and "Stop" are also available
in full screen. Large videos need to be fully downloaded before playing;
The temporary disk should have enough space for the file and a reserve of 64 MiB. In cache
one last original is saved, reopening uses it without a network;
if there is a confirmed imported copy, it opens. When replacing
cache/disabling the temporary original is deleted. Imported files are saved.
Viewing does not create an import record and does not clear the “New” flag: for permanent
save by date, use the copy buttons. When the shutter is released this
the quality setting does not start the download.

Marks have a white background and a dark frame: the copy is highlighted in blue, the defect is highlighted in red.
There is a separate second column for rejected captures. The “Delete rejects…” button shows the number
frames/files and expandable exact list. Deletion is irreversible and affects everything
frame, including RAW+JPEG and related files; local copies are saved. Default
Cancel is selected. Protected frames and deletion not supported by the camera are not available.
If there is an error, the remaining deletions are aborted; update the list and confirm them again.
The frame disappears from the list only after the result is confirmed by the camera.

Updating the list resets the selection marks. The card is updated manually after shooting: the camera notifies you automatically
does not start transfer for itself. After reconnection, the marks are reset, the directory
need to re-read; an incomplete queue is not automatically resumed.

## On computer: gallery and EXIF

The “On your computer · EXIF” tab shows the selected directory and subdirectories;
hidden directories and symlinks are not indexed. New local files appear
after two observations of constant size/time, usually after 1–2 s.
Incomplete JPEGs are not displayed.

JPEG, PNG, ARW, SR2, SRF and DNG are supported. RAW and JPEG with the same name
without extension in one directory are combined. JPEG is preferred. Yes
search by name/path, sort by date/name and JPEG/RAW/RAW+JPEG filters.

Click to select the photo, on the right - the image and available EXIF: camera, lens,
shutter speed, aperture, ISO, focal length, date, orientation, color profile and size.
Missing tags are not made up. Double click or separate window button
open the photo at 100% zoom. ←/→ and Page Up/Page Down scroll through pictures
in the order of the current filter and sorting, including in a separate window and in full screen.

For RAW, available embedded JPEG is extracted via ExifTool. The signature contains
its dimensions; 100% refers to this JPEG and not RAW touch data. If
There is no preview, an explanation is shown. The RAW original is not changed.

## Live View and Studio Mode

Live View is built into the first tab. “Fit”, “100%”, +/− and the wheel changes
scale; the left button moves the image. Full screen - button or F11
with focus in the viewer, return - Esc.

In “Settings → Preview” you can enable “Preview after capture (no RAW)” and
select 2–10 s. When notified by the SDK, the application reads the directory and requests the compressed
previews the last frame, then returns Live View. This mode requires support
previews and notifications of model/firmware; The resolution is determined by the camera. Receipt
Full RAW resolution would require transfer of the original and is therefore not performed.

“Hold Frame” pauses the return, unchecking the checkbox starts the interval again.
Live View continues to be received in the background. Disabling a mode or camera cancels
viewing. Manual copying and opening of local files is not included in the studio screening.
New studio previews are not requested during a card transaction.

Since 0.7.0, the quality list offers "JPEG from card · original size". On camera
need JPEG or RAW+JPEG: after imprinting, only JPEG is transmitted, decoded in the background
and is deleted from the temporary directory after display. RAW is not transmitted, directory
import/gallery is not replenished. Size - the original size of the saved JPEG camera,
rather than an enlarged compressed preview. The source and dimensions are visible in the caption; interval
2-10 sec is counted after frame is received, delay depends on size/network.
With RAW-only, a compressed preview will remain with the caption “JPEG is not on the card.”
The program does not change the camera's recording format. The quality selection is retained.

## Progress and stop copying

Since 0.7.0, SHA256 is calculated before publishing: transfer takes 0–90% of progress,
calculation of the amount 90–99%, completion - 100%. Cancellation also affects the calculation of the amount;
the unfinished file does not appear under the final name.

“Check local copies · SHA256” checks the contents against the amount when importing,
in the background, without a camera and without changing files. The result shows correct,
altered, missing copies and “no reference” for old imports. This is a test
changes on the computer after the transfer, not reconciling with the inaccessible amount of the camera.
Damaged files are not deleted, the standard is not replaced; “Copy new” will allow
get them again with a different name. Fast status uses size/mtime,
explicit checking rereads all content, even at the same size and time.

The queue is saved in the selected directory. After a break/restart, connect
to the previous camera and click “Continue import”: the list will be updated if necessary,
verified copies will be ignored. Changed/missing frames will remain
in a saved queue without transfer. An incomplete file starts over again as a whole.
Cancel saves the remaining plan, the new "Copy..." command replaces it.
Shutter release and removal are not automatically repeated. Accident between publication and
The log entry may leave an additional full copy if you continue.

Progress shows the queue and the current file. The completed queue reaches 100%,
and the result line separately reports the number of errors. Inactive "Stop" when
a completion message means the end of the operation, including when copying is denied. "Stop" clears the remaining
queue and waits for completion/confirmation of cancellation of the active transfer. Completed
copies are saved. If the SDK does not confirm cancellation within 5 seconds, the connection
is restarted so that a late callback is not mistaken for another file.
A deletion already accepted by the camera cannot be canceled with this button.

SDK writes to `.sonshell-incoming/` on the same file system. After Result_OK
the original is checked for size and published atomically, without rewriting. Error
and cancel do not leave a partial file under the final name. After an emergency
completion there may be hidden parts left; the gallery does not show them. Byte-range
resumption is unavailable: after a break, use “Continue import” or “Copy new”.

Do not attach settings file or fingerprints to public bug reports.
Help is built into the program: Help menu, F1 (including full screen).
“Settings → Language” selects Russian or English from the next launch.
This saves the current connection and selection. More details - LOCALIZATION.md.

## SDK options and AF point

In “Exposure” ISO, shutter speed, aperture and EV correction have rotary controls. Drag a dial with the
mouse or use the wheel: the steps correspond to the available camera values.
When dragging, the value is sent upon release; when scrolling, the value is sent
250 ms after the last step. ISO AUTO is at the beginning of the ISO scale.
You can select a value from the list or enter it manually and press Enter/Set;
For fractional numbers, a dot and a comma are suitable, for shutter speed - `1/125` or seconds
with the suffix `s`. `AUTO`, `f/4`, `-0,3 EV` are supported. The value should
be present in the camera list; unsupported input is not sent.
Esc cancels the entry. “Read Options” cancels the draft and reads the camera.
The fields show the current values, updated after the callback SDK and application;
auto-update is delayed during editing. Not available in current
mode/lens setting, disabling and card operations block control.
To change the exposure mode, use the camera switch.

The Camera Options SDK block reads focus mode, AF area, white balance and
pulling mode. The camera provides options; inaccessible or read only
options are disabled. After the change, the application rereads the value, failure
camera remains visible. "Read Settings" updates them manually.

For the AF point, turn on Live View, select a moving focus area (for example Spot)
and Click-to-Autofocus. Clicking sets the frame center and triggers AF, dragging moves
image. Fields are ignored; boundaries are checked against the SDK frame data, so
Failure is possible at the edge of the frame. During studio photo display or card operation
Mouse AF is disabled. After setting the point, the application makes a half-press S1
for a second and lets him go, without taking a picture; The Autofocus button also remains available.
In MF or when S1 is not available, the AF click is disabled. Launch error after successful
moving the point is visible separately. Accepting a command does not confirm sharpness.
The program does not change the exposure mode or focus switch for AF.

AF behavior, orientation and property availability need to be tested on the physical
camera. RAW development, other SDK parameters and optimizations are planned later.

## Video on the card and locally (0.6)

MP4s are no longer hidden by the photo filter. “Photo and Video”, “Photo Only” and
“Video only” select recordings; video recording includes returned SDK proxies,
XML and other related files. "RAW Only"/"JPEG Only" select formats
photo. Viewing a video string only asks for a thumbnail. "Play" loads
the original is stored in a temporary cache and starts viewing itself after downloading. Size visible
on the “Original” button, progress and cancellation are available. Replay uses
cache or confirmed imported copy, without new transfer. When descending
originals are not loaded. There is a pause, “Stop”, jump to second and full screen;
preview without sound, in original resolution and frequency. The file and sound do not change.
SDK stream without MP4 download is selected in “Settings → Preview → Video on card”.
Its resolution is set by the camera; Support depends on camera/firmware/network.

Space/Delete works in video and full screen. The transparent overlay at the top left shows
both marks and name; a light inscription at the bottom left confirms the action or reason
refusal. The shadow of the letters maintains readability on a light background; the center of the frame is free.
“Delete rejects…” deletes the entire video with all recording files after confirmation.
To copy and delete, you need to stop the thread. Changing line/tab stops
viewing. Live View stops for a while, then returns if it was enabled.

Local gallery includes MP4/MOV/M4V. The first frame and metadata are read in the background,
viewing is activated explicitly by a button; pause, stop, go to second and fullscreen
available in the general viewer. Photos and videos of the same name are not combined.
Local playback and the first frame retain the original resolution; player
does not limit the frame rate and does not compress the image in JPEG (0.6.1).
The full path of the selected file is shown at the bottom of the main window, available for
selection/copy and in the tooltip; a long way can be scrolled.

If the SDK controls the video, but there is no video data, check the incoming TCP 55001,
55003, 55005 **from camera to computer**, separate from the SSH control connection.
The error shows addresses; UFW rules and rollback are described in [INSTALL.md](INSTALL.md).

## Subdirectories of shooting dates (0.6)

For new copies the structure is as follows:

```
Selected_directory/
  2026-10-23/
    Slot_1/
      DCIM/100MSDCF/DSC0001.ARW
      DCIM/100MSDCF/DSC0001.JPG
      PRIVATE/M4ROOT/CLIP/C0001.MP4
      PRIVATE/M4ROOT/CLIP/C0001M01.XML
```

The camera's creationDatetimeLocaltime is used. If it is incorrect, it is taken
valid UTC date; without both dates - Unknown-Date. The current date of the computer is not
used. The hierarchy on the card and slots are stored inside the date. Existing file
is not overwritten. Old copies from the Slot_N structure remain in place and continue
be taken into account by the journal; the update does not lead to their re-downloading via “New”.
Long transfer of a large video with the callback SDK is no longer limited by the general
lasting 10 minutes; No notifications for 3 minutes causes cancellation.

Connection, auto-connect, directory and studio viewing parameters are located
in the Settings menu and are saved automatically. "Connection log" in this menu
shows hidden log; The hold button appears in Live View for the duration of the frame.
The SDK stream remains the choice of “Settings → Preview → Video on card”.
Some camera and video combinations transmit only sound during successful network
connections; Start/Play does not prove the presence of video frames. View original
bypasses this SDK path and preserves the original resolution and frame rate.

## Viewer navigation and loading (0.8.3)

"Previous File" and "Next File" select adjacent visible files of the card, gallery and individual
windows, including fullscreen. Search/filter taken into account; transition at the edges
stops. The current video stops playing; next one starts
by Play. Moving to another file while the original is loading cancels it.

In the card viewer, a loading bar is visible next to “Stop”, including
on full screen. The percentages and queue counter coincide with the main indicator;
after completion/cancellation, the bar disappears. This is the loading/operation indicator;
video is controlled by a separate panel.

“Back 5 s” / “Forward 5 s” rewinds the current video from the position
playback For originals and local videos, the beginning and end are taken into account
file; rewind on pause shows the selected frame and saves the pause.
Repeated presses add up. Current time and duration are visible side by side
with the exact transition field in seconds. The panel is available in the gallery, on the card,
in a separate window and fullscreen. While the duration is being read, rewinding is inactive.
In the optional SDK stream, the position is estimated based on the decoded timing
marks, and the transition is requested from the camera with an accuracy of up to a second.

## Default language (0.8.4)

English is the default for a new installation. Settings → Language offers Russian
for the next launch. Existing saved language preferences are retained; missing or
invalid values fall back to English.
