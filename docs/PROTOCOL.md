[English](PROTOCOL.md) · [Russian](PROTOCOL_RU.md)

# GUI ↔ SonShell: REMOTE/1

The GUI runs the backend of this project via QProcess (stdin/stdout pipes):

```
sonshell --gui --auth-env --host <IPv4> --model <alias> --sync-dir <directory> --liveview-file <private-path> --camera-cache <private-directory>
```

`--gui` uses line input with poll, keeping libedit normal interactive
CLI mode. The regular CLI retains the old commands. In --gui sync commands, except sync off,
are rejected: GUI uses explicit card copy. `--version` does not initialize the SDK.
The GUI adds the backend and CrAdapter directories to the LD_LIBRARY_PATH of the child only
process: this is needed for transitive dlopen Sony libraries.
GUI does not use `--keepalive`: reconnections belong to CameraSession.

The username and password are passed by the variables `SONSHELL_CAMERA_USER` and
`SONSHELL_CAMERA_PASSWORD`; backend reads and unsetenv before Init. argv and GUI log
do not contain a password. The process environment is still accessible to privileged local
tools; it is not an encrypted channel for storing secrets.

## States

Full event line including stable logger wrapper (LF at the end):

```
INFO  | [INFO] REMOTE/1 connecting
INFO  | [INFO] REMOTE/1 connected
INFO  | [INFO] REMOTE/1 disconnected
INFO  | [INFO] REMOTE/1 liveview-started
INFO  | [INFO] REMOTE/1 liveview-stopped
INFO  | [INFO] REMOTE/1 liveview-failed
```

`connected` is published only after SDK handshake, `disconnected` is published from callback
SDK. The remaining lines are diagnostic and do not determine the state of the camera.
GUI buffers unterminated strings, removes ANSI, only accepts exact matches
events; partial read() does not lose the event. Secrets are redacted only in the copy used for logging,
so that a password with the value `connected` does not break protocol processing.

Buttons are only available in Connected. When the process ends/breaks, the GUI resets
Live View and repeats launch with 3/6/12/24/30s delay if enabled.
If successful, the delay is reset. Initial connection timeout - 45 s.
"Disconnect" cancels the timer, sends quit and closes stdin. After 1.5 s - TERM,
after another 1.5 s - KILL. Waiting does not block the main Qt loop.

## Live View

`monitor start` with `--liveview-file` receives JPEG from SDK in a separate thread without
OpenCV. Up to 15 fps, buffer limited to 32 MiB. First the entry `<path>.part`, then
atomic rename. Only the last frame, file and directory belong to the current session.
Qt checks for updates every 70 ms; If there is a reading error, it waits for the next frame.
`monitor stop` and ending the connection stops the thread before the ReleaseDevice.

This is a prototype local transport. Asynchronous binary socket and separate channel
transfer progress can be added to REMOTE/2 without changing the viewing widgets.

## Extensions 0.3: finished files and progress

```
INFO  | [INFO] REMOTE/1 transfer-progress <0..100> <hex-UTF8-absolute-path>
INFO  | [INFO] REMOTE/1 download-complete <new|sync> <hex-UTF8-absolute-path>
```

The path is encoded lowercase hex, so there are spaces, Unicode and newlines in the name
do not violate the boundaries of events. GUI checks format, percentage range and placement
in a given snapshot directory. `download-complete` is issued after atomic publication,
and not when a file is found on disk. `new` — Auto Sync by camera event, `sync` —
manual download/Sync All. In 0.3, `new` launched a studio show. Since 0.4 GUI does not use
This chain does not start Auto Sync.

All SDK transfers are serialized: callback without transfer ID always refers to
one active file. Staging is unique to the transfer and is located on the same FS.
Publishing does not overwrite the existing final path. `sync stop`
retains ownership of the current transfer until the terminal callback SDK; this rules out
late cancel callback after running the next file. Resume after
Failure is accomplished by re-downloading the missing file, without byte-range resume.


## Extension 0.4: camera card

Commands (one line per file/frame; GUI sends the next one after the result):

```
card list <request-id>
card preview <request-id> <epoch> <slot> <content-id> <file-id> <screen|thumb>
card copy <request-id> <epoch> <slot> <content-id> <file-id> <hex-UTF8-destination-directory>
card delete <request-id> <epoch> <slot> <content-id>
card cancel
```

