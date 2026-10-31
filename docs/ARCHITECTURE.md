[English](ARCHITECTURE.md) · [Russian](ARCHITECTURE_RU.md)

# Architecture and solutions

## 2026-10-19: separation of GUI from SDK

SonShell remains a separate process. Qt does not depend on proprietary headers,
An SDK failure does not terminate the GUI. The GUI manages reconnection.
A running backend process does not imply an established camera connection:
actions become available only after backend events confirm it. In 0.2/0.3 after restoration
Auto Sync and Live View were reapplied; Solution 0.4 below overrides Auto Sync in the GUI.

GUI: MainWindow (interface), CameraSession (process and states), SettingsStore
(settings), ImageViewer (zoom/pan), PhotoLibrary (ready-made JPEGs).
Modules are tested without SDK.

Built-in Live View: backend receives JPEG from SDK and publishes with atomic rename
the last frame in the private temporary directory. Qt only reads the finished file,
does not embed an OpenCV/X11 window. Headless disables the standalone
OpenCV viewer; embedded GUI Live View remains available. Frames are not accumulated. The protocol is in PROTOCOL.md.

## Credentials

The checkbox enables saving both credential fields. Otherwise they remain in
memory until the window closes, including for reconnection. Unchecking the checkbox immediately deletes the saved content.
The new project uses separate settings. The file has permissions 0600, directory 0700;
There is no encryption on the disk, the checkbox hint explains this. The password is not included in argv
and log: backend gets environment, reads and deletes variable before SDK Init.
Keyring is planned separately.

## 2026-10-19: installation on clean Ubuntu

DEB package includes private backend and runtime SDK, Qt and system libraries
installs apt. ELF dependencies calculated by CPack/dpkg-shlibdeps, dynamic
Qt platform/image plugins are listed separately. Install RUNPATH uses $ORIGIN,
without absolute SDK paths. The target of the first build is Ubuntu 24.04 amd64. Other Ubuntu
and ARM require their own packages and check matrix. Before the first connection
The GUI shows the terms and conditions of the Sony SDK, including the consents required by Sony; full HTML
and OSS notices are installed with the application.

SDK libmonitor_protocol.so loads the neighboring libmonitor_protocol_pf.so and does not have
own RUNPATH. CameraSession sets LD_LIBRARY_PATH only to the child backend
(its directory and CrAdapter), then saves the custom part of the path. This is also
covers transitive dlopen without changing vendor binaries and /etc/ld.so.conf.

## 2026-10-21: media library and studio shooting

For EXIF/embedded RAW JPEG, the system ExifTool is used as a separate process
instead of the previously proposed Exiv2. This maintains the GUI/SDK dependency boundary,
provides support for Sony MakerNotes and does not require the new C++ ABI. The process only reads
specified tags, without shell and custom ExifTool config. Timeout, cancellation and
result limit limits work on the damaged file. For RAW obviously
the dimensions of the embedded JPEG are shown; Sensor-data RAW development is not implemented.

Indexing and decoding are done in background Qt tasks. The result contains
catalog/query generation: the late result does not override the selected snapshot.
The gallery combines RAW/JPEG in only one directory and with one name without extension.
Thumbnail cache is limited; full originals are not stored in the cache.

The download goes to hidden staging on the same file system, the finished file
rename is published after SDK Result_OK. Only after publication the GUI receives
event with path; studio timer starts after decoding the original
JPEG (or clearly labeled embedded RAW preview), and Sync All does not include studio preview.

QObject children live longer than C++ window fields. Before destruction MainWindow disables
calls child objects back to itself, then stops the timers and session. Destructor
CameraSession disables QProcess handlers before waitForFinished because
QProcess synchronously emits finished even while waiting in the destructor.
Otherwise, the callback refers to the already destroyed list of actions/StudioReview.
This case was found by AddressSanitizer when closing the gallery window.


## 2026-10-21: separate shooting from working with the card (0.4)

The previous decision to transmit the original prior to studio screening resulted in
large RAW transfers on every shutter release. Auto Sync has been removed from the GUI; old meaning
settings are ignored and deleted when saving. After connection it is sent
`sync off`. The CLI retains the previous sync commands, but in `--gui` they are rejected,
except `sync off`; playback-hook and ContentsListChanged do not trigger the download.

