[English](CHANGELOG.md) · [Russian](CHANGELOG_RU.md)

# Changes

## Not released

- Sources published in public ndrco/SonShell-Remote, main branch:
  README with direct
  screenshot of the interface on demo data, cloning command,
  possibilities and limitations. Corrected DEB name in BUILD, added information
  about legacy G'MIC sources. The application code and version 0.8.3 have not changed.
  Verified Gitleaks history, README/PNG mapping, public cloning,
  fresh GUI build without SDK and CTest offscreen 7/7.
- 2026-10-31: audit of all requests for 0.8.3, current summary of remaining tasks and
  The list of remaining tasks has been clarified. Archive audit 0.6.3 is marked historical;
  functions closed later are separated from remaining tasks and hardware acceptance.
  Rebuilding GUI/backend and CTest offscreen 9/9; The application code has not changed.

## 0.8.4 — 2026-10-31

- English is now the source and default application language. Russian remains
  available through the complete `_RU` Qt catalog; saved language preferences
  are preserved and invalid settings fall back to English.
- English documentation is primary, with retained `_RU.md` Russian counterparts.
  Both languages are included in installed help and user documentation.
- README uses the screenshot supplied by the project owner. The former demo
  caption was removed. Local development notes and their links were excluded.
- GUI/backend rebuilt; CTest offscreen 9/9 and AddressSanitizer 5/5 passed.
  Fresh-install and public release verification are recorded in TESTING.md.

## 0.8.3 — 2026-10-26

- “Previous file”/“Next file” buttons in the card, gallery and separate window viewers,
  including full screen: adjacent visible files, taking into account search and filter.
  For Live View, the buttons are not shown. The current clip stops when
  transition; The original of the following video is loaded only by Play/Original.
- Separate “Back 5 s” / “Forward 5 s” rewind the current video from the position
  decoder, including fullscreen. Originals/Local Files: Save Pause
  with display of the selected frame, file boundaries, accumulation of quick presses,
  current time/duration. The duration is read asynchronously via ffprobe.
- Real progress bar inside card viewer next to cancel: visible
  in fullscreen and shows the same percentages/queue as the main indicator.
  After completion/cancellation it is hidden and not shown during SDK playback.
- Transferring while the original is loading cancels the previous transfer and prompts
  selected preview; the later result does not start the old movie. RU/EN updated.
- CMake GUI + SDK backend, CTest offscreen 9/9; AddressSanitizer 4/4.
  DEB tested with a clean install on Ubuntu 24.04.5 amd64, running RU/EN,
  FFmpeg/ffprobe and update from 0.8.2. Hardware testing for this version has not been performed.

## 0.8.2 — 2026-10-25

- About: NDRco icon from GitHub profile, author/maintainer and link to
  ndrco/SonShell-Remote, version, links to SonShell and Sony Camera Remote SDK.
  The image is built into the application, About opens without a network and does not block
  camera event loop. The window and information are available in Russian and English.
- Project address and NDRco added to README; source and SHA256 icons are described
  in gui/assets/README.md. Publishing to GitHub is not included in this step.
- GUI build with and without SDK, CTest 9/9; About RU/EN, reopening
  and closing along with the main window are checked under AddressSanitizer. DEB passed
  clean installation on Ubuntu 24.04.5 amd64 and update from 0.8.1.

## 0.8.1 — 2026-10-25

- Play on the card loads the original video into a temporary cache and starts viewing
  after downloading, with the original resolution/frequency. The size is visible in advance, transfer
  cancelled; replay uses a cache or verified local copy.
  Cancel/change line/tab does not trigger late result. Nothing on shutter release
  new is not transmitted. Stream SDK remains an option in Settings: on ILCE-6700
  A failure was reproduced without video frames when network connections were working.
- Light transparent inscriptions with letter shadow: marks at the top left, confirmations
  bottom left, without central plate; They also work in full screen.
