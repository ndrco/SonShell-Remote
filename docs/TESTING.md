[English](TESTING.md) · [Russian](TESTING_RU.md)

# Checks

## Results 2026-10-19

- Ubuntu 24.04 amd64, GCC 13.3, Qt 6.4.2, Sony SDK 2.02.00.
- Building GUI without SDK, GUI + headless backend and backend with OpenCV: successful.
- CTest: 1 set, 8 behavior scenarios, 0 errors (Qt also does init/cleanup).
- Tested: opt-in and removal of username/password, 0600, scale and return from full
  screen, incomplete JPEG and file replacement, fragmented events, commands
  no secret in argv, reconnect/cancel, turn off replays, stop
  hung process without blocking UI, MainWindow integration + built-in JPEG.
- DEB created by CPack, dependencies calculated by dpkg-shlibdeps. desktop-file-validate: successful.
- Full installation via apt in clean Ubuntu Base 24.04 amd64: successful.
  Used official rootfs with SHA256 verification, proot only for apt/dpkg;
  runtime was launched in a separate bwrap user/mount/PID/network namespace, without
  access to the working tree and SDK. apt of the installed application allowed everything
  dependencies. The base rootfs has a machine-id/runtime directory created before starting,
  which are usually already installed in Ubuntu Desktop.
- Installed GUI: --version outputs SonShell Remote 0.2.0; window lives up to
  control timeout 3 s (code 124), no abnormal termination. Offscreen plugin
  reports expected missing propagateSizeHints; this is a test limitation
  graphical backend, not build error. Ldd backend and PTP adapter: all dependencies found.
- Final DEB reinstalled; The SHA256 of the installed GUI matches
  package contents. Removal via apt remove was successful, the executable file was deleted.
- Interface screenshot checked visually; controls and Live View are visible,
  There are no overlays or cropped text at 1280x870.
- In a clean image, all packaged SDK .so resolve dependencies with private
  LD_LIBRARY_PATH; backend --version outputs 0.2.0 (REMOTE/1). SDK Init and try
  connections to isolated 127.0.0.1 fail with the expected Camera not available / CrError_Connect_ConnectIP and code 2
  in the absence of a camera, without bootloader errors and crashes.
- No physical camera was used. SDK operation when shooting, real speed
  Live View, RAW+JPEG transfer and network interruption require lower receiving hardware.

Automatic checks use fake backend and temporary settings/images;
they do not confirm that the physical camera works with the SDK.

## Hardware scenario

1. Connection with Access Authentication; check incorrect password.
2. Save fields by checkbox, restart; uncheck the box, check deletion.
3. Live View inside GUI: Fit, 100%, wheel, F11, Esc, return and continue flow.
4. Remove JPEG, wait for transfer; separate window, 100%, pan and full
   screen. Check a large and rotated JPEG.
5. Turn off Wi-Fi/camera: actions are disabled; restored after switching on
   connection, Auto Sync and Live View.
6. While waiting, click “Disable”: the repetitions stop.
7. Close GUI on connection/transfer: backend should exit.
8. Auto Sync and Sync All for RAW+JPEG; check the downloaded originals.

## Repeat installation check

Image: https://cdimage.ubuntu.com/ubuntu-base/releases/24.04/release/ – amd64
base tar.gz and its SHA256SUMS. In this session everything is placed inside `build-clean/`;
the host system and installed prototype did not change.

1. Unpack the verified rootfs into build-clean/rootfs. Install DEB command
   `apt-get install /packages/sonshell-remote_0.2.0-1ubuntu24.04_amd64.deb` inside
   rootfs (root -0, bind only /dev, /proc, resolv.conf and package directory).
2. Create rootfs/etc/machine-id and private XDG_RUNTIME_DIR for the minimal image.
3. Run the installed application in bwrap with rootfs as /, `--unshare-all`,
   clean env, QT_QPA_PLATFORM=offscreen. Do not mount the working tree and SDK.
4. Check `sonshell-remote --version`, launching a window under timeout, ldd backend and
   adapters, lack of SDK paths in RUNPATH, then `apt remove sonshell-remote`.

This is a userspace installation and startup check, not a full-fledged VM with Ubuntu Desktop,
graphics driver and physical camera. Verification is required for a stable release
in a real Ubuntu Desktop with a network and camera according to the scenario above.

## Version 0.3 - 2026-10-21

- Environment: Ubuntu 24.04 amd64, Qt 6.4.2, ExifTool 12.76, Sony SDK 2.02.00.
- Two sets of CTest, 15 behavioral scenarios: successful (21.31 s in the final package build).
- Added EXIF and JPEG rotation checks, embedded RAW extraction and rotation
  JPEG with separate RAW size, pairs/filters/search, exclusion of hidden staging,
  publication and refusal to overwrite the finished file, change directory/cancel reading,
  restart/hold studio timer and full script with fake backend.
- In full script: Unicode/space in name, transfer progress, new JPEG
  shown at 1200x800 instead of Live View 640x480; then the flow is restored.
  Sync All updates the gallery but does not include the studio show.
- RAW fixture - synthetic TIFF/DNG with built-in JPEG and EXIF; generated
  test, is not a real camera file. The test confirms the format path
  reading previews, but does not replace checking ARW from different Sony models.
- Visual inspection found heap-use-after-free when closing the window:
  finished QProcess accessed the destroyed list of actions. ASan confirmed
  exact reason; explicit teardown of MainWindow/CameraSession fixed the error.
- Both sets repeated under AddressSanitizer: 0 memory access errors,
  ASAN_OPTIONS=detect_leaks=0:halt_on_error=1; Leaks were not tested by this run.
- Backend is built with SDK 2.02, headless and OpenCV. SDK/hardware new ways
  the programs have not yet been tested on camera; fake backend does not replace such verification.
- Screenshots 1280x870 and a separate gallery are checked: thumbnails, filters, preview,
  EXIF and current progress are visible. Side blocks scroll in a smaller window.

### Installation 0.3

DEB 0.3 installed by apt into a separate Ubuntu Base 24.04 amd64 without dev packages:
ExifTool and Perl are installed automatically from Depends. In bwrap with isolated
GUI/backend versions were checked using the network and a clean environment and the window was launched up to
control timeout (code 124, without crash).

In the same image, a separate small helper calls MediaReader from the same
GUI static library: successfully read embedded JPEG 400x600, rotation 6,
model ILCE-6700 and RAW size 6000×4000 synthetic DNG. Not mounted into image
project sources or source SDK - only helper and synthetic fixture.
This checks runtime ExifTool and JPEG decoder, not camera hardware compatibility.