ID consists of ASCII letters/numbers/hyphen, maximum 80 characters; GUI adds UUID
session. Epoch is equal to the request-id of the last list. Reconnecting resets the queue.
Old eras are rejected. Worker backend accepts no more than 8 pending requests,
The GUI keeps the rest of the queue for itself. If the deletion fails, the queue is terminated.

Response string: `INFO  | [INFO] REMOTE/1 card <lowercase-hex-UTF8-JSON>`. Object
has `type` and `request`. Each directory entry is separate (up to 60,000 hex characters),
not one JSON of the entire card. The GUI parses these lines separately without printing JSON to the log.

| type | Fields and meaning |
|---|---|
| changed | request is empty; the card has changed, transfers do not start |
| begin | epoch, beginning snapshot |
| item | epoch, slot, id, stamp (UTC), identity, protected, deletable, files |
| end | epoch, stale; end of catalog |
| slot-error | slot, message; unavailable/unsupported slot, then end |
| progress | percent 0..100 for active transfer |
| preview | path in --camera-cache, size; only compressed transfer, the original is not requested |
| copied | path, size; only verified and published original |
| deleted | terminal OnWarningExt DeleteContent OK for exact content/slot |
| error | message; operation is unsuccessful, late success is not simulated |
| fatal | message; You cannot continue transfer on this connection, restart the backend |

files is an array of `{id, format, size, path}` objects. size — decimal string,
to avoid losing uint64 in JSON. Numeric format corresponds to SDK (JPEG 0x3801,
RAW 0xB101, HEIF 0xB110); the accompanying files remain as part of the frame. Up to 0.6 list
contained only photographs; starting from 0.6 includes MP4 (see below). id/contentId are limited to uint32.
identity is formed from the available serial (aka model+host), slot, date and
paths/dimensions; this is metadata for the import log, not a hash of the original.

If the compressed API is unavailable, an error is returned, without a fallback to the original.
Copy and preview are separated from the old download-complete. GUI checks
private preview path/copy destination, session state and request ID; unknown
or legacy events do not change the selected frame/operation state.

## Expansion 0.5: available parameters and AF point

```
controls list <request-id>
controls set <request-id> <property-code-decimal> <value-decimal>
controls af <request-id> <x-0..639> <y-0..479>
```

Commands are available only to GUI backend. `request-id` - 1–80 ASCII letters/numbers/hyphens;
The GUI uses the UUID and only accepts the result of the current request. Answer:
`INFO  | [INFO] REMOTE/1 controls <lowercase-hex-UTF8-JSON>`.

- `snapshot`: `request`, `properties` and `af` (bool). Each parameter:
  `{name, code, readable, writable, current, current_label, values:[{value,label}]}`.
  Numeric values ​​are lossless decimal strings uint64; code - uint32.
  Stable name: `focus_mode`, `focus_area`, `white_balance`, `drive_mode`,
  `iso`, `shutter`, `aperture`, `ev`. current_label — displays the current
  values, including a value outside the writable list.
- `applied`: `request`; SetDeviceProperty returned success. It's accepting the setting
  and not confirmation of sharpness or unchanged value after the camera callback.
- `error`: `request`, `message` (diagnostic code). A mistake is not considered a success.
- `INFO  | [INFO] REMOTE/1 controls-changed`: callback properties. GUI integrates
  notifications with a timer and rereads the snapshot when there is no card operation.

Allow-list backend limits set to the specified eight properties. Before everyone
set are re-checked IsSetEnableCurrentValue, type UInt16/32Array or Int16Array
and GetValues. For EV wire value is an unsigned representation of the lower 16 bits
(for example -333 → "65203"), with the sign before the SDK set restored. Exposition
written with scalar type UInt32/UInt16/Int16, as in CLI exposure.
GetSetValues/GetSetValueSize are reserved in API 2.02 and are not used.
Properties without an option list remain unwritable. Snapshot is limited
29,000 bytes to hex; too large response becomes error.

AF uses AF_Area_Position: x in the high 16 bits, y in the low 16 bits. Availability
properties and writability are checked immediately before set. Borders
shrink by half the size of the frame from LiveViewProperty/CrFocusFrameInfo;
with several frames, a conservative intersection is taken. Without reliable
AF frame metadata is disabled. Points outside the boundaries are rejected, exposure mode
and focus area do not switch automatically. Since 0.6.2 the command after successful
The point is set by S1 Locked → 1 s → Unlocked, without a full shutter release. Before
records are checked for AF-S/AF-C/AF-A/AF-D/DMF mode and S1 availability; in MF/PF AF
unavailable. Snapshot af takes these conditions into account. S1 is not added to the allow-list set.
RAII releases the half-press on a launch failure and repeats the unsuccessful release.
Point setting error does not trigger AF. af-press:<SDK> means that the point is already
moved but AF trigger rejected; af-release:<SDK> - release is not confirmed.
Applied comes after release, does not confirm optical sharpness.