- Settings: connection/password/autoconnect, directory and preview/studio settings.
  The quality of the photo/video card is preserved, the magazine is hidden and can be opened through the menu.
  Connect, status, and survey remain in the main window; Holding a frame in Live View.
- RU/EN help and interface; regressions of video start/cancel, cache and settings window.
- CTest 9/9 and updated GUI/video 2/2 after the last edit; ASan 4/4,
  then repeat the updated 2/2. ILCE-6700: original video 1920×1080, Play,
  pause, cache and full-screen marks are hardware confirmed.
- DEB: installation with dependencies in clean Ubuntu 24.04.5 amd64, RU/EN runtime,
  SDK loader, update 0.8.0 → 0.8.1 and removal.

## 0.8.0 — 2026-10-25

- The connect button is larger, with a contrasting blue background and keyboard focus.
  The “Connect at startup” checkbox is saved and enabled by default:
  Only the saved username/password and previously accepted SDK agreement are used.
  With incomplete settings, there is no modal window; manual connection/disconnection,
  unchecking and closing cancels the delayed start.
- Collapsible sections “Photo recording” and “Video recording”: RAW/JPEG/HEIF format,
  JPEG/HEIF, quality, size, aspect ratio and RAW compression; format, frame rate,
  video bitrate/color and photo/video stabilization. Lists, current value and
  availability are obtained from the SDK, values are rechecked before being written.
- Supported UInt8Array for frequency/stabilization/compressed format. Signatures of new
  values are generated from the local SDK when building, without copying the SDK to Git.
  After the setter, the fields wait for a new snapshot of the dependent properties; obsolete events
  input does not send the second command. New properties are not automatically updated.
- RU/EN interface and help, auto-connect/cancel scenarios, dependencies
  parameters, read-only, unsupported properties and SDK precision coding.
- On ILCE-6700, auto-connect without clicking, 10 new properties available out of 11,
  recording the current ImageSize without changing the value, re-reading and manually disabling
  confirmed by hardware. Transitions between modes/models require separate acceptance.
- CTest 9/9, 101 Qt scripts, ASan GUI/backend. The video test has been translated to
  continuous event loop: previous qWait pauses gave a false RGB-pipe timeout;
  20 repetitions preserved all original frames and pixels. The working player has not changed.
- DEB: installation with dependencies from scratch on Ubuntu 24.04.5 amd64, RU/EN runtime,
  SDK loader in isolation, update 0.7.1 → 0.8.0 and removal.

## 0.7.1 — 2026-10-24

- In the card tab, the default photo opens from the original JPEG, including
  RAW+JPEG. Quick compressed preview remains a separate option; studio quality
  and Live View independently. RAW is not downloaded when selecting a line and when descending.
- “Original · ... MiB” button for photo/video: background transfer with progress,
  canceling and checking the size. JPEG - original pixels, RAW - best built-in
  JPEG with size indication, MP4 - original size and frequency without recoding.
- One last original in the temporary cache; reopening uses cache
  or a certified imported copy. Viewing does not change the import log
  and status “New”; Temporary files are deleted when replaced/disabled.
- Quality, file size and undo available in full screen. Flipping
  stops the player, the later result does not replace the selected frame.
  RU/EN interface and help have been updated.
- ILCE-6700 received original JPEG 6192x4128 (6,139,791 bytes) and video
  3840x2160 (67,383,474 bytes), 4K footage shown; reopening both
  used cache without transfer. Shooting, deleting and camera settings did not change.
- CTest 9/9 (88 Qt scripts), ASan GUI/SDK, backend headless/OpenCV.
  DEB tested by installing with dependencies from scratch on Ubuntu 24.04.5 amd64,
  update 0.7.0 → 0.7.1 and removal, RU/EN and SDK loader in isolation.

## 0.7.0 — 2026-10-24