The final DEB is re-installed after editing the source signature: GUI and
backend output 0.3.0, the window starts without crash. SHA256 of installed GUI
matches the contents of the package. Removal via apt remove was successful, the GUI file was deleted.
The original prototype and host settings/services were not changed.

### Hardware acceptance 0.3

1. JPEG + RAW+JPEG + RAW: check originals, EXIF, orientation and source signature.
2. Auto Sync and Sync All: files appear only ready, ready ones are not duplicated.
3. Studio 2/3/10 seconds, burst and camera button; hold/remove hold,
   changing the interval, turning off the mode, losing connection and closing the window.
4. Stop Sync during transfer; on a camera without immediate cancel wait for the current one
   file. Repeat Sync after cancellation/break, check for the absence of incomplete final files.
5. Large card/catalog and large RAW without preview: UI remains manageable,
   the reading error is explained, the original RAW is not changed.

## Version 0.4 - 2026-10-21

Scenario 0.3 revised after hardware user comment: heavy RAW
transmitted after each shutter release. The checks below apply to the new deferred
import. Physical camera performance was not measured in this session.

- Ubuntu 24.04 amd64, GCC 13.3, Qt 6.4.2, Sony SDK 2.02.00.
- GUI without SDK and backend headless/OpenCV are built. GUI does not include Sony headers.
- 4 sets, 25 behavioral scenarios: basic 8, media 7, GUI card 6,
  backend cards 4 (init/cleanup Qt not included in scripts).
- Fake backend: normal shutter release, list and preview do not call card copy and do not create
  originals in the destination catalogue; the old auto_sync=true is not restored.
- RAW+JPEG checked, select one format, selected/all/new, save log
  between controllers, local file disappearing and new destination directory.
  The full repeated copy receives suffixes, the “new” ones are not transmitted again.
- Cancellation stops the queue; deleting and copying have different marks.
  Canceling the dialog does not send the deletion; the protected frame is not deleted; SDK error
  is not considered a success and stops further deletions.
- The new backend is tested by substituting API functions at the linking stage. SDK runtime
  used for constructors/destructors of structures; Init/Connect and real
  The camera/network is not called in these tests. Tested compressed API without original
  fallback, explicit original transfer and cancellation to terminal callback, check
  lengths, repeated contentId with different identity, directory epochs, symlinks,
  capability/protection and successful/unsuccessful OnWarningExt removal.
- GUI sets and backend cards passed AddressSanitizer with
  `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1`. There are no memory access errors;
  Leaks were not tested by this run.
- Additional check of closing with full-screen preview revealed saving
  fullscreen state when returning to QSplitter. Fixed state reset, order
  panels and size restoration. The regression is checked in the MainWindow test.
- 1280x870 interface checked visually: card and local files are separated,
  preview/source/scale, slot/date/status, independent marks and buttons are visible.

### Hardware acceptance 0.4 (not yet completed)

1. Update 0.3 with Auto Sync=true; a series of heavy RAW from software and hardware
   shutter release Check for original transfers and compare Live View latency.
2. Update the card after the series; compare list and formats with card, including both
   slots and the same names. The unsupported/empty slot needs to be explained.
3. Iterate through RAW and RAW+JPEG previews, Fit/100%/F11/Esc and closing the window in fullscreen.
   Confirm that the compressed preview is being transmitted, and not the original.
4. Select several frames, copy JPEG, then new RAW; compare sizes
   and contents with a card. Repeated “new” ones are not transmitted, “all” create complete
   additional copies without overwriting. Check another destination directory.
5. Test frame with backup: Cancel delete dialog, then delete
   Entire RAW+JPEG. Check security/unsupported model and SDK error.
6. Stop a large transfer, turn off the network, reconnect and copy again
   new. Partial files should not appear under final names.
7. Studio preview using a separate flag, return after 2/3/10 s and hold;
   manual import does not include studio and does not request RAW on release.
8. Large card and long queue: evaluate list and update delays. Qt
   the list is not yet virtualized; profiling of this case remains in the plan.

### Installation 0.4

Final Release build: 4/4 CTest, 25 scenarios, 0 errors, 25.00 sec.
Three GUI kits passed ASan; after the last change to full import mode
affected camera-card-tests has been repeated. Four backend scripts also passed ASan.

For release, the official Ubuntu Base 24.04.5 amd64 was extracted into a separate one
`build-clean/rootfs-0.4`, SHA256 tar.gz verified against previously received SHA256SUMS.
Runtime dependencies were installed by apt from Ubuntu repositories from scratch, without dev packages.
The final DEB is reinstalled after the last edit; hashes of installed
The GUI and backend matched the contents of the DEB. In a separate bwrap namespace without access
to the working tree/source SDK checked:

- GUI and backend `--version`: 0.4.0; ExifTool: 12.76.
- GUI lives until control timeout 3 s (code 124), without crash.
- Ldd backend and PTP adapter: no missing libraries.
- SDK Init/attempt to connect to isolated 127.0.0.1: expected
  CrError_Connect_ConnectIP, code 2, without loader error/crash; physical camera
  and the user's network are not available to this run.
- Removal via apt remove was successful, the installed GUI was removed.

All installation files, test settings and images are located in the build directories
or QTemporaryDir. The original prototype, settings and host services did not change.
Package: `build-package/sonshell-remote_0.4.0-1ubuntu24.04_amd64.deb`; control
the checksums and the original commit are written side by side in `RELEASE-0.4.txt`. Hardware acceptance
and the speed of the actual camera remain untested.

## Version 0.4.1 - 2026-10-21

When reading-only checking the running version 0.4, you can see: 2 files processed, 2 errors
“Unsafe path on the card”, indicator 50%, “Stop” off, the rest
actions are available. This is the end of the queue with failures before calling SDK transfer,
and not a frozen transfer. User confirmed deletion and one physical slot
α6700. Copying with the new version on the camera has not yet been tested.

Added checks:

- Paths with volume namespace, C:/ and back separators are copied to the expected
  hierarchy Slot_1/DCIM; `..` and symlinks are still blocked.
- Missing physical slot 2 does not give slot-error; supported but not ready
  slot reports problem; the available slot is reread.
- The queue from the RAW+JPEG pair ends at 100% both with success and with two failures.
  "Stop" is disabled only after the operation is completed.
- Copy and defect marks are switched independently in the dark palette; visually
  Large white squares, dark frames and colored checkmarks have been checked.
- Arrows and Page Up/Page Down work in card preview on full screen, in local
  gallery and a separate photo window. The search and list order are preserved.