`CameraCard` owns the remote directory and GUI sequential queue;
`CameraCardWidget` - selection, separate defect marks and viewing.
`CameraBrowser` in the backend has one worker, a snapshot of the directory and a separate
callbacks state. The preview uses GetRemoteTransferContentsCompressedDataFile
(Screennail), the original is only an explicit GetRemoteTransferContentsDataFile.
The absence of a preview is never replaced by the original. Preview cache is private for
session, cleared when the process starts; The GUI holds up to 64 MiB of images.
Preview decoding is done in Qt Concurrent with size checking.

The list contains individual SDK contentId/slot, files and sizes. Every action
has request UUID and directory epoch: old result is not applied after
reconnection. Notifications mark the directory as out of date. Before deletion
the slot is read again and the identity, protectionStatus and current are checked
DeleteContentOperationEnableStatus. Before transfer, when changing the card also
identity is checked. Removal applies to all content, including RAW+JPEG
and related files. Success means OnWarningExt CrWarningExt_DeleteContent with OK,
matching slot/contentId, not just a successful API return.

Originals and previews are serialized by one mutex: SDK does not return request ID in
transfer callback. Cancel holds the transfer until the terminal callback; no answer
in 5 seconds the backend marks the channel as unusable and the GUI restarts the process.
When closing, the worker stops before the ReleaseDevice. Deleting with unconfirmed
the result stops the queue; replay requires updating and confirmation.

The `.sonshell-imports.json` log is stored separately in each destination directory
and is written via QSaveFile (0600) only after SDK OK, length check and
publications. Key: SHA256 from identity metadata (serial, if the SDK provides it,
else model+host; slot, creation UTC, file paths/sizes), rather than a hash of the content.
Changing the rating and unstable contentId do not turn the copy into a new one. "New"
checks the recording and file size; Cryptographic reconciliation remains on track.
A card after formatting and files with completely matching metadata may
require explicit selection of another directory/reconciliation; this is not proof of equality.

The copy directory is divided into slots, SDK paths are cleared of traversal; symlinks
along the way, destinations are rejected. A name collision is given a suffix. Publication
uses Linux renameat2(RENAME_NOREPLACE), eliminating overwriting even during a race.

Studio viewing by a separate checkbox receives only a compressed preview of the latest
frame and clearly shows the source. Manual import does not intercept Live View.
This is a conscious change in the requirement for full permission in favor of shooting without mandatory
RAW transfers; full-size PostView/JPEG is considered separately.

A full "all" import explicitly repeats the transfer of all selected formats and saves
old files under the same names; new ones receive suffixes. "New" and "Checked"
use verified local copies. This allows you to re-get
originals without manually deleting the log and without overwriting existing files.

Viewing in QSplitter preserves the index and dimensions of the panels before fullscreen.
When returning, WindowFullScreen is reset, the widget is inserted in its original place;
MainWindow explicitly returns all viewers before closing and destroying fields.
This is also checked when directly closing the window in fullscreen.


## 2026-10-21: fixes for verification 0.4 on the camera

In running GUI 0.4 it was discovered: a queue of two files ended with two
errors “Unsafe path on the card”, but the indicator remained at 50%. Backend not
transferred the originals: the path conversion took place before the SDK transfer. It was
termination with error rather than active transfer; “Stop” naturally turned off.

Sony paths can contain volume namespaces and back separators. Namespace
the first component with a colon is removed, delimiters are normalized, colons
in the remaining names are encoded; transitions `..` and destination symlinks are still
are rejected. All local components remain under the selected /Slot_N directory.

Progress: completed - number of files already processed, percent - percentage only
current unfinished file. After increasing completed, percent=0 is transmitted.
The widget limits the value to a range and does not add the current percentage when
completed=total. The old 150/200% was rejected by QProgressBar, leaving the old 50%.
Errors from each operation are additionally logged.