- SHA256 of content before the original is published, overridden calculation in SDK worker,
  saving the standard in the journal. Local check without camera, with progress and
  cancellation, detects a change at the same size/mtime. Faulty standard
  not replaced; old entries without a sum remain compatible and are marked
  "without a standard." The local copy is checked; the SDK does not provide the camera hash here.
- Saving the queue and “Continue importing” after cancellation/abort/restart:
  new epoch/content-id are matched by stable identity, completed
  files are not repeated. An incomplete file is transferred again; shutter release/removal is not repeated.
- Update the status of one row after copying instead of recreating the list.
- Studio JPEG in original size from JPEG/RAW+JPEG recording, only temporary
  catalogue. After decoding it is removed; RAW won't load. RAW-only gives
  obviously signed compressed preview. The source/size is visible and the quality selection is retained.
  Changes during the slow preview are merged into the last frame request;
  duplicate notifications do not cause the same frame to be shown again.
- Full path of the camera source file in the bottom line during studio screening.
- RU/EN interface and help, protocol and build instructions with OpenSSL have been updated.
- CTest 8/8 (80 Qt scripts), ASan GUI/SDK without access errors; headless/OpenCV,
  DEB, clean install of Ubuntu 24.04.5 amd64, 0.6.3 update and uninstall verified.
- JPEG+RAW tested on ILCE-6700, stop/reconnect/continue without
  repeat JPEG, SHA256 of two copies and JPEG 6192x4128 of the existing frame. Check
  physical shutter release/series remains separate from automated tests.

## Documentation - audit 2026-10-24

- Roadmap verified with implementation 0.6.3: missing functions separated,
  additional extensions and hardware acceptance. Studio restrictions have been clarified
  resolution, import, platforms and advanced camera settings.
- Fixed outdated notes about packaging, SDK video and exposure parameters;
  automatic transfer of originals is not returned to the backlog.

## 0.6.3 — 2026-10-24

- ISO, shutter speed, aperture and EV: scroll with mouse/wheel, list of values and
  manual input Enter/"Set". The ISO field has been expanded to include AUTO entirely.
- Current values and steps come from SDK instead of fixed GUI values;
  mode/lens, negative EV and fractional shutter speed are taken into account. Before recording
  The backend rechecks the camera's availability and list of values. CLI saved.
- Drag sends total value after release, wheel merges
  steps in 250 ms; shutdown/busy cancels the pending command. Auto update
  waits for input to complete, Esc cancels the draft. RU/EN tips and help.
- The user confirmed the operation of the corrected autofocus 0.6.2.
- CTest 8/8, ASan without access errors, SDK backend headless/OpenCV; DEB verified
  installation with dependencies from scratch on Ubuntu 24.04.5 amd64, updating and uninstalling.
  Hardware acceptance of new dials remains open.

## 0.6.2 — 2026-10-23

- Clicking on a Live View point triggers AF after moving the frame: half-press S1
  for a second with release, without taking a picture. The Autofocus button is retained.
- MF/PF modes and unavailable S1 disable click AF. No move error
  starts AF; Startup failure after movement is shown separately.
- RU/EN interface, tips and help have been updated to “Click-to-autofocus”.
- Checking the order of SDK calls, releasing on failure, no AF after
  errors in coordinates/recording, manual mode and GUI messages.

## 0.6.1 — 2026-10-23

- Local video: original resolution and frequency instead of 720p/12 fps,
  RGB without re-JPEG compression. The first frame is read at its original size;
  The reduction remains only for list thumbnails.
  The pace is determined by the timing of the frames, so that the short clip does not start abruptly.
- Full path of the selected file in the bottom line: local absolute or
  source SDK path on the card. Select, copy, scroll long path.
- Movie Playback: finishing the queue at normal completion, waiting
  preparation takes up to 20 s, FFmpeg starts only after data arrives.
  An empty stream gives a separate translated error with addresses and TCP ports.
  The actual resolution of the resulting video is displayed in the preview pane.