- Quick presses while the camera is responding select the last frame; old preview
  does not replace choice. The marks are saved. The current frame hidden by the filter is not
  blocks the transition to visible.

SDK-free GUI is tested with fake backend; SDK backend - linking API substitutions.
The new checks do not initiate a connection to the real camera and do not delete its files.
Temporary settings/synthetic images and diagnostic screenshots are not
are added to Git. The running user session is not closed or changed.

Final Release build: CTest 4/4, 35 cases without init/cleanup, 28.32 sec.
GUI kits have passed ASan; after clarifying the case of a hidden current frame, repeated
affected by camera-card-tests (3.94s). The SDK backend also passed ASan (9 cases).
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1: no access errors, leaks were not checked.

DEB 0.4.1 tested by updating from 0.4.0 in isolated Ubuntu Base 24.04 amd64
with previously installed runtime dependencies. GUI/backend output 0.4.1;
window lives up to timeout 3 s (124), SDK connection to isolated 127.0.0.1
ends with the expected CrError_Connect_ConnectIP/code 2, no loader errors.
The hashes of both installed executables matched the DEB; apt remove is successful.
The original SDK, work tree and user camera are not accessible by runtime namespace.
These checks do not confirm the hardware speed/correctness of the new transfer.

## Version 0.5 - 2026-10-21

Added checks without a camera:

- Space/Delete switch their columns regardless of the current cell; fullscreen,
  pressing again, frame protection and normal search editing. Removal commands
  are not sent when pressing Delete.
- Convert click to image coordinates for fit, zoom and pan; fields and
  drag and drop excluded. Point selection is disabled along with the function.
- Fake backend: available/read-only parameters, AF point only in Live View,
  shutdown when connection is lost, restore value after failure,
  absence of a polling cycle when the capabilities API is unavailable.
- Link-time substitution SDK: GetValues options, set re-validation,
  AF x/y packing, frame borders, missing frame, SDK failure and invalid input.
  SDK Init/Connect are not called. Utility setters LiveViewProperty in SDK 2.02
  do not fill in metadata, so the test substitutes getters and does not write private memory.
- Russian/English MainWindow, F1 from fullscreen, full built-in help,
  saving the language for the next launch and fallback of an unknown value.
- Completeness of the English TS catalog, completeness of translations and substitutions.

At this stage, no physical camera was used. AF availability and accuracy,
orientation, hardware mode changes and post-switch behavior
Photos/videos require separate verification on the α6700. A successful set does not mean sharpness.

### Final build and installation 0.5

- Full Release CMake/CTest: 7/7, 46 Qt cases without init/cleanup and separate
  directory check (197/197 lines used), 31.35 sec, no errors.
- SDK headless and the previous OpenCV backend are assembled. ASan: 5/5 GUI/directory; after
  the last edit repeated the affected controls-tests (3.22 s), backend 2/2.
  Leak detection is disabled due to system Qt/SDK libraries; no access errors.
- Russian/English interface and help are visually viewed on offscreen
  screenshots; The new panel transfers allow for a narrow side column.
- Re-packaged official Ubuntu Base 24.04.5 amd64 in
  `build-clean/rootfs-0.5`, tar.gz verified by SHA256SUMS. Apt installs from scratch
  runtime dependencies, including qt6-translations-l10n; SDK/dev packages externally
  not available for isolated launch.
- Bwrap with separate network: GUI/backend --version = 0.5.0, ExifTool = 12.76;
  both GUI languages ​​live until the control timeout, English --help translated;
  ldd backend/PTP adapter without missing libraries. Init SDK and connection
  to isolated localhost are completed with the expected CrError_Connect_ConnectIP,
  code 2, no accident; no physical camera is used.
- SHA256 of installed GUI/backend matches DEB files. Apt remove is successful.
  Additionally, 0.4.1 was installed and updated to 0.5.0 with removal.
- Package `build-package/sonshell-remote_0.5.0-1ubuntu24.04_amd64.deb`;
  checksum/commit - `SHA256SUMS-0.5.0.txt`, `RELEASE-0.5.0.txt` nearby.
  Logs: `build-clean/fresh-install-0.5.log`, `fresh-runtime-0.5.log`,
  `upgrade-0.5.log`, `verify-0.5.log`; the host settings and services did not change.

## Version 0.6 - 2026-10-23

Testing new scenarios without a physical camera:

- Space/Delete in fullscreen give visible confirmation, permanent states
  and an explanation of the prohibition for a protected file. The marks change and over the moving
  video. The synthetic video and overlay were checked visually using the screenshot.
- Real FFmpeg decodes generated MP4/H.265: first frame in MediaReader,
  play/pause/stop, stream via QLocalSocket and EOF with read-through.
- Videos and photos of the same name are indexed separately. Gallery/filters see MP4,
  remote MP4+XML is copied by local date, New does not repeat the completed import;
  the old Slot_N structure continues to be taken into account through the old ledger.
- Backend with SDK substitution includes MP4/sidecar, forces use of Thumbnail,
  copies the original only explicitly. Local date, fallback UTC, calendar boundaries
  and leap year are checked separately from the current computer time.
- SDK movie substitutions check start/play/pause/seek/cancel/StopComplete,
  old request, startup failure and fatal for unconfirmed stop.
- MainWindow disables conflicting commands for the duration of the movie and returns Live View
  after switching to another tab. SDK Init/Connect is not called in these tests;
  The movie route in the substitution is defined for loopback without sending datagrams.

Synthetic H.265 does not validate the data format/delivery of the actual camera.
Hardware acceptance testing: streaming and network ports α6700, firmware, local date approx.
midnight/time zone, long MP4, pause/seek, protect/delete recording composition.
Preview without sound up to 1280×720/12 fps, originals are not re-encoded.

Release: CTest 8/8, 56 Qt cases without init/cleanup and translation directory check,
36.46 s. ASan GUI/directory 6/6 and backend 2/2; repeated after final changes
camera-card-tests, controls-tests, video-tests (3/3, 11.28 sec). Leak detection
disabled; no access errors. Headless and OpenCV backend are assembled.

### Installing release 0.6

The official Ubuntu Base 24.04.5 amd64 has been re-packed into `build-clean/rootfs-0.6`
with tar.gz verification using SHA256SUMS. Apt installed all runtime dependencies from scratch,
including FFmpeg. In a separate bwrap namespace without access to the working tree and SDK
GUI/backend 0.6.0 tested, Russian/English launch before control timeout,
translated --help, ldd without missing libraries, ExifTool and real
H.265 encode/decode → JPEG loop. Connecting the SDK to an isolated loopback gives
expected CrError_Connect_ConnectIP/code 2; The camera and user network are not used.
The hashes of the installed GUI/backend matched the DEB. Additionally completed installation
0.5.0 → update 0.6.0 → apt remove. The host settings and services did not change.