The GUI converts the coordinate via mapToScene/mapFromScene of the current image;
fields are excluded, zoom and pan are taken into account. Live View is shown in coordinates
JPEG SDK without additional rotation/mirroring. Orientation matching
and boundaries on each physical model remains a hardware check.

GUI request timeout is 7 s, outdated responses are ignored. After set the GUI rereads
camera values, including failure; The diagnostic message is saved.
Parameter JSON is not printed to the user log. Old CLI commands
The card protocol and the order of transfer of originals have not been changed.

## Extension 0.6: video and shooting date

Directory `card list` no longer excludes MP4 entries (SDK format 0xB982).
The accompanying XML (0xBA82), proxy and other files remain part of the record;
deleting by content-id applies to the entire post. The "video" filter selects posts
from MP4 and copies their composition, “photo” - other media records; RAW/JPEG files
the appropriate format. When `card preview ... screen` MP4 is forced
uses Thumbnail: Screennail is not supported by the Video SDK. screen command
does not load the original; a separate original mode was added in 0.7.1.

`item` additionally contains `capture_date`: YYYY-MM-DD from
creationDatetimeLocaltime of camera. If the local date is unreliable, use
valid creationDatetimeUTC; otherwise `Unknown-Date`. Days of the month and leap days
year are checked. New originals are published under
`destination/capture_date/Slot_N/relative-camera-path` with previous checks
symlink/out-of-root, size, atomicity and non-overwriting.
The computer date is not used. Old ledger entries and files are not moved;
New/Selected continue to take into account confirmed copies in the old structure.
The transfer of the original is interrupted by 3 minutes without a callback SDK, and not by the general
duration 10 minutes: the large video with incoming notifications does not end
due to a fixed time limit. Explicit cancellation still waits for the terminal callback.

### Streaming

```
card movie <request> <epoch> <slot> <content> <MP4-file-id> <hex-UTF8-local-socket-path>
card movie-play <request>
card movie-pause <request>
card movie-seek <request> <seconds-0..86400>
card cancel
```

The GUI creates a QLocalServer in advance in a private camera-cache. Backend checks
that the UNIX socket is located directly in this directory. Outgoing video bytes
SDKs are transmitted only through this channel; stdout remains text REMOTE/1.
The computer's IP is determined by the route to the camera's IPv4 via getsockname UDP socket,
without sending a datagram. SDK SetMoviePlaybackSetting gets slot/content/file,
address and standard SDK ports. Monitor stops before movie starts,
EnableLiveView is disabled. Start waits for StatusChanged, then Play. Pause and seek
executed by worker; callback does not call SDK control functions. Seek is transmitted
in units of 1/100 s. The management commands check the request of the active view.

Responses from channel `REMOTE/1 card <hex-JSON>`:

- `movie-state`: request, state (`preparing`, `playing`, `paused`). States
  playing/paused means a command accepted by the SDK; an erroneous result is returned error.
- `movie-stopped`: StopComplete/normal completion confirmed by SDK.
- `error`: failure/no data/channel closed/decoder failure; success is not faked.
- `fatal`: SDK did not confirm the stop, further operations require a new backend.

Thread queue is limited to 8 MiB, individual packet 4 MiB. Callback copies data
and wakes up the worker; it sends them with backpressure control, no SDK lock
callback and without writing the original to disk. Start waits 20 s, no video data
12 s (pause excluded), non-receiving decoder 5 s, StopComplete 5 s. View
serialized with transfers/removals. Unconfirmed Stop leads to fatal/reconnect,
so that later callbacks do not end up in the next view.

The GUI runs a separate FFmpeg with the first bytes, receives RGB24/PPM frames of the original size/frequency and displays
them via ImageView with normal scale/fullscreen/overlay. Audio is not played in this preview. Input/output memory is limited; Decoding errors are visible. After
The EOF decoder finishes reading the buffer. Moving to another entry stops the current thread,
then requests the last selected preview. The Space/Delete selection is also available in
video viewing time; copying/deleting requires stopping the thread. Go to
the other tab is terminated by movie; the previous Live View is restored after stopping.