MediaSLOT2_Status is not available on models without a second slot (according to API reference).
The presence of a slot is determined by a readable status property or an allowed list,
and not the absence of a card/zero enable status. Unsupported second slot
skipped; a supported but not ready slot continues to report the problem.

ImageViewer provides navigation signals; the source of order remains the card list
or proxy local gallery. Navigation is enabled only for photo viewers,
Live View retains its keys. Shortcuts belong to ImageViewer itself,
therefore, they take effect after switching to a separate window/fullscreen. When loading a preview
the list remains available; The last request is saved, the outdated preview is not
replaces the current line. Copy/delete continues to block selection changes.

Two columns of marks are drawn by the local delegate: a 22 px white square with a dark border,
blue/red fill and white check mark. The size of the click area matches the rendering;
The system theme and global settings do not change.

## 2026-10-21: keys, help, languages and SDK options (0.5)

Space/Delete belong to the list and the ImageViewer itself, so they continue to work
after separating fullscreen. ImageViewer only asks for a checkmark; CameraCardWidget
checks the current frame, protection, delete availability and occupancy. Delete toggles
second column; calling the uninstall SDK is only possible through the previous confirmation dialog.
Shortcuts do not intercept search input, Live View, or local gallery. Auto repeat
marks is disabled, standard Space-switching delegate is removed to avoid
depending on the current cell/double switching.

Help - modeless QTextBrowser with Russian/English Markdown in Qt resources;
F1 is also available from fullscreen. External links can only be opened by clicking on them.
GUI translation - standard QCoreApplication::translate, TS/QM and QTranslator.
The initial language is Russian, the initial choice is Russian; English catalog is embedded
via lrelease and RCC. Qt standard dialogs use qtbase_ru from the runtime package
qt6-translations-l10n. The language is saved and applied upon new startup so that
Do not recreate the window, session and selection during the operation. Help uses language
current interface. Adding languages: docs/LOCALIZATION.md.

CameraControlsWidget is separate from the window; REMOTE/1 gets supported options
from the SDK, and not a fixed set for the α6700. Backend camera_controls.cpp - separate
module without Qt. The SDK pointer belongs to the REPL, the commands are executed sequentially
under the previous mutex. Callback reports a change; GUI merges reads and defers
them for the duration of the card transaction. List enum - GetValues/GetValueSize(GetSetValues
reserved in 2.02); each set re-checks availability and validity
value, the result of the SDK is explicitly passed. After the result, the camera is read again;
if it fails, the value is returned, the error message remains. The old CLI is preserved.

Clicking in Live View is optional; dragging continues panning. Coordinates
taken from the actual QGraphicsView transformation, fields are excluded; separate
There is no SDK JPEG rotation. The backend additionally checks the AF frame against the LiveViewProperty.
Inaccessible metadata disables the feature. During the studio screening of the frame, transfer,
waiting for a command and when communication is lost, AF is disabled. Selecting an area is not displayed as
confirmed sharpness: half-press remains a separate action. Settings
The exposure/physical mode of the camera does not automatically change for AF.