Package: `build-package/sonshell-remote_0.6.0-1ubuntu24.04_amd64.deb`.
Logs: `build-clean/fresh-install-0.6.log`, `fresh-runtime-0.6.log`,
`upgrade-0.6.log`, `verify-0.6.log`; checksum/commit —
`build-package/SHA256SUMS-0.6.0.txt` and `RELEASE-0.6.0.txt`.


## Version 0.6.1 - 2026-10-23

- Release: CMake build and CTest 8/8, 62 Qt scripts without init/cleanup plus check
  translation. SDK backend 2.02 is built with headless and OpenCV.
- New 1920×1080/24 fps check receives all 24 frames, compares decoded ones
  RGB pixels of the first frame with reference FFmpeg output. Playing time
  no shorter than 850 ms for a second clip: PTS tempo checked without initial jerk. Checked damaged
  local file, empty camera channel without false FFmpeg error and full path
  camera/local file when switching tabs.
- H.265 over QLocalSocket starts showing frames before EOF (short video), after
  EOF received all 24 frames. The backend separately checks the addition of the queue after
  NormalTermination and structured movie-no-data with stop confirmation.
- ASan: GUI remote/media/card/video 4/4 and SDK mock card-backend 1/1 without errors
  access, detect_leaks=0. After the last change, video-tests Release was repeated
  and ASan; behavior does not amount to leak testing.
- User-downloaded video H.264 10-bit 4:2:2, 1920×1080, 50/1, 144 frames:
  all 144 frames are received and transmitted to ImageView at 1920x1080. The original is not
  changed and was not added to Git. This is a local file check, not a stream SDK.

### Hardware diagnostics

On the connected ILCE-6700 the card directory has been read, Start/Play Movie has been executed
Playback, received StatusChanged/NormalTermination. No video bytes were received.
The kernel log for the same time confirmed UFW BLOCK TCP SYN from the camera on port 55001
computer; the socket/bind/listen trace confirmed the correct address and listeners
SDK ports. Network payloads and credentials were not recorded by the trace.

With the individual consent of the user through the system administrator dialog
added UFW address rule: incoming TCP 55001/55003/55005 from 10.0.1.5 to
10.0.1.84. Rollback - delete allow command in INSTALL.md. After recovery
The camera's connection to Wi-Fi has been re-checked hardware-wise:

- A 1280×720 SDK preview was received and decoded on the ILCE-6700 without ripping MP4.
- Pause, repeat Play, Seek for 1 s accepted by SDK; after Seek frames are received,
  There were 56 total in the script. There were no SDK/decoder errors. Position accuracy by timecode
  was not measured separately.
- StopComplete received; after monitor start the real Live View is read again
  JPEG 1024x576. Automatic transition of MainWindow is tested separately by fake backend.
- Hardware script log: build-package/hardware-movie-07.log, exit 0;
  Git credentials/images were not added.

Initial SSH ServerConnectFailed 0x8214 was reproduced when unavailable
camera address and disappeared after the user restored the network. This refusal is not
mixed with the reproduced blocking of the incoming UFW stream.

### Isolated installation

The official Ubuntu Base 24.04.5 amd64 has been re-unpacked, the archive has been verified by SHA256.
Apt installed DEB 0.6.1 with all dependencies from scratch. In bwrap without access to
GUI/backend, RU/EN launch, English versions have been checked for the working tree/source SDK
--help, ExifTool, H.265 encode/decode and RGB24/PPM output, ldd without missing ones
libraries. SDK loopback fails with expected ConnectIP/Code 2; camera and network
usernames are not used in this test. The GUI/backend hashes matched the package.
Passed 0.6.0 → 0.6.1 and removal; Host dependencies/services did not change.
Final package re-installed into the same image: GUI/backend hashes
matched DEB, RU/EN and SDK loader were checked, then the package was deleted. Helper from
the same GUI core rendered all 24 frames 1920x1080; a second clip took 1012 ms.
Final CTest 8/8, 38.65 pp. Log: build-clean/verify-final-0.6.1.log.

Artifacts: build-package/sonshell-remote_0.6.1-1ubuntu24.04_amd64.deb,
SHA256SUMS-0.6.1.txt, RELEASE-0.6.1.txt. Logs: build-package/release-0.6.1.log,
final-video-0.6.1.log, local-view-quality-0.6.1.log; build-clean/verify-0.6.1.log,
fresh-runtime-0.6.1.log, upgrade-0.6.1.log. Personal files, SDK and diagnostics from the camera
are not included in the repository.


## Version 0.6.2 - 2026-10-23

- Release CMake build and CTest: 8/8, 67 Qt scripts without init/cleanup plus check
  translation. SDK backend 2.02 is built headless and with OpenCV.
- The spoofed SDK checks the order AF_Area_Position → S1 Locked → S1 Unlocked,
  absence of S1 in case of coordinate/point setting error, shutdown in MF/unavailable
  S1, release after failure to press and repeat after failure to release. S1 is not
  available via generic controls set. GUI shows separate AF failure after
  moving the point and keeps the error after updating the snapshot.
- Controls GUI and backend repeated under ASan: 2/2 sets without access errors,
  detect_leaks=0. This is not a leak test.
- Attempt to hardware check through the same CameraControlsWidget/CameraSession
  did not reach AF: camera 10.0.1.5 is unavailable, SDK SSH ServerConnectFailed 0x8214.
  The user later confirmed the fix worked (2026-10-24), no list
  specific modes. The full hardware AF matrix remains open.
- DEB 0.6.2 was installed/updated 0.6.1 → 0.6.2 in an isolated
  Ubuntu Base 24.04.5 amd64 with runtime dependencies installed when clean
  check 0.6.1. Dependencies did not change. RU/EN, versions, SDK loader,
  hashes of installed binaries and removal are verified. Host Settings/Services
  did not change; helper output 24 frames 1920×1080 in 1013 ms.

Artifact: build-package/sonshell-remote_0.6.2-1ubuntu24.04_amd64.deb. Logs:
build-package/release-0.6.2.log, build-asan/af-tests.log,
build-asan-backend/af-tests.log, build-clean/verify-final-0.6.2.log.

## Version 0.6.3 - 2026-10-24

- Release CMake build and CTest: 8/8, 71 Qt script without init/cleanup plus check
  translation, 46.11 p. Sony SDK 2.02 backend is built headless and with OpenCV.