- Hardware diagnostics detected blocking of incoming TCP 55001 from the camera
  UFW firewall. With the user's consent, an address rule has been added,
  stream, pause/resume/seek/stop and Live View recovery are hardware checked. The RU/EN help and installation instructions describe the address
  rule for 55001/55003/55005 and its rollback; the application does not change the rules of the system.
- Regression checks for source pixels/size/frequency, empty stream,
  damaged file, end of SDK queue and full paths.

## 0.6.0 — 2026-10-23

- Fullscreen: permanent overlay of name/tags, Space/Delete confirmations and
  explanation for unavailability of deletion. When changing a line, the old photo is removed.
- MP4 included in camera directory along with proxy/XML. Photo/video filters, thumbnail,
  the same marks/delete/copy. For video, the Thumbnail SDK is used.
- Streaming SDK viewing by button, pause/stop/seek, without downloading the original;
  limited buffers, Stop confirmation, Live View recovery. Preview
  without sound, up to 1280×720/12 fps; The hardware streaming check remains open.
- MP4/MOV/M4V local gallery: first frame and metadata in background, explicit playback.
- New copies in YYYY-MM-DD/Slot_N/original hierarchy: local SDK shooting date,
  then UTC/Unknown-Date. Old files and ledger remain compatible.
- Large file transfers are limited to a silent timeout rather than 10 minutes
  total duration. FFmpeg - explicit DEB dependency; Help/translations updated.
- CTest 8/8: 56 Qt cases and translation checking; ASan without access errors.
  Verified headless/OpenCV builds, installation on fresh Ubuntu 24.04 amd64,
  H.265 decoder, updated from 0.5.0 and removed. Hardware acceptance testing is open.
  Scenarios and results - docs/TESTING.md.

## 0.5.0 — 2026-10-21

- Space switches the copy mark, Delete – defect on the card, including complete
  screen. Delete does not trigger deletion; protected frames excluded, auto-repeat
  disabled, search input saved.
- Menu and help F1 with manual/table of keys without the Internet, “About” window.
- Russian/English interface, Qt TS/QM, saving the language for the next launch;
  Help in both languages, checking the completeness of the catalog during CTest.
- SDK parameters: focus mode, AF area, WB, drive from those supported by the camera
  values; recheck before recording, update after callback/change.
- Selecting the AF center by clicking in Live View, taking into account zoom/pan and the boundaries of the SDK frame.
  Studio photo and card operations disable point selection; setup success not
  issued as confirmed sharpness. CLI and lazy import are preserved.
- CTest 7/7: 46 Qt cases and checking 197 translations. ASan GUI/backend without errors
  access; SDK headless and OpenCV builds completed.
- DEB tested from scratch in a separate Ubuntu Base 24.04 amd64: both languages, runtime SDK,
  update 0.4.1 → 0.5.0 and removal. Hardware acceptance of AF and parameters is open.

## 0.4.1 — 2026-10-21

- Fixed deviation of Sony SDK volume paths when copying; namespace and
  backseat delimiters are normalized without traversal/symlinks permission.
- Fixed indicator remaining at 50% after queue completion with errors:
  the total no longer falls outside the range of the QProgressBar. Errors are logged.
- The second slot is requested only when supported by the camera, no slot
  is no longer considered a card malfunction.
- Copy/reject marks: white square 22 px, dark frame, blue/red check mark;
  the contrast is preserved in the dark theme, the click area corresponds to the rendering.
- Flip ←/→ and Page Up/Page Down for card and local gallery,
  including a separate window and fullscreen, taking into account search/sorting.
- Card Quick Jump remembers the last selected frame while loading
  preview; does not require waiting for intermediate images and saves marks.
- Checking SDK paths, traversal, one/two slots, final progress when
  success and error, keys/fullscreen, filter and independent switching of marks.
- CTest: 35 cases, ASan without access errors. The package has been verified by update 0.4.0 →
  0.4.1, isolated launch and uninstallation on Ubuntu 24.04 amd64.

## 0.4.0 — 2026-10-21