SetMoviePlaybackSetting support for the ILCE-6700 is noted in the API Reference table
2.02. Real SDK streaming/firmware/networking requires hardware acceptance; fixtures
H.265 tests the transport/decoder, but does not validate the physical camera.

Since 0.6.1 error may contain `code: "movie-no-data"`, `camera_host`, `computer_ip`:
The GUI displays the translation with addresses and incoming TCP 55001/55003/55005. Normal
ending with no video bytes is considered an error rather than a successful viewing. At EOF
The queue of already received SDK packages is added before closing the local channel.

### Additions 0.7: SHA256 and studio JPEG

- copied includes sha256: 64 lowercase hex characters, the sum of the entire staged file
  after Result_OK/size check and before publication. This is the standard of the resulting copy;
  The camera reference hash is not available in this API.
- progress can contain phase: "hash", percent 0–100. GUI displays the transfer
  as 0–90%, hashing 90–99%, publication/ledger - up to 100%. card cancel cancels
  and hashing; an unverified staged file is not published.
- `card preview <request> <epoch> <slot> <content> <file> jpeg` gets the original
  JPEG in camera-cache via GetRemoteTransferContentsDataFile. JPEG only up to
  128 MiB, without RAW/MP4. Identity checks are the same; preview's answer is the same.
  After decoding, the GUI deletes the temporary file.
- The continuation queue and checking local checksums are implemented in the GUI; new teams
  There is no repeat shutter release/removal of the SDK. After reconnect the new epoch is read and
  stable identities of the saved plan are compared.

### Addition 0.7.1: original for viewing the card

`card preview <request> <epoch> <slot> <content> <file> original` transmits the selected
JPEG/RAW/HEIF/MP4 via GetRemoteTransferContentsDataFile to private camera-cache.
The format specifies the `.jpg/.arw/.hif/.mp4` extension, not the camera path. Zero size
and accompanying files are rejected. Free space is checked before transfer
for a file with a reserve of 64 MiB; staged/publish, size verification, cancellation and identity
catalogs are the same as for import. The answer is preview, without copied/ledger/SHA256.

GUI decodes original in QtConcurrent with cancellation: JPEG in original size,
RAW - best embedded JPEG via ExifTool, MP4 - first frame and then local
FFmpeg without reduction/recompression. JPEG photo is automatically requested
when selecting a line in standard quality; RAW/video - only with a separate button.
The quality of the studio is independent. Fast mode uses the same screen/thumb.
One last original is stored in the cache until replaced/disabled; imported
the verified copy is used directly and is never deleted by this mechanism.
When a line changes, the local player stops; outdated results
do not replace the selected frame. Cancel decoding does not send an SDK command.

CrMoviePlaybackSetting SDK 2.02 does not contain resolution/bitrate fields. Size
The SDK stream is determined by the camera (ILCE-6700 previously provided 1280x720); viewing
original requires a separate full transfer and is not presented as an improvement in the quality of the stream.

## 0.8: recording options

The controls list/set format does not change. Snapshot may contain additional
names (code always comes from the SDK, not specified in the GUI):

| name | SDK Property |
| --- | --- |
| file_type | FileType: RAW/JPEG/RAW+JPEG/HEIF |
| compression_format | CompressionFileFormatStill: JPEG/HEIF |
| image_quality | StillImageQuality |
| image_size | ImageSize |
| aspect_ratio | AspectRatio |
| raw_compression | RAW_FileCompressionType |
| steady_still | ImageStabilizationSteadyShot |
| movie_format | Movie_File_Format |
| movie_framerate | Movie_Recording_FrameRateSetting |
| movie_setting | Movie_Recording_Setting |
| steady_movie | Movie_ImageStabilizationSteadyShot |

Choices reads UInt8Array/UInt16Array/UInt32Array and the former Int16Array EV,
without treating byte 128–255 as negative. Invalid array length/type disable
record. For enums, the setter stores the ValueType received from the SDK (as
in CameraDevice.cpp Sony 2.02); the previous scalar exposure setters are retained.
Before each entry, the white list code, enable flag and choices are checked again.
StillImageStoreDestination and other uninferred properties are not available via set.

current_label and values.label of new properties are generated from the enum of the local SDK
during build; a value unknown to this SDK is shown in hex without inventing
sense. Applied confirms the SDK call, after which the GUI waits for a new Snapshot
before the next change. The dependent values ​​are changed by the camera; GUI doesn't send
setter chains and does not reproduce settings on reconnect. Auto connect when
startup - GUI settings, SDK command protocol does not change.