- GUI checks: drag result only after release, merging the wheel into one
  command and cancellation when blocked; manual ISO/shutter speed/f-number/negative EV,
  comma and ones, unsupported input and Esc. Callback does not reset the draft;
  disable and read-only block control. ISO field geometry tested for
  AUTO in the main window. RU/EN window viewed offscreen using screenshots;
  Horizontal scrolling of the panel has been eliminated by increasing the minimum width.
- SDK fixture checks real 16/32-bit encodings, Int16Array EV, negative
  current EV, scalar record types, value re-validation/read-only.
- Controls GUI and backend under ASan without access errors; detect_leaks=0, leaks
  are not checked by this run. Logs: build-asan/exposure-tests.log,
  build-asan-backend/exposure-tests.log.
- Physical camera was not used to change exposure: working session
  user is left open. Hardware acceptance of dials remains open.

### Clean installation and update 0.6.3

The official Ubuntu Base 24.04.5 amd64 has been re-unpacked, the SHA256 of the archive has been checked.
In an empty image, apt installed 0.6.3 and 249 dependencies; previously downloaded DEB
used only as a cache; there were no runtime dependencies installed in the image.
In bwrap without access to sources/SDK/network, GUI/backend versions 0.6.3 have been tested,
RU/EN launch, English --help, Qt translations, ExifTool, H.265 encode/decode and RGB24/PPM,
ldd without missing libraries. SDK localhost failed as expected
ConnectIP/code 2. The hashes of the installed GUI/backend matched the DEB.
Completed uninstallation, installation 0.6.2 → update 0.6.3 and uninstallation again.
The host settings/services and the original prototype were not changed.

Artifacts: build-package/sonshell-remote_0.6.3-1ubuntu24.04_amd64.deb,
SHA256SUMS-0.6.3.txt, RELEASE-0.6.3.txt. Logs: build-package/release-0.6.3.log,
final-ui-0.6.3.log; build-clean/verify-0.6.3.log, fresh-install-0.6.3.log,
fresh-runtime-0.6.3.log, upgrade-0.6.3.log. RU/EN pictures of the final window:
build-package/final-ui-0.6.3/ (synthetic camera, no personal photos/passwords).

## Version 0.7.0 - 2026-10-24

### Checks without a camera

- CMake Release, CTest 8/8: 80 Qt scripts without init/cleanup and translation checking.
  The SDK backend is built headless and with OpenCV, additionally OpenSSL EVP is used.
- SDK fixture: known SHA256 content, cancel during hashing without
  publications, JPEG original via preview, prohibition of RAW/MP4 in original JPEG mode.
- GUI: corruption at the same size/mtime, re-check does not replace
  standard, repeated copying saves the damaged file. Old ledger without
  the amount remains “without a standard”. Local scanning is available without a connection;
  cancel does not send the SDK command. After a successful copy, the line/marks are saved.
- JPEG → stop on RAW → disconnect/restart → new content-id → continue
  RAW only, clearing the completed queue. The other camera/identity is rejected.
- Full-size studio JPEG 6192×4128, RAW-only fallback, no copies
  in destination directory, delete temporary JPEG, return to Live View, full
  SDK path in status. Two shutter releases during a delayed preview show the latter
  frame; repeated callback does not load the same frame again.
- ASan: remote/media/camera-card and card-backend without access errors,
  detect_leaks=0 (leaks were not checked by this). Final media set repeated
  after adding the studio path. RU/EN window shots were checked offscreen.

### ILCE-6700 Hardware Test

In a separate CameraSession/CameraCard with the new GUI core, 356 entries were read.
Copying an existing RAW+JPEG entry to QTemporaryDir: JPEG 5,195,156 bytes,
then stopping the queue after the first file (pending=1). SDK process stopped
connection restarted, “Continue import” received only RAW 32,325,632
byte. Result: two copies, queue empty, local SHA256 check - OK 2,
altered/missing/no reference 0. JPEG was not copied again.

The original JPEG of the same recording was received via preview ... jpeg: 5,195,156 bytes,
6192x4128. In the test Qt ImageViewer, the timer was 50 ms; maximum
interval 56 ms, 310 operations for the entire scenario with reconnection. This
Measuring a given run does not guarantee latency on all cards/machines.
No physical shutter release/series was performed in this run: an existing one was used
frame. These scenarios remain a separate practice; automated tests simulate them.
The camera files and shooting mode did not change, the test copies were temporary.

The working GUI was closed normally to free the connection, then it was launched
again and the connection is restored. Global settings/services have not changed.
Hardware run logs contain private diagnostics only in the ignored
build-package/hardware-import-0.7.0.log, hardware-import-run-0.7.0.log.

### Installation

New Ubuntu Base 24.04.5 amd64 unpacked with SHA256 check. DEB and dependencies
installed via apt from scratch (previously downloaded packages are used only as a cache).
In bwrap without sources/SDK/network, RU/EN launch, versions, Qt translations are checked,
ExifTool, FFmpeg H.265 and RGB24/PPM, ldd, SDK localhost with expected ConnectIP/code 2.
Runtime helper on the same GUI core checked the local SHA256 without a camera and found
damage at the same size/mtime. The hashes of the installed GUI/backend matched
with DEB. Update 0.6.3 → 0.7.0 and removal completed.

The final package is re-installed into the same image after the last amendment
studio way. RU/EN, SDK loader, binary hashes, local SHA256 and deletion
re-checked; The DEB checksum is verified separately. Final CTest
8/8, 56.90 s. 265 translation records, 256 used strings. Final ASan media
12/12 with init/cleanup; other affected GUI/SDK sets have also been passed.

Artifacts: build-package/sonshell-remote_0.7.0-1ubuntu24.04_amd64.deb,
SHA256SUMS-0.7.0.txt, RELEASE-0.7.0.txt. Logs: build-package/release-0.7.0.log,
final-ui-0.7.0.log; build-asan/import-tests.log, studio-tests.log;
build-asan-backend/import-tests.log; build-clean/verify-0.7.0.log,
verify-final-0.7.0.log, final-runtime-0.7.0.log. Personal data and SDK are not added to Git.

## 0.7.1 - card viewing quality, 2026-10-24

### Automatic checks

- Final Release: CMake GUI + SDK headless, backend OpenCV, CTest 9/9,
  61.08 s. 88 Qt scripts without init/cleanup and separate checking of translations.
  Catalog EN: 278 entries, 268 used lines, placeholders are the same.
- New card-quality-tests: original JPEG 6192x4128 and pixels instead of compressed
  320x180, 100%/full screen; RAW+JPEG transmits only JPEG, RAW-only only
  by button and with the best built-in JPEG; original MP4 1920×1080 instead of thumbnail,
  local decoder frames in original size; pause/stop when changing tabs.