- Fixed shooting script: GUI does not download originals after release, old Auto Sync
  ignored. CLI saved; Hidden playback loading in the GUI is disabled.
- “On camera” tab: slot catalog, compressed SDK preview, formats/sizes/dates,
  separate copying and defect marks; the local gallery is located in “On the computer”.
- Explicit copying of all, new or marked, RAW/JPEG, directory change,
  sequential queue, progress and stop. Verified copy log
  saved in a directory; deleted/truncated local files are new again.
- Defect removal with confirmation of the entire RAW+JPEG frame, availability check,
  protection and identity; success only by callback camera.
- Previews and originals are separated; studio mode uses compressed preview without RAW.
  Background decoding, 64 MiB cache, outdated responses are discarded.
- The size of the original is checked against the SDK; name collisions are not overwritten, publication
  renameat2(RENAME_NOREPLACE) is atomic.
- Cancellation awaits terminal callback; lack of confirmation causes reconnection.
- Fixed returning full-screen viewing in QSplitter and closing the window with an active preview.
- Added GUI scripts with fake backend and backend with SDK API substitution without camera.
- 25 scenarios pass; ASan without access errors. DEB 0.4 verified by installation
  with dependencies in the latest Ubuntu Base 24.04 amd64, isolated launch and removal.

## 0.3.0 — 2026-10-21

- Gallery with thumbnails, search, sorting and JPEG/RAW/RAW+JPEG filters.
- RAW/JPEG pairs are combined by name within the same directory, JPEG has priority.
- EXIF and built-in JPEG RAW via a separate ExifTool; the originals are not changed.
- The source and size label is placed above the scale so that the RAW preview is
  clearly marked and in a narrow gallery area.
- Indexing and decoding in the background, canceling outdated queries, cache up to 128 thumbnails.
- Studio viewing 2–10 s after the new photo is ready, holding the frame,
  restart the timer during a series, return to Live View. Sync All does not capture the screen.
- Progress of the current program and Sync stop button.
- Hidden staging, atomic publishing via SDK Result_OK, progress and readiness events.
- Serialization of transfers and waiting for terminal callback upon cancellation.
- DEB installs ExifTool; The GUI uses Qt Concurrent for background tasks.
  The settings format is compatible with 0.2.
- Fixed releasing the window when QProcess ends: callbacks are disabled
  before the MainWindow members are destroyed, teardown CameraSession does not dispatch UI events.
- EXIF/orientation, RAW preview, gallery, atomic publishing, legacy tests
  tasks, timer/hold and complete chain new photo → studio → Live View.

## 0.2.0 — 2026-10-19

- An independent project has been created with SonShell history up to `107f9fa` and GUI import.
- GUI is divided into MainWindow, CameraSession, SettingsStore, ImageViewer, PhotoLibrary.
- The name and password are saved by checkbox and deleted when it is removed; file 0600,
  directory 0700. Password excluded from argv and log.
- Built-in Live View: JPEG from SDK without OpenCV, scaling and full screen.
- Separate viewing of the original JPEG: 100%, zoom, pan, F11/Esc.
- Camera states based on SDK events, automatic repeats and cancellation without blocking the GUI.
- Shooting, AF, video, exposure, Auto Sync, Sync All and directory selection are saved.
- Added REMOTE/1, backend pipe mode, --version option and saving the regular CLI.
- JPEG is shown after the file has been stabilized and the end of the image has been checked;
  re-writing the same path also updates the view.
- Added DEB for Ubuntu 24.04 amd64 with SDK runtime, menu, icon, licenses,
  automatic dependencies and agreement to Sony's terms and conditions upon first connection.
- The plan for gallery, RAW/EXIF, studio shooting, mouse AF and SDK expansion is recorded in the documentation.
- Added a private path for downloading SDK libraries only for the child backend,
  including dependencies monitor_protocol; system library search paths do not change.
- Automatic and installation tests are described in docs/TESTING.md.