Primary sources: local API Reference 2.02 `dp_AF_Area_Position.html`,
`device_property.html` and example CameraDevice::execute_pos_xy from installed
[Sony SDK](https://support.d-imaging.sony.co.jp/app/sdk/en/),
[QTranslator](https://doc.qt.io/qt-6/qtranslator.html) and
[Qt Linguist](https://doc.qt.io/qt-6/qtlinguist-index.html).
SDK headers and documentation have not been added to Git. Context7 did not provide
Sony SDK documentation, so the contract was verified against the official local kit.

## 2026-10-23: selection in fullscreen, video and dates (0.6)

The previous directory discarded content without RAW/JPEG/HEIF, so MP4s were missing
despite CrGetContentsInfoListType_All. MP4s are now included with files
records; the thumbnail goes through Thumbnail, since Screennail for videos is prohibited
SDK contract. Studio View selects only photos. Photo/video filter
works at the recording level, RAW/JPEG - at the file level.

ImageView places QLabel overlays at viewport coordinates, on top of the image,
without participation in zoom/pan. The constant line shows the name and both marks; separate
a 2.3 s message confirms the action or explains the refusal. Both overlays are plain
text, with a contrasting background and without mouse interception. New selected row clears
past notification, when loading a preview, the previous photo is removed: not allowed
assign a mark to the new frame while continuing to show the old image.

The directory date is taken from the SDK local creation time, if necessary UTC,
otherwise Unknown-Date. The generic capture_date.h checks the calendar date. Identity and
ledger keys have not changed: the update does not force the old import to be repeated and does not
moves personal files. The slots and original hierarchy are preserved within the date.

MovieStream is an SDK module without Qt that lives in CameraBrowser. Worker owns lifecycle,
SDK callback only passes a copy of the data/state through limited queues.
FFmpeg is moved to a separate process: local MP4/MOV/M4V and SDK stream use
general VideoPreview. Private QLocalServer/AF_UNIX channel with back pressure
separates binary video bytes from the text protocol. SDK stream is not published
under the name of the original, secretly copy heavy MP4 for viewing the application is not
trying. Stop is confirmed before the SDK operation is released; failure leads to reconnect.
During movie, conflicting camera buttons are disabled; the old Live View is restored.

VideoPreview displays frames in their original size/frequency without sound. Removed from 0.6.1
scale/fps and MJPEG transcoding: FFmpeg outputs RGB24 in binary PPM, Qt parses
short title and copies the completed frame into the QImage. Frame limit - 128 MiB,
the size is checked before the image is created. For a local file, realtime filter
sets the tempo according to the PTS of decoded frames: the input -re gave an initial jerk when
negative DTS/pre-roll short clip. The clock correction limit of 0.2 s allows
resume after SIGSTOP/SIGCONT without a long accelerated catch-up. None
the entire received frame is not discarded by the parser.
The first frame for the gallery is a PNG of the original size, the thumbnails are reduced separately.
The play/pause/stop/seek buttons are included in the ImageViewer and remain in fullscreen along with
selection keys. The local gallery receives the video separately from the photo with the same
name, the first frame/metadata is read in the background, playback is enabled explicitly.
External FFmpeg - DEB system dependency; The GUI/SDK does not link with its libraries.

Primary sources: API Reference Sony SDK 2.02,
`class/contents_transfer_with_remote_control.html` (MP4, Thumbnail/Screennail),
`op_flow_and_seq/op_movie_playback.html`, `api_reference/movie_playback.html`,
`function_list/api_list.html` (SetMoviePlaybackSetting - ILCE-6700). SDK documentation
and no libraries were added to Git. The streaming option is implemented under a contract;
verification on the physical camera remains separate from synthetic H.265.

In 0.6.1, hardware diagnostics detected a UFW BLOCK of incoming TCP from the camera to
55001: Successful Start/Play and StatusChanged did not mean receiving video. Empty
the stream now gives a structured error movie-no-data with addresses; GUI translates
it and indicates the ports/help. FFmpeg only starts with the first bytes, so
The empty input error does not mask the SDK/network reason. Normal ending finishes reading
callback queue until the socket is closed. Preparation is limited to 20 s, the rest are canceled
remain interruptible; Stop is confirmed until the next operation. Both processed
callback OnWarning/OnWarningExt option. The installation is not controlled by the firewall.

The path in QStatusBar is a constant read-only field: for a local file it is absolute,
for the card the original filePath SDK without temporary substitution of the thumbnail. Field
updated by selection/filter/tab, allows you to select and copy the path.

Hardware check 0.6.1 after negotiated UFW address rule confirmed
H.265 over the same private socket: 1280x720 from SDK, pause/resume/seek/StopComplete,
return JPEG Live View 1024x576. The application does not increase the resolution of the camera preview;
the panel shows the actual size of the QImage. Imported test
original - 1920×1080/50 fps; size and all 144 frames are saved when viewing locally.


## 2026-10-23: launch AF after selecting a point (0.6.2)

Before 0.6.2 controls af changed only AF_Area_Position. Now the backend executes
single sequential request: validation of coordinates/mode/S1 → setting a point →
S1 Locked for 1 s → Unlocked. Failing to set a point does not trigger AF. HalfPress
The RAII guard also attempts to release S1 after a rejected press and reports a release failure.
Applied is issued after releasing the half-press; the sharpness flag is not invented.

Snapshot af takes into account autofocus mode and IsSetEnableCurrentValue
for S1. MF/PF, no metadata, Live View, card busy and pending request
disable click. S1 is read as a utility feature, not included in the generic set
or four parameter lists. CLI focus saved. In RU/EN UI and help the action
called “Click to focus”; partial failure after moving the point is visible separately.

Primary sources: local Sony SDK 2.02 dp_AF_Area_Position.html and dp_S1.html;
Excerpt S1 uses the existing SonShell focus command behavior.

## 2026-10-24: exposure from camera capabilities (0.6.3)

Four static MainWindow fields are replaced by ExposureControl within a common one
CameraControlsWidget. All eight properties share the same serialized channel
controls with UUID, deadline and update after callback/applied/error; parallel
There are no exposure or AF requests. During card operations (including Movie Playback)
control is not available. CLI exposure remains compatible.

ISO/ShutterSpeed/FNumber/ExposureBiasCompensation are read by one GetSelect;
GetValues ​​determines the available steps, not a reference table of photographic stops.
Int16Array is read bit by bit as uint16 wire, EV sign is restored when written
and formatting. The current EV is normalized to 16 bits. Snapshot adds current_label
for the current value outside the write list. The exposure SDK set uses scalar
CLI-style types. Each entry re-reads the possibilities, so changing the mode
between snapshot and set does not make do with the legacy GUI list.

ExposureControl sorts real values: ISO AUTO before numeric ISOs,
shutter speed by seconds, aperture by f-number and EV with a sign. QDial changes index
choice; the typed value is mapped to the camera options display,
and is not calculated by rounding into arbitrary SDK code. RAW saved without import
after shutter release and independent viewing of the card.

When dragging, the display follows the pointer, the recording goes to release; wheel/keys
combined with a single-shot timer of 250 ms. Camera shot does not overwrite draft
during editing; Esc/manual reading cancels the entry. Shutdown/busy
stop the timer. Fields have a minimum text length, captions above the line:
AUTO and fractional shutter speed fit into a narrow panel without being compressed into a couple of characters.

Sources: local Sony SDK 2.02 dp_IsoSensitivity.html, dp_ShutterSpeed.html,
dp_FNumber.html, dp_ExposureBiasCompensation.html;
[Qt QDial](https://doc.qt.io/qt-6/qdial.html) (drag/release, wheel and tracking).

## 2026-10-24: reliable import and studio JPEG (0.7.0)

After SDK Result_OK, the backend checks the size of the staged file and reads it in 1 MiB blocks
and calculates SHA256 via OpenSSL EVP in worker CameraBrowser. Cancellation is verified
between blocks; The file is not published until hashing is completed. Error/cancellation deleted
staged data. Atomic rename remains the last step. copied contains sha256;
The GUI writes the amount, size and mtime to the ledger. This is the standard of the received copy,
rather than checking against an inaccessible camera hash. New build dependency - libssl-dev;
runtime libcrypto defines DEB. Qt is not added to the backend.

"Check local copies" reads files in QtConcurrent promise-mode: bounds
directory, size, SHA256, read immutability. QFutureWatcher reports progress,
QPromise cancels work between blocks, no camera needed. The mismatched standard is not
replaced by: invalid in ledger includes the file in “New” again. Re-import
saves the old copy and creates a new one with the suffix. Old ledger without amount
compatible; the check shows “without a standard”, without creating a fictitious historical
hash. Size/mtime serves as a quick status, explicit check rereads the contents.
The copied frame status updates one row without recreating the QTreeWidget.

Before the transfer starts, the private .sonshell-copy-queue.json is atomically saved:
identity, slot, path, size, format, ledger key. Successful ledger entry
precedes the removal of a task from the queue. Break/cancel/restart leave
unfinished entries. Explicit "Continue Import" rereads the directory and matches
stable identity, rather than the previous content-id/epoch. Unknown footage remains
in a queue without transfer. Shutter release and removal are not repeated. New copy command
replaces the previous plan. RemoteTransfer 2.02 does not give offset/range: incomplete file
is transmitted again. A crash after publish before a ledger entry can leave
additional full copy when repeating; no overwriting. JSON is limited to 32 MiB.

Studio quality: default compressed preview or original JPEG from the card.
In the second case, only JPEG is selected, including RAW+JPEG recording. preview...jpeg
calls DataFile only for JPEGs up to 128 MiB, in private camera-cache; RAW/video
are rejected before the SDK. After decoding, the JPEG is removed and is not included in the import,
ledger or gallery. With RAW-only - Screennail with an explicit signature about the absence of JPEG.
The source and dimensions are visible in the frame. The 2–10 s interval begins after the display;
Latency depends on the network. Image limit 120 MP, Qt limit 512 MiB; obviously
the specified QT_IMAGEIO_MAXALLOC retains priority. Reading the last frame for now
uses a directory; a restricted request requires SDK semantics/order checking.

PostView RAM investigated but not enabled: API 2.02 requires SetSaveInfo,
StillImageStoreDestination HostPC/HostPCAndMemoryCard and EnablePostView.
Switching would return the auto-transfer of the originals. The camera's recording mode does not change.

New frame notifications during busy times are saved as pending and merged
timer 500 ms; after the operation, the last frame is requested. Last shown
identity excludes repeating callback duplicates. Directory changed while reading
re-read after a pause. The studio frame passes the original SDK path to the bottom
string; When you return to Live View, the path is cleared.

Sources: local API Reference 2.02 api_reference/pull_postview_image.html,
op_flow_and_seq/op_pull_postview_image.html, contents_transfer_with_remote_control.html;
[QtConcurrent](https://doc.qt.io/qt-6/qtconcurrentrun.html),
[OpenSSL EVP](https://github.com/openssl/openssl/blob/master/demos/digest/EVP_MD_demo.c).

## 0.7.1: quality of viewing saved files

The quality of the card tab is separated from the studio and Live View. Default line selection
requests the original JPEG (for RAW+JPEG only JPEG); fast mode saves
Screennail/Thumbnail. RAW-only and MP4 are loaded exclusively with the original button
with size, without turning on automatic transfer when shooting. Movie Playback
does not allow selection of size/bitrate in CrMoviePlaybackSetting 2.02; for the original
MP4 quality requires a DataFile and a local decoder.

CameraCard original uses a separate GUI task and preview... original
in the protocol. Copy/ledger/stored queue is not affected. Transfer
checks the size and publishes atomically; MediaReader decoding goes to
QtConcurrent, cancellation is carried by an atomic token. JPEG is limited to 120 MP/128 MiB,
RAW shows the best embedded JPEG, MP4 - frames of the original size and frequency.

The cache holds one last original (file and decoded frame); during
new transfer, a second staged file is possible. Before transfer you need a free
The size of the new file is +64 MiB, so the previous file does not interfere with canceling the replacement.
The cache key includes a stable identity/path/size. Confirmed imported path
used directly; ownership of the temporary file is kept separate from the path.
The new original is first transferred to the viewer, then the previous one is deleted
temporary file. Changing the line/tab stops the player. Shutdown
first clears the viewer, then the cache; imported files are not deleted.
After a canceled/deprecated request, no results are shown, temporary file
is deleted. The full-size image does not fit into the limited compressed QCache
preview: It's held by the current result, so the 26 MP doesn't preempt itself.
The quality, size and undo tools are inside ImageViewer and remain
available after moving it to a full-screen window.

## 2026-10-25: starting connection and recording parameters (0.8)

AppSettings.auto_connect is stored separately from auto_reconnect and defaults to true.
MainWindow runs a one-time QTimer 250ms after the UI/signals are initialized.
Remember_credentials, non-empty saved name/password and accepted ones are checked
SDK terms. Without them there is a modeless explanation; the user connects manually.
In the automatic path, invalid IPv4/model/directory does not open the QMessageBox.
Hand button, uncheck and close/destructor stop the timer; further
attempts are made by the old CameraSession. The button is highlighted by local QSS by objectName,
without changing the styles of other actions. Auto-connection rollback—uncheck the box in the window.

Registry camera_controls has been updated with 11 entry names. SDK queried choices
extended UInt8Array; enum ValueType is preserved when recording, exposure remains
your previously tested scalar types. Backend re-queries properties
before the setter and does not accept code outside of an explicit list. Recording Destination Settings
HostPC is not included in the list. The GUI creates only the explicitly selected entry; dependent
values are reread after Applied/error while holding the awaiting_snapshot lock.
It also rejects already queued selection events between Applied
and Snapshot. Manual updating allows you to recover from a read failure.

Photo/video are presented in separate collapsible sections. Missing
property/choices remains disabled, read-only current_label is displayed without
substitution of an unexplained number. The names of the technical SDK values remain
in English, field names and help are translated. gen_recording_labels.py
builds enum signatures from local CrDeviceProperty.h; generated header
lives in build/gen and is not added to Git. The GUI does not depend on SDK headers.

Reason: local API Reference 2.02 dp_FileType, dp_CompressionFileFormatStill,
dp_StillImageQuality, dp_ImageSize, dp_AspectRatio, dp_RAW_FileCompressionType,
dp_Movie_File_Format, dp_Movie_Recording_FrameRateSetting, dp_Movie_Recording_Setting,
dp_ImageStabilizationSteadyShot, dp_Movie_ImageStabilizationSteadyShot;
the setter type was checked against the official app/CameraDevice.cpp Sony SDK 2.02.

Video stream checking uses a continuous QEventLoop with a separate deadline,
as a working viewer. QTRY/qWait pauses between readyRead portions and when
142 MiB RGB for a second 1080p/24 can artificially fill the pipe, giving
false timeout. Checks of the original 24 frames, pixels and tempo are preserved;
20 iterations completed on local FFmpeg. The production VideoPreview has not changed.

## 2026-10-25: viewing videos and Settings (0.8.1)

On the ILCE-6700, two existing movies completed the MoviePlayback SDK with audio, but
no video footage. The route only bind/listen/accept/connect showed successful
connecting the camera to all three incoming TCP ports; this is not a repeat of earlier
confirmed UFW BLOCK. The reason for the missing frames inside the camera/SDK is not
localized. The application does not change network rules and recording formats.

Main Play now uses original DataFile and local FFmpeg:explicit
click means loading, the size is visible before launching on the original button. Receipt
original_ready schedules to run on the next event cycle after busy=false.
The autoplay key is tied to the selected entry, cleared when canceling, changing the line,
deactivation or refusal. Reopening uses the old cache/validated import;
no new automatic transfers after shutter release. SDK stream kept separate
choice, its empty result suggests the original and does not assert network blocking.
The protocol and CLI have not changed; Pause/Stop/seek of local VideoPreview is the same.

Persistent QDialog with three pages (connection, directory/files, preview)
belongs to MainWindow and is shown via show() without blocking the SDK event loop.
The same editors and handlers are used, without a second copy of the password or
condition. Values ​​are saved immediately/when editing is completed and closed.
Access fields are disabled when the process is active, directory is disabled when the card is busy.
Card quality settings have also been moved to the dialog and saved separately from
studio JPEG. Old ini continues to be read with the same default values,
For video, the original is selected by default. Main heading: status, Settings,
Connect. The log is hidden, accessible via the Settings action; studio retention
frame is shown in Live View only while the frame is active.

QLabel overlays remain children of the viewport and allow the mouse to pass through. Transparent background,
regular font 13px, light letters with a small shadow QGraphicsDropShadowEffect;
width is limited to 560px and viewport size. Check-ins top left, notifications
bottom left, including moving the viewer to a full screen window.

Qt base: [modeless QDialog](https://doc.qt.io/qt-6.5/qdialog.html),
[QGraphicsDropShadowEffect](https://doc.qt.io/qt-6.5/qgraphicsdropshadoweffect.html).

## 0.8.2: project information

A separate AboutDialog is opened via show(), like Help/Settings, and not
puts a modal event loop into camera control. MainWindow holds
QPointer and picks up an already open window, WA_DeleteOnClose releases it.
The main closing closes About.

The NDRco icon from the GitHub profile is included as a PNG in :/branding/ndrco.png.
There are no network calls when the window is opened. QLabel with links allows navigation
mouse/keyboard via QDesktopServices; URLs are constant, credentials are in them
are not used. The texts are included in the existing RU/EN catalogue. The version is taken
from QCoreApplication::applicationVersion(), as in the main executable.

Planned source address: https://github.com/ndrco/SonShell-Remote.
Verified gh 2.45.0 is already installed locally and authorized as ndrco via
keyring. Repo view does not resolve this address yet; the repository was not created
and the code was not sent. Installing tools and changing global settings
were not required.

## 0.8.3: navigation buttons and progress fullscreen

ImageViewer contains two adjacent file buttons in the signature line. They send the former
navigate_requested(−1/+1), like existing keys; set_navigation_enabled
controls their visibility so Live View doesn't get navigation. Card list
and gallery proxy model preserve order/filters and limit transition to edges,
without cycling. The general viewer is also used by a separate window.
The transition does not trigger automatic transfer of the next original or autoplay.

CameraCardWidget places an additional QProgressBar inside preview_tools,
child QWidget of ImageViewer. The panel is transferred along with the viewer when
setParent(..., Qt::Window); no new top-level overlays/progress timers.
The indicator uses the same calculation and queue format from progress_changed as
main strip. It is visible only when busy without SDK movie and is hidden after
busy=false. The cancel button remains nearby and is available in full screen.

request_preview postpones the new selected entry and causes cancellation not only
with SDK movie, but also with loading_original. After confirmation busy=false
the previous deferred request for the last selected key is executed. This is not
holds a heavy transfer when the user has already moved to another file,
and retains existing autoplay protection against stale results.

The video panel is separated by ±5 s increments from adjacent files. VideoPreview reads
machine `-progress pipe:2` FFmpeg every 0.1 s; position is equal to out_time_us plus
offset of the original `-ss`. After seek, the position is updated immediately so that fast
the clicks added up. The SDK stream uses the same decoder scale with a new one
prop after the seek request; The SDK takes whole seconds. This is a position assessment
and not the timecode received from the camera. The backend and protocol have not changed.

For a local file, ffprobe asynchronously reads duration/avg_frame_rate in JSON
(5 s, output ≤64 KiB, file/pipe only). The old process ends when changed
source, its signals are blocked; Before reading the duration, the seek buttons are turned off.
The bounds are limited to zero and the last frame interval. No additional downloads
resize/re-encode or frame rate changes. When rewinding while paused, the process
resumes for one frame and stops again; re-seek
maintains the pause intent even before the first frame arrives. Absolute jump field
uses the same path. Metadata/decoding does not trigger gallery import.

## Publication of sources on GitHub - 2026-10-31

At the owner's command, a public ndrco/SonShell-Remote was created, the main branch was main.
SonShell history and subsequent GUI history are preserved without rewriting.
Git publishes sources, documentation, NDRco and a permitted UI screenshot;
SDK/runtime, binary packages, custom snapshots and customizations are excluded.
Screenshot - direct capture of MainWindow with SDK-free backend and own vector
test scene. In the README it is clearly labeled as a demonstration without a camera.
The application and protocol have not changed. The results are in TESTING.md.

## 0.8.4: English source language and Russian retention

English literals are now the stable GUI translation keys. The existing English
translations were inverted into `sonshell_RU.ts`, preserving Russian wording and
placeholders. Two formerly ambiguous English labels use distinct keys: Stop
transfer/Stop and Photo folder/Photo directory. The Russian QM is loaded only
for Russian; the English UI uses source text. Settings retain a valid saved
language, while new or invalid settings use English. Backend diagnostics and
source comments use English without changing protocol keys or command syntax.

Help is stored in HELP.md/HELP_RU.md with unchanged en.md/ru.md resource aliases.
Documentation has English primary files and retained _RU files. Desktop metadata
is authored separately in English and _RU, then combined into one installed
launcher with a localized Comment[ru]. The DEB installs both user-guide languages.
The README screenshot is the owner's supplied original PNG, not a generated scene.