- Re-opening from cache without transfer, displacing the previous temporary file,
  saving the imported copy and opening it without SDK, canceling with progress
  in full screen, protection from outdated results when scrolling quickly.
  Viewing does not create a ledger/import queue and does not change the “New” attribute.
- Backend: original uses DataFile for RAW/MP4, saves extension,
  rejects XML, incomplete original is not published, cancellation is confirmed by SDK mock.
- ASan: card-quality-tests 8/8 and card-backend-tests 22/22 with init/cleanup,
  no access errors; detect_leaks=0 for Qt/offscreen, this is not a leak check.

### ILCE-6700 Hardware Test

A directory of 695 entries was read using SDK 2.02. The regular GUI was temporarily closed
to release a single session; check used saved ones
settings without displaying credentials and a separate temporary directory.

- Selecting an existing RAW+JPEG in CameraCardWidget opened only JPEG:
  6,139,791 bytes, 6192x4128. The size matched the catalogue; original pixels are shown.
- An existing MP4 of 67,383,474 bytes is selected. First thumbnail, then explicit button
  original: first frame and five frames of playback 3840x2160. This is a separate
  full MP4 transfer rather than upscaling the 1280x720 SDK stream.
- JPEG and MP4 reopening used cache: three transfer events in total
  (JPEG, MP4, MP4 thumbnail), there were no re-uploads of the originals.
- 50ms GUI timer worked 306 times; the maximum interval is 117 ms in this run.
  No import log was created. The shooting/deleting/recording mode of the camera did not change.
  The proof originals have been removed from the temporary directories.

Working GUI restarted on final build, executable file matches
checked with current build. When restoring the session after checking the camera
stopped responding over the network; Automatic reconnection is left enabled.
This does not cancel the above successful file verification, but reconnecting
not confirmed in the production GUI in this state. Global settings have not changed.

### Installation

The final DEB is installed via apt with dependencies into the new Ubuntu Base 24.04.5
amd64 with verified SHA256; previously downloaded DEBs are used only as cache.
In bwrap without sources/SDK/network, RU/EN GUI, version 0.7.1, Qt translations have been tested,
ExifTool, FFmpeg H.265/RGB24/PPM, ldd, SDK localhost (expected ConnectIP failure,
code 2) and a local SHA256 helper that detects corruption with the same size/mtime.
The hashes of the installed GUI/backend matched the contents of the DEB. Removal completed
installing the previous 0.7.0, updating to 0.7.1 and uninstalling again.

Artifact: build-package/sonshell-remote_0.7.1-1ubuntu24.04_amd64.deb,
5,568,786 bytes; SHA256 95fcf6afe8c06b4a29e44ea2faf2c176ac627f8d02f5ad1e32778101dac10259.
Original commit d3578f9; SHA256SUMS-0.7.1.txt and RELEASE-0.7.1.txt next to DEB.
Logs: build-package/package-0.7.1.log, asan-quality-071.log,
asan-backend-071.log, hardware-quality-0.7.1.log;
build-clean/verify-release-0.7.1.log, fresh-install-0.7.1.log,
fresh-runtime-0.7.1.log, upgrade-0.7.1.log. SDK, photos and private logs have not been added to Git.

## 0.8.0 - connection and recording parameters, 2026-10-25

### Automatic checks

- CMake Release GUI + SDK backend headless, separate OpenCV backend - successful.
  Final CTest 9/9, 70.04s: 101 Qt scripts without init/cleanup plus check
  translations. 296 EN entries, 286 usable lines; placeholders matched.
- Auto-connect: saved access/conditions, unchecked, unsaved
  data, empty username/password, unaccepted conditions, incorrect IPv4. If you miss it, no
  backend/modal window. Start is canceled by flag, closing, early manual
  connection/disconnection; two connections do not start. There is no password in argv/log.
- New fields: actual values, UInt8Array without sign distortion,
  other types/EV without regression, incorrect length choices, read-only/unsupported,
  format dependence RAW+JPEG → RAW+HEIF. After Applied, the fields wait for Snapshot;
  an already queued event does not send a second entry. Transfer
  blocks changes. StoreDestination and codes outside the list are rejected by the backend.
- ASan: remote-tests 18/18, controls-tests 16/16, controls-backend-tests 12/12
  with init/cleanup, no access errors; detect_leaks=0, leaks were not checked by this.
- During a full run, a false timeout of the old video test was detected: QTRY/qWait pauses
  between readyRead the large RGB pipe was artificially slowed down. Individual worker
  the loop played back all the frames. The check has been replaced with a continuous QEventLoop with
  same deadline 10 s. 20 repetitions of 1.24–1.33 s: 24 frames 1920×1080,
  exact RGB match and tempo are preserved. Production VideoPreview has not changed.
- The interface was checked visually at 1280×870 (RU, offscreen) and 1779×1299 (EN,
  standard dark theme): large blue button and focus, four connection flags,
  collapsible photo/video sections, readable captions.

### ILCE-6700 Hardware Test

After user confirmation, the camera was available. Working GUI at the moment
the first scan run was missing. The test used MainWindow, its
start timer and saved access copied to private temporary
settings; the target directory was also temporary. Credentials were not displayed.

- Connection without clicking worked exactly once, Live View started.
- Of the 11 new fields, 10 are readable/writable: FileType, CompressionFileFormatStill,
  StillImageQuality, ImageSize, AspectRatio, RAW_FileCompressionType,
  ImageStabilizationSteadyShot, Movie_File_Format, Movie_Recording_FrameRateSetting,
  Movie_Recording_Setting. Movie_ImageStabilizationSteadyShot in current mode
  unavailable (read/write=false, choices=0) and properly blocked.
- Values and captions obtained from SDK: RAW+JPEG, JPEG, Fine, L, 3:2, Lossless,
  On, XAVC S HD, 50p, 50 Mbps 4:2:2 10-bit. Setter ImageSize with its own
  current value of L; Applied and subsequent reading confirmed the same meaning.
- Manual disconnect ended the session, after 500ms it remained disconnected.
  Shooting/deleting/changing formats was not performed. Real switching matrix
  photo/video, dependencies of modes, frequencies and other models have not yet been completed.

The working GUI is then launched with normal settings. After the final build again
restarted normally; the launched executable file coincided with the final build.
Global settings/services have not changed. You can disable auto-connection with your
checkbox in the window; this does not change the individual replay setting after a break.

### Installation

The final DEB is installed via apt with dependencies into the new Ubuntu Base 24.04.5
amd64 with verified SHA256. The cache of previously downloaded packages was used only for
delivery of dependencies. In bwrap without sources/SDK/network, RU/EN launch is checked,
version 0.8.0, Qt translations, ExifTool, FFmpeg H.265/RGB24/PPM, ldd, SDK localhost
(expected ConnectIP/Code 2), local SHA256 helper. GUI/backend hashes in the root
coincided with DEB. Uninstallation, installation 0.7.1, update to 0.8.0 and removal completed.

Artifact: build-package/sonshell-remote_0.8.0-1ubuntu24.04_amd64.deb,
5,578,506 bytes; SHA256 4913a209222aa1a0ee35adbf212c8a931242bfc59327617d36da657999676bd9.
Original commit c55734f (8bdeb9d implementation), RELEASE-0.8.0.txt and
SHA256SUMS-0.8.0.txt next to the package. Logs: build-package/ctest-final-080.log,
asan-remote-080.log, asan-controls-080.log, asan-backend-080.log,
hardware-controls-0.8.0.log, hardware-controls-run-080.log,
video-eventloop-repeat-080-1.log … -20.log, package-0.8.0.log;
build-clean/verify-release-0.8.0.log, fresh-install-0.8.0.log,
fresh-runtime-0.8.0.log and upgrade-0.8.0.log. No private data/SDK added to Git.

## 0.8.1 - videos, overlays and Settings, 2026-10-25

### Automatic checks

- CMake Release GUI + SDK backend assembled. Full CTest offscreen 9/9, 73.11 p.
  After restricting the re-reading of the directory when closing Settings, repeated
  affected remote/video: 2/2, 19.12 sec; closing without changing directory does not recreate
  lines and does not interrupt local video. The set contains 105 Qt scripts without init/cleanup.
- Play itself downloads the original and starts showing; replay does not transmit and does not
  launches SDK movie. The canceled result does not run after flipping;
  switching tabs stops the player. SDK mode is checked separately.
- Access/catalog fields are hidden in Settings, catalog/studio/quality changes
  the cards are saved. Modelless window saves connection; access fields disabled
  to Disconnect. The menu shows the log. Space/Delete check has been added
  position of inscriptions at the edges and transparency; Delete does not send a delete.
- ASan GUI without SDK: remote/card/card-quality/video 4/4, 32.90 s; after the last one
  edits remote/video 2/2, 18.93 p. There are no access errors; detect_leaks=0,
  the inspection is not declared a leak audit. Catalog EN: 313 signatures, 299 in use.

### Hardware check separate from substitution

- ILCE-6700: SDK MoviePlayback of two existing videos C0048 and C0010 gave
  audio and NormalTermination, there were no video frames. Call only route
  bind/listen/accept/connect confirmed the connection of the camera with all incoming
  TCP 55001/55003/55005. Payload/credentials were not recorded. Replay with Seek
  did not restore the flow; The reason inside the SDK/camera is not identified. Settings
  networks, recording formats and camera files have not changed.
- The main Play in the final build MainWindow loaded the original
  67,818,488 bytes and automatically showed 1920x1080; pause/continue,
  stopping and retrying from the same cache is successful. There are 12 frames in the script, only two
  transfers (thumbnail and original), the import log remained empty.
- Space/Delete set both checkboxes to fullscreen; the inscription “Marked as defective”
  below (y=617, viewport height=682), constant marks above. Screenshots
  Settings/main window/overlay are inspected and left in the ignored build.
  The delete command was not executed. UI heartbeat 50ms lasted: 293 events,
  maximum interval 191 ms. This is one video/network, not a guarantee of all formats.
- Separate check of viewer return from fullscreen in QSplitter preserved dimensions
  582/592 and visibility. Supporting sources/logs and data are not added to Git.

### Package and clean Ubuntu

- DEB 0.8.1-1ubuntu24.04 amd64: 5,583,032 bytes, original commit `50894fa`.
  SHA256 `2cfcbec288c7b1447f7cae2f35f63ec31f82786b6ec0dd74a5eea9d78424bd7b`.
- Fresh Ubuntu Base 24.04.5 amd64: apt installation with dependencies from scratch.
  In bwrap without sources/SDK, GUI RU/EN, help/version, are launched from the host/network
  ExifTool, FFmpeg H.265/PPM; Qt translations and ldd have been checked. SDK localhost
  gave the expected ConnectIP/exit 2: this is an SDK download, not a connection to the camera.
- The installed binaries matched DEB by SHA256; runtime helper detected
  Damage to the imported copy with unchanged size/mtime. Update
  0.8.0 → 0.8.1 and removal successful. Host packages/services have not changed.
- Logs: build-clean/fresh-install-0.8.1.log, fresh-runtime-0.8.1.log,
  upgrade-0.8.1.log, verify-release-0.8.1.log; hardware log
  build-package/hardware-preview-0.8.1.log. SHA256SUMS/RELEASE next to DEB.

## 0.8.2 — About and NDRco, 2026-10-25

- CMake Release GUI + SDK backend assembled; GUI without SDK built with ASan.
  CTest offscreen 9/9, 70.80 s; the composition is the same, 105 Qt scripts without init/cleanup.
  The new low-risk screen was tested visually without adding text tests
  constants Catalog EN: 316 signatures, 301 in use.
- About RU/EN is opened through a real QAction MainWindow in an isolated
  configurations; showing a PNG from Qt resources, version 0.8.2 and all four URLs.
  Double call raises one window, Close releases it, reopens
  creates a new one, closing MainWindow closes About. ASan without access errors,
  detect_leaks=0. Screenshots of both languages ​​580×359 viewed, logo 112×112;
  all texts are included. This is an offscreen interface test, not a hardware test.
- The icon was downloaded from the ndrco public profile without image conversion;
  source, date and SHA256 are recorded in gui/assets/README.md. Network when showing About
  not used; browser transitions were not performed during the scan.
- GitHub CLI 2.45.0 is already installed and authorized as ndrco via keyring.
  users/ndrco successfully returned profile/icon; repo view for ndrco/SonShell-Remote
  returned Could not resolve. The repository was not created, the push was not performed;
  no CLI installation or global configuration changes were required.
- DEB 0.8.2-1ubuntu24.04 amd64: 5,627,536 bytes, commit `a6fa9f8`.
  SHA256 `0c6ff20403d38a3488c84627dcd72610cd07eb91b08d5f20d77ce534ee9b8b24`.
- Clean Ubuntu Base 24.04.5: apt installs dependencies; GUI RU/EN,
  help/version, Qt translations, ExifTool, FFmpeg H.265/PPM, SDK loader, ldd
  and SHA256 control of the imported copy took place in isolation. Installed
  The binaries are the same as DEB. Update 0.8.1 → 0.8.2 and removal successful.
- The first launch of the verifier showed the successful completion of all checks, but it
  shell exited SIGTERM/143; the check was completely repeated in the new root
  rootfs-0.8.2-recheck without managing the desktop window, resulting in exit 0.
  Logs build-clean/verify-release-0.8.2-recheck.log, fresh-install-0.8.2-recheck.log,
  fresh-runtime-0.8.2-recheck.log, upgrade-0.8.2-recheck.log; SHA256SUMS/RELEASE
  next to DEB. System packages and host services have not changed.

## 0.8.3 - navigation, rewind and progress, 2026-10-26

- CMake Release GUI + Sony SDK backend assembled; CTest offscreen 9/9,
  74.81 s. GUI without SDK: ASan remote/card/card-quality/video 4/4, 37.31 s,
  detect_leaks=0; No memory access errors were detected.
- Synthetic 13 second MP4: rewind from current position after
  playback, pause saving, one new frame after seek, repeated fast
  pressing, boundaries 0/last frame, Stop. The pixels of the frame coincided after +5 s
  with independent FFmpeg decoding at the requested mark. Previous check
  the original 1920×1080/24 fps and RGB without recoding also passed.
- Tested gallery, separate window/fullscreen and SDK mock stream with seek commands.
  Navigation takes into account selected files; change during transfer of the original
  sends cancel, does not import the file and does not trigger late autoplay.
- The original indicator in fullscreen showed 45%, saved the value after exit
  and re-entry; cancel was available and hid the bar without a completed file.
  RU/EN screenshots viewed at 800×800: buttons for adjacent files at the top,
  rewind at the bottom, text/controls are placed. Artifacts in build-package are ignored.
- These are checks without a camera. SDK seek accuracy has not been tested in hardware;
  its position is estimated from the decoded stream, the camera takes whole seconds.
- DEB 0.8.3-1ubuntu24.04 amd64: 5,635,432 bytes, commit `82860b8`.
  SHA256 `d33804a333ef2558dc910a168b12c433e2f9c6f21c94646931d58faa459f011d`.
- Fresh Ubuntu Base 24.04.5: apt installed the package and dependencies from scratch.
  In an isolated bwrap without host sources/SDK/network, GUI RU/EN is running,
  help/version, ExifTool, FFmpeg HEVC/PPM; ffprobe read duration=8 s from MP4.
  Tested Qt translations, SDK loader/ldd (localhost ConnectIP/exit 2),
  SHA256 of installed binaries and detection of corruption of the local copy.
  Update 0.8.2 → 0.8.3 and removal successful; the verifier exited with exit 0.
  Logs build-clean/verify-release-0.8.3.log, fresh-install-0.8.3.log,
  fresh-runtime-0.8.3.log, upgrade-0.8.3.log; RELEASE/SHA256SUMS next to DEB.
  The host packages/services and the original prototype were not changed.

## Audit 0.8.3 - 2026-10-31

- Verification of the original task and clarifications with the code and saved hardware
  the results are recorded in the documentation. Historical points of the plan
  separated from the current balance of work; The application code has not changed.
- Re-build of CMake Release GUI and SDK backend is successful. CTest offscreen:
  9/9, 80.89 pp. Log build-package/audit-2026-10-31-ctest.log, outside of Git.
- SHA256 and local DEB 0.8.3 size matched the release result above.
  ASan, clean installation and update were not repeated this time.
- GitHub CLI 2.45.0: authorized user ndrco, repo view specified
  addresses returned Could not resolve. Repository creation/push was not performed.
- There were no new hardware checks; settings, camera files and running
  the application has not been changed.

## Publication GitHub - 2026-10-31

- The following command from the owner created PUBLIC ndrco/SonShell-Remote, main.
  The history has been saved, the README with a screenshot is publicly available; Apache-2.0
  recognized by GitHub. Metadata, HTML README and HTTP 200 images are checked.
- Gitleaks 8.30.1: HEAD history, zero finds; The composition of the paths was checked separately
  on SDK/runtime, snapshots, settings and fingerprints. The tool itself and the logs are outside of Git.
- Public cloning over HTTPS without authorization is successful. README files
  LICENSE and the screenshot matched the local ones, byte-by-byte. Screenshot - real UI
  with SDK-free backend, without camera/personal data; PNG 1640×1080 inspected.
- GCC 13.3.0 was compiled from a fresh clone `5f0a680` Release GUI without SDK; CTest
  offscreen 7/7, 69.39 pp. lrelease is set to a private local path via
  QT_LRELEASE_EXECUTABLE: There is no system lrelease on this machine.
- SDK backend/hardware scripts and DEB were not changed at this stage.

## 0.8.4 — English default and bilingual documentation, 2026-10-31

- GUI and SDK backend built successfully. Final CTest offscreen 9/9, 75.29 s;
  ASan remote/card/card-quality/controls/video 5/5, 49.73 s, detect_leaks=0.
- English default/fallback, retained saved Russian preference, both help resources,
  Qt system buttons and source/catalog placeholders verified. English/Russian
  screenshots inspected. Unicode path fixtures retain their runtime values.
- English documentation, retained _RU counterparts, links and excluded-document
  references checked. The owner's PNG was copied unchanged; SHA256
  `87f393aab8d2b17bfb1f3937900c55d09d51d26e0c34200d7acbee8216402d69`.
- DEB 0.8.4-1ubuntu24.04 amd64: 5,634,870 bytes; source `43b9b6e`;
  SHA256 `fae4c4c58704a7108525037d4b48d05317384b19373237187cb4885d05e93f19`. Contains both languages of INSTALL/USER_GUIDE/THIRD_PARTY,
  localized desktop metadata, current official Sony license HTML and OSS notices.
- Fresh Ubuntu Base 24.04.5: apt installation, isolated English/Russian GUI/help,
  SDK loader, FFmpeg/ffprobe, installed binary hashes, content-integrity check,
  upgrade 0.8.3 → 0.8.4 and removal passed. The initial minimal-image run stopped
  at the documentation check because Ubuntu Base excludes /usr/share/doc by
  default; a new root included this package's documents and completed with exit 0.
- Logs: build-translation/ctest-final-084.log, asan-ctest-084.log;
  build-clean/verify-release-0.8.4-final.log, fresh-runtime-0.8.4-final.log,
  upgrade-0.8.4-final.log. No host configuration or camera files changed;
  no new physical-camera acceptance run. Gitleaks history scan found no secrets.
