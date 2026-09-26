# TC002 port validation

Latest validated candidate: **1.1.0-tc002.4**, with 112 passing pytest cases and
one passing hardware CTest. The sections below record successive validation
rounds; earlier version numbers and test counts describe those earlier rounds.
Paths under `device-private/` and `dist/` refer to local evidence and artifacts
that are not included in the public repository.

Upstream: AWTRIX NG 1.1.2, commit `6d6cc64aa6739724d8501199de70c6692cbb2c6c` (earlier rounds: 1.1.1, `73b4582e157484a737397bb0fa616da62212fe9e`; 1.1.0, `4ff1de83428ed13cae6e210dfcbf9d186a09bc60`).
Device: TC002 stock app 1.1.1, MCU V1.0.17, ARMv7 SSD202D, glibc 2.30.
Validation date: 2026-09-15. Permanent installation and startup after Linux reboot passed.

## Permanent installation

- Installed `update.img` SHA-256
  `82eb8251acbea8194ad56311c45573b8eb26667513e7ab8520d817aeee5ee2dd`.
- The updater wrote the res partition and verified every erase block by read-back,
  then rebooted Linux. Its successful completion is recorded privately in
  `device-private/installation-success.log`.
- After reboot, Linux uptime was 201 seconds and the process ran directly from
  `/res/bin/awtrix-tc002`, with persistent settings under `/data/awtrix-ng`.
  The API reported version 1.1.0-tc002.1, 42 FPS and roughly 16 MiB available RAM.
  Wi-Fi reconnected and NTP synchronized. The web service listens on port 80.
- Browser checks against the installed firmware passed: no JavaScript errors,
  52 × 16 preview and the correct fixed-matrix controls. Screenshot:
  `dist/webui-installed.png`.
- Initial install attempts safely aborted before erasing because the stock
  `/res/bin/hciattach` Bluetooth helper kept `/res` busy. The updater now waits
  for GUI shutdown, sends the helper SIGTERM, waits two seconds and uses SIGKILL
  only if the same PID still runs that exact executable. A no-write diagnostic
  unmount/remount passed before the final installation.
- MQTT is disabled until a broker is configured in the web UI.

## Confirmed on the physical clock

- Full AWTRIX app: correct, steady 52 × 16 color bands and border, confirmed by owner.
- GPIO 35 direction low/high, 1 ms before and after SPI write, 15 ms idle.
  SPI mode 0, 8 bits, MSB first, 10 MHz; RGB rows padded to 64 pixels (3072 bytes).
- Normal application maintains approximately 42 FPS; final RAM trial reported
  about 14 MiB available memory. Stock and AWTRIX never own the display together.
- Two sets of three rising RTTTL notes, clearly heard by owner.
- Generated stereo MP3 and HTTP MP3 stream: playback stayed active for the clip
  duration and completed without errors at zero volume. A shared-speaker stop-scope
  bug found by this check was fixed and the check passed afterward.
- Left/right buttons and rotary navigation both work, confirmed by owner.
- MCU battery percentage and reported millivolts, live Wi-Fi status and scan results.
- HTTPS status 200 with certificate verification, using statically linked OpenSSL
  3.5.8 and the public Mozilla CA bundle. Stock OpenSSL 1.1.0i could not negotiate
  TLS 1.2 and is not used for AWTRIX HTTPS.
- mDNS service response includes the correct hostname, UID and web port.
- NTP synchronization logged during the final launcher trial.
- 21 ARM integration cases passed: MQTT topics/replies, retained state, Home
  Assistant discovery, pushed apps, notifications, settings, display, audio
  command validation, silent unknown topics and fixed-wiring rejection.
- Web dashboard and System page pass browser checks: 520 × 160 preview,
  52 × 16 = 832 fixed-matrix badge, no ESP32 GPIO controls, no JavaScript errors.
- Streaming update endpoint rejects an invalid image with HTTP 422; no writes.
- Actual FlyThings launcher tested from a temporary bundle with the final app,
  TLS and descriptor cleanup. Stock is restored after each bounded trial.

- Update helper preflight passed on the real 8 MiB NOR res partition, with no writes.

## Local checks

- Complete local pytest suite: 62 passed; CTest: 1 passed.

- CTest hardware packing/parser test: passed.
- Firmware-image tests: 38 passed, including the independent native validator.
- Host transport tests: Art-Net fills all five universes / 832 pixels; platform
  capabilities are correct; HTTP authentication survives configuration restart.
- Original stock image reconstructs byte for byte. Both generated images pass
  native container validation. Image checks cover CRC, MD5, target, exact length,
  truncation and filesystem bounds.
- Final updater links only root-filesystem runtime libraries. It is copied to
  /tmp before use, validates before stopping the GUI, requires /res to unmount,
  and reads back each flash erase block before continuing.

## Remaining release validation

- Physical power-cycle startup and restoring the stock recovery image have not
  been exercised. Installation and automatic startup after Linux reboot passed.
- 1.1.0-tc002.9 has not been installed on a clock yet. New and untested on hardware:
  the knob-hold and three-strikes fallback to the vendor application, firmware
  staging under `/data/awtrix-ng/staging`, the preflight-before-install step, and
  the vendor fingerprint gate. Run docs/VALIDATION.md before tagging it.
- Wi-Fi credential changes, static addressing and fallback access-point mode are
  implemented but untested physically; tests preserved the owner's connection.
- A real external radio station, playlist and live ICY metadata have not been
  verified end to end. Local HTTP MP3 streaming and HTTPS transport pass separately.
- Long-running stability and battery-powered operation have not been soak-tested.

## Intentional platform differences

- Fixed matrix and GPIO mapping; mirror/rotate and button swapping remain available.
- No ambient-light, temperature or humidity sensor; manual brightness is used.
- Knob rotation maps to previous/next app; pressing it maps to select.
- Sleep blanks the panel and pauses services, then restarts the app. It is not
  hardware deep sleep. Device reboot restarts AWTRIX's process.
- Firmware update images replace only res, preserving Linux, bootloader and MCU.

## Display diagnosis

Changing transfer timing alone did not fix corruption. An isolated test using
installed Ulanzi SpiHelper/GpioHelper worked with the same packed pixels.
Tracing showed GPIO direction writes and SPI setup/readback order differences.
Matching both fixed the full application. The individual cause was not isolated;
preserve the user-confirmed sequence.

## RAM test housekeeping

Old temporary test binaries initially consumed almost 7 MiB of RAM filesystem.
A large TLS diagnostic upload then prevented new ADB shell processes. Truncating
that diagnostic through ADB sync freed memory, and removing the obsolete test
copies restored /tmp to about 0.3 MiB. Trial tools now remove their binaries when
finished. The firmware update handler streams uploads to avoid duplicate bodies.


## 2026-09-15: display scaling correction (1.1.0-tc002.2)

The original port changed the physical matrix geometry but left upstream built-in
layouts, notification fonts, page/script icon buffers and the GIF decoder at
32 × 8 / 8 × 8. The icons page/editor also still advertised legacy sizes.

Changes:

- Fit built-in layouts to 52 × 16; extend the source frame for long date/time
  formats before fitting. Keep Berry canvases and explicit draw commands native.
- Expand both notification/pushed-app fonts 2×, including Unicode glyphs and
  metrics used by centring and scrolling. Reserve each icon's actual width.
- Decode GIFs/JPEGs through 52 × 16, including animated/full-screen and inline
  assets. Enlarge legacy page icons automatically; retain native script sizes.
  Grow GIF LZW scratch to the standard 4096 entries.
- Fix progress placement/thickness, indicator/link-dot size, long/dense bar chart
  coverage, boot/provisioning presentation and the fixed-size LookingEyes effect.
- Update icon upload limits, both UI languages, editor presets/default, live editor
  bitmap conversion and initial dashboard aspect ratio. Reject oversized uploads
  in the browser; preserve pixel edges during conversion.

Validation before installation:

- 85 pytest cases passed (23 new pixel regressions) and the hardware CTest passed.
- Real Chrome: PNG→GIF upload and display at 16 × 16 and 52 × 16; inline 52 × 16
  JPEG last pixel; oversized image rejection; matching still previews for legacy
  and native icon sizes; no page JavaScript errors.
- Actual hosted Piskel runtime confirmed presets 16x16/52x16/8x8/32x8 and a 16 × 16
  default canvas. The live dashboard canvas is 520 × 160 with a 52:16 aspect ratio.
- ARM build and host/native image validation passed. Device preflight verified
  the matching 8 MiB NOR res partition without writing.
- Previous AWTRIX image saved as device-private/installed-tc002.1.img; current user
  configuration backed up in device-private/pre-scaling-data. Stock recovery image
  remains byte-identical (SHA256 8d66b54a594c9a173ebf8a0267dbc4bb71d8ffdbcae075c52ea5a4ad651cafb3).

Scaling policy and the physical-coordinate script/draw distinction are documented
in README.md. MQTT topics, schemas and explicit pixel coordinates are unchanged.

Post-installation verification:

- Installed update SHA256 01fc60edf73cf25d96fb89ac7a1298c71c4f62d5ab59c88a962eecb9cf498dd4
  (4,371,004 bytes). Updater reported every flash block read-back verified and
  rebooted Linux. `/proc/uptime` was 120 seconds during the post-install check.
- Version 1.1.0-tc002.2 starts from `/res`, reports 42 FPS, and reconnects to the
  owner's existing MQTT broker and NTP. Both persistent configuration files are
  byte-identical to their pre-update backups.
- Installed ARM renderer: every pixel matched expected results for 8 × 8→16 × 16,
  native 16 × 16 and full 52 × 16 GIF icons; notification glyph height/centring and
  Time/Date/Battery full-height layouts verified through the live screen endpoint.
- Real browser verified the served updated icons/editor UI and 52:16 preview.
  Screenshot: dist/webui-installed.png. Live pixel captures:
  device-private/scaling-live-screens.json.
- These are device framebuffer checks, not a new owner confirmation of the physical
  panel appearance. The previously owner-confirmed GPIO/SPI driver is unchanged.


## 2026-09-15: approved native clock/date/battery layouts (1.1.0-tc002.3)

The owner reviewed interactive previews and approved all three before implementation.
The final clock adjustments were: calendar rows 3–4 white (two red header rows),
day number up one row, time left one column, and weekday indicators left one column.
The day number remains horizontally centred; its temporary left shift was reverted.

Added Tc002Layout native rendering through an optional IApp renderNative hook.
The three approved layouts bypass the fractional 32→52 framebuffer stretch; other
built-ins and unsupported alternate formats retain the previous path. Existing
settings, date formatting, separator effects, weekday colours and battery alarms
still apply. No MQTT topics or schemas changed.

Validation before installation:

- Frozen 18 expected frames directly from the approved browser previews: four
  clock/day combinations, four dates (including leap day), and ten charge levels
  spanning 0%, 100% and the red/amber/green boundaries.
- The actual C++ built-in renderers match every pixel of all 18 approved frames.
- All 103 pytest cases passed, including MQTT/HTTP, native drawing/script geometry,
  decoder limits and image validation. The hardware CTest also passed.
- Previous installed image saved as device-private/installed-tc002.2.img.
  Current settings/assets backed up under device-private/pre-approved-layouts-data.

Post-installation verification:

- Installed 1.1.0-tc002.3; update SHA256
  2c103ee4c5e1c25d97329137059e08d252e616adc9c9aec0541fa6a757c2e83d
  (4,371,004 bytes). Every flash block read-back verified; Linux reboot completed.
- Live installed Time, Date and Battery framebuffers match the deterministic C++
  renderers verified against the approved browser previews. The clock's pulsing
  colon was checked separately for consistent colour and placement.
- 42 FPS, MQTT connected, NTP synchronized; both device.json and settings.json are
  byte-identical to the backups made before installation.
- Verification restored the previous active app; it did not change persistent
  settings. Captures: device-private/approved-layouts-live.json.
- Clock/calendar modes 0–4 use native layouts when the text fits. Big/binary modes
  and date formats wider than 52 physical pixels retain the previous fitted path.
  The physical GPIO/SPI driver, web UI, scripts and MQTT contract are unchanged.

## 2026-09-15: separate minus/plus button controls (1.1.0-tc002.4)

The separate minus/plus buttons previously shared app navigation with the knob.
Rotary detents now travel separately from held button states; knob navigation and
select behavior remain available. The minus/plus buttons change all three speaker
volume settings by five percentage points on short release. After a 700 ms hold,
they change brightness by ten every 200 ms, clamped to 1–255. Long releases do not
also adjust volume. Volume values clamp to 0–100. Changes use the normal settings
dispatcher, persistence and state publication. Physical minus/plus direction is
independent of panel rotation and navigation swapping.

Validation: 112 pytest cases and the hardware CTest passed; the ARM build, image
validator and physical updater preflight passed. Nine new application-level cases
cover both button directions, release timing, hold repetition, value limits, knob
navigation, navigation locking and saved settings. The display driver and approved
clock/date/battery layouts are unchanged. Physical press/hold confirmation remains
an owner check.

Installed update SHA256
`f744b32c7aadbfb52ccd6f46e8dc2095fec2b774191ef4ca3cf16437b2f5db2b`.
The updater verified every flash block and rebooted Linux. The installed API
reports 1.1.0-tc002.4 at 42 FPS; both persistent configuration files are identical
to their pre-update backups.

## 2026-09-15: Audio page and local MQTT DNS (1.1.0-tc002.5)

Reproduced `GET /api/v1/audio/mp3` returning 404 on the installed clock. Added the
native list, multipart upload and named deletion routes used by the upstream
Audio page, with filename/content validation and storage-write error reporting.
The file handler serves these routes without duplicating uploaded MP3 buffers.

The vendor DHCP properties contained the correct LAN DNS server, but libc read
the read-only `/etc/resolv.conf`, which listed public DNS servers. AWTRIX now
bind-mounts a RAM resolver file in a private mount namespace before starting
hardware or networking threads. It uses DHCP DNS properties or configured static
DNS servers and refreshes when the network properties change. Other processes
and the root filesystem retain their original resolver. The update helper joins
init's mount namespace before preflight/installation so `/res` still unmounts in
the system namespace before any flash erase.

Validation: the full 122-case pytest suite and both CTests passed. After the final
file-handler refinement, all 51 platform/image tests passed again. Host and ARM
builds passed. A physical RAM trial loaded the complete web UI, including Audio,
without JavaScript errors; MP3 listing/upload/deletion passed without playback.
The patched application connected to `mqttserver.lan` at `192.168.100.5:1883`.
The trial restored the installed service and confirmed both persistent
configuration files were unchanged. The DNS probe also verified the original
resolver was preserved outside its namespace and restored on cleanup.

Full reboot testing also exposed the vendor launcher's inherited property
mapping being closed by the port's descriptor cleanup. The loader now preserves
the read-only descriptor named by `ANDROID_PROPERTY_WORKSPACE` across exec while
closing hardware descriptors. A new exec regression passed on both the host and
the clock; all three CTests passed. A normal init-service startup with the
corrected loader then connected through the broker hostname successfully.

Installed update SHA256:
`01fe9ac61df09700e25038f08ef6f43be2e65a565bc8813cbc83be84e4d87a8a`.
The update and stock recovery passed host validation. Physical updater preflight
passed from both the system and private mount namespaces without writing flash.
A fresh backup of the installed res partition produced
`dist/rollback-tc002.4.img`, matching the previously installed `.4` image checksum
above. The updater wrote and read-back verified the corrected `.5` image, and a
changed Linux boot ID confirmed a full reboot. The installed web UI, including
Audio, passed the browser check. MQTT is saved as `mqttserver.lan` and connected
to `192.168.100.5:1883`, including after a configuration-triggered process restart.
Device and display settings were preserved across installation; the only
intentional subsequent configuration change was the broker hostname.

## 2026-09-15: TC002 application title and repository link

The browser title and application heading now read `AWTRIX NG TC002`. The support
button is replaced by an accessible GitHub link to this repository, opening in a
new tab. The obsolete support icon, styling and JavaScript handler were removed.
Browser checks passed at 1360, 390 and 320 pixels wide without JavaScript errors.

Installed branding update SHA256:
`6ab1eac33182e63dbf87dd802c70df50e31cfd92bd6917c36a98be378e71101b`.
Application, loader, updater and vendor bootstrap binaries match the preceding
installed image. The prior image is saved as
`dist/rollback-tc002.5-before-branding.img`. Device validation passed, but the ADB
installation command lost connectivity without reporting completion. The owner
reported the clock display running with flashing connection indicators and no
web access. After the owner power-cycled the clock, it reconnected. The installed
`/res/ui/awtrix.html` matched the source byte-for-byte. The full UI browser check
and desktop/mobile branding checks passed against the clock. All saved device
and display settings matched the pre-update backup; MQTT connected through
`mqttserver.lan`, and the application reported 42 FPS.

## 2026-09-15: volume button display feedback (1.1.0-tc002.6)

Minus/plus taps changed the volume without rendering any feedback. Each tap now
shows a speaker icon and percentage for 1.5 seconds, with a mute cross at zero.
Further taps refresh the timer. The displayed value follows active MP3/radio
playback, falling back to tone volume when idle. The underlying app continues
running and becomes visible again when the feedback expires. Powered-off panels
stay dark, and brightness holds do not trigger volume feedback.

Validation: 54 host control/native-layout/scaling tests and all three CTests
passed. The ARM build and host/device firmware validation passed. Comparing the
new filesystem with the live backup found only `bin/awtrix-tc002` changed.

Installed update SHA256:
`7c310023ddf5215aa8568dc0260576cee6ae6fd4ac49ab9aea5719a62a7c63f3`.
The updater reported a verified flash write and rebooted Linux. The changed boot
ID, running launcher service and installed API confirm automatic startup into
1.1.0-tc002.6. The installed application is byte-identical to the built binary;
all eight saved configuration/asset files match the pre-install backup.

Backup, rollback image and installation evidence are saved locally under
`device-private/pre-volume-feedback-20260915/`. Update artifacts are in
`dist/volume-feedback/`. Physical button/panel appearance remains an owner check.

## 2026-09-15: volume feedback shifted right (1.1.0-tc002.7)

At the owner's request, moved the speaker/mute icon and percentage four physical
pixels to the right. At 100%, reduced the gap before the percent sign by one
pixel so its final column remains visible on the 52-column panel.

Verified every level from 0 through 100 against the previous renderer: the shift
preserves every lit pixel, with only the documented 100% spacing adjustment.
All 31 control/native-layout tests passed; host and ARM builds passed.

Installed update SHA256:
`4a9507875d9885ce65bbd6c44f37c79f0353b16e8b2b21933989eb24a208bf78`.
Host validation and device preflight passed. The updater verified the flash write,
then the clock rebooted and started version 1.1.0-tc002.7 automatically.
Only the application binary changed in the firmware filesystem.
Backups and installation evidence are in
`device-private/pre-volume-shift-20260915/`; artifacts are in `dist/volume-shift/`.

## 2026-09-16: upstream credit restored (1.1.0-tc002.8)

Upstream review feedback pointed out that the web UI's Ko-fi button had been
replaced by a link to this repository while the documentation button and the icon
editor still load from the upstream author's hosting. That was wrong. The Ko-fi
button, icon and click handler are restored verbatim from upstream, ahead of the
repository link, and a footer carries the PolyForm Required Notice, the licence
and a pointer to support upstream. `tests/test_webui_static.py` fails if either
disappears again. No firmware behaviour changed; the version is bumped so the
shipped UI is distinguishable from tc002.7.

## 2026-09-16: response to the upstream review (1.1.0-tc002.9 candidate)

The upstream author reviewed the port and declined to adopt it: a fork rather
than a port, the simulator's web server on a networked device with 8 MiB
uploads held in RAM, hard coupling to one vendor firmware with no version
check, an in-place flash with untested cold boot and no recovery without ADB,
and his Ko-fi button replaced by a link to this repository. Every point was
checked against the pristine upstream commit and confirmed, except that the
bootloader, kernel and rootfs were never touched and the public trial ZIP never
contained vendor files or a flasher.

Changes in this candidate, all verified by the host suite (138 pytest cases,
golden screens unchanged) and not yet on a clock:

- Ko-fi button and Required Notice restored (shipped as tc002.8).
- Upstream is a pinned submodule; TC002 changes are eight patches (four hooks
  with upstream defaults, four guarded); platform code moved to `src/tc002`.
  Two ESP32 memory regressions in the old in-place edits are gone.
- Request bodies capped per route before reading; MP3 and firmware uploads
  stream to storage; one large upload at a time; timeouts bounded.
- Launcher falls back to the vendor application on a knob hold at power-on or
  after three failed starts; input nodes found by capability.
- Vendor libraries only used when their SHA-256 is recorded; the updater
  refuses unknown stock firmware without `--force`; status at
  `/api/v1/tc002/vendor`.
- `tools/image.py` takes the version from CMakeLists.txt and only marks a build
  validated through `--validated`, per docs/VALIDATION.md.

## 2026-09-16: device facts and vendor fingerprints (1.1.1-tc002.1 candidate)

Read from the clock over ADB, without changing anything on it:

- Flash: `mtd3 res` 8 MiB (squashfs, AWTRIX), `mtd6 data` 8 MiB jffs2 mounted at
  `/data` with about 7.6 MiB free, `mtd2 rootfs` 4.3 MiB squashfs, `mtd4 config`,
  `mtd7 UDISK` (vfat). `/tmp` is a 16 MiB tmpfs. MemAvailable about 15 MiB.
- The vendor libraries the port calls are on the rootfs: `/lib/libmi_ao.so` and
  `/lib/libzknet.so`; the vendor application is `/res/lib/libulanzi-bootstrap.so`.
  Their SHA-256 values are now in `src/tc002/vendor-fingerprints.json`.
- Input nodes: `/dev/input/event67` is `soc:gpio_keys_1`, `/dev/input/event68` is
  `knob_key`. The capability scan in `src/tc002/InputDevices.h` tries these first.
- Installed: 1.1.0-tc002.7, MCU V1.0.17, DHCP DNS, MQTT and NTP working.
- The clock's BusyBox has no `find`, `grep` or `head`; device tools use `ls` and
  parse on the host.

The firmware staging check now asks for room for the actual upload (Content-Length
plus 1 MiB) instead of a fixed 9 MiB, which this 8 MiB data partition could never
provide, and reports a full partition as 507 instead of an invalid image.

## 2026-09-16: RAM trial of 1.1.1-tc002.1 on the clock

`tools/trial.py` ran the new ARM build from RAM for 75 seconds on the test clock
(stock app 1.1.1, MCU V1.0.17), then restored the installed 1.1.0-tc002.7:

- `/api/v1/version` reported 1.1.1-tc002.1; 42 FPS; about 12 MiB free RAM.
- `/api/v1/tc002/vendor`: `libmi_ao.so` checked and trusted; the buzzer
  capability was on and a test tone played. `libzknet.so` was not exercised
  because the trial inherits the launcher's DHCP lease, and the vendor
  application is only used by the launcher, which a RAM trial bypasses.
- Wi-Fi connected, matrix reported as 52 × 16 fixed.
- A POST with a 9 MB Content-Length was refused with 413 before any body was read.

Still not exercised on hardware: installation from the web UI with staging on
`/data`, the knob-hold and three-strikes fallbacks, cold boot, restore. These
are the docs/VALIDATION.md steps that remain before tagging.

## 2026-09-16: 1.1.1-tc002.1 installed; cold boot strands the clock in AP mode (1.1.1-tc002.2)

Installed 1.1.1-tc002.1 through the web UI (after first flashing a stale
tc002.5 `dist/update.img` by mistake, which the same path also installed and
booted). Preflight with the new stock-firmware check passed. Linux reboot over
ADB: web UI back in about 30 s, `boot-attempts` read 1 during the first minute
and was gone after it, so both halves of the three-strikes counter work.

Cold boot (unplugged 60 s): the top-left Wi-Fi pixel kept pulsing and the
`awtrixng-77a8ff` access point appeared. The radio needs longer than 15 s to
associate after a cold start, the port then opened its access point and never
retried the saved network. A warm power cycle associated within seconds. Fix:
the first association gets at least 90 s, the launcher waits up to 45 s for
the lease, and from access-point mode the saved network is retried every two
minutes. The updater's log moves to `/data/awtrix-ng/update.log` because
`/tmp` does not survive the reboot, and the app reports the vendor
application's fingerprint at start. The web UI's Reboot only restarts the
AWTRIX process; the validation protocol now says `adb shell reboot`.

## 2026-09-16: on-clock validation of 1.1.1-tc002.2, boot counter lost on power cuts (1.1.1-tc002.3)

- Cold boot twice: associated before the app's first log line both times; no access point.
- Knob held at power-on: the stock Ulanzi UI started, `/tmp/awtrix-loader.log` read
  "knob held at power-on: starting the vendor application", ADB worked under stock, and
  a normal power cycle started AWTRIX again. First hardware run of the fallback path.
- Three power cuts within 20 s of the display lighting up, then a normal boot: AWTRIX
  started and `boot-attempts` read 1. The launcher's counter writes were never committed
  to jffs2 before the power cuts. Fix: the launcher now fsyncs the counter file and its
  directory. Retest of this step is pending on tc002.3.

## 2026-09-16: three-strikes counter was cleared by the app, not lost by the flash (1.1.1-tc002.4)

A synced marker file on `/data` survived a power cut, and the launcher's counter
writes were confirmed on clean boots, so the flash was not losing anything. The
app cleared the counter as soon as the monotonic clock passed 60 s, and that
clock counts from Linux boot: after a cold start the launcher can wait up to
45 s for Wi-Fi first, so the app often started past the mark and deleted the
counter on its first frame, before the tester's power cut. That explanation
turned out wrong: the simulated timer counts from the app's first call, so the
mark was already app-relative. The cause is still open; tc002.4 records every
start in a synced `launcher.log` on the data partition and adds a full sync
after the counter write, so the next run shows what each boot saw. The launcher also records the
start before loading the vendor library and keeps a synced `launcher.log` on
the data partition. Retest of the three-strikes step is pending on tc002.4.

## 2026-09-16: three-strikes fallback proven for crash loops; power cuts do not count (1.1.1-tc002.4)

tc002.4 records every launcher start in a synced `/data/awtrix-ng/launcher.log`.
Three well-timed power cuts (five seconds after the AWTRIX animation) left no
entry at all, while a marker written a minute into a boot survived a cut, so
this jffs2 volume does not keep writes made in the first seconds after boot
across a power cut, and the counter cannot count pulled plugs. Documented as
such; the knob-hold remains the recovery for power-cut loops.

Crash-loop test over ADB, which is the case the counter exists for: the app was
killed three times within a minute of each restart; the launcher logged
attempts 1, 2 and 3, the counter read 3, and on the fourth start it logged
"AWTRIX did not start healthily three times: starting the vendor application",
cleared the counter and started the stock UI.

Validation status for 1.1.1-tc002.4: preflight, web-UI install, warm restart
over ADB, two cold boots, knob-hold fallback and crash-loop fallback all pass
on stock 1.1.1 / MCU V1.0.17. Not run: restore-stock through the web UI and
the reinstall after it (steps 8 and 9 of docs/VALIDATION.md).

## 2026-09-16: one-line installer (1.1.1-tc002.5)

`install.sh` downloads a released installer bundle, verifies its checksum and runs
`install.py`, which needs only python3, adb (downloaded on demand) and squashfs-tools:
it reads the clock's application partition over ADB, verifies the vendor fingerprints,
builds `update.img` and `restore-stock.img` on the computer with a synthesized
container header (no stock update file needed), runs the helper's preflight, asks the
user to type `flash`, installs and waits for the new version. `--restore` flashes the
recovery image back. `tools/image.py` became a library and now rebuilds the vendor
layout from a partition that already carries the port. The bundle is validated by the
same allowlist checker as the trial ZIP and contains no vendor file. Exercised in
build-only mode against the test clock: dump, three fingerprints verified, both images
valid; the firmware itself is unchanged from tc002.4.

## 2026-09-20: upstream 1.1.2 (1.1.2-tc002.1 candidate)

Moved the submodule to v1.1.2 (`6d6cc64`) with `tools/upstream.py rebase`. Seven of the eight
patches touched files upstream changed:

- 0001 lost its GIF half: upstream removed `MicroGif::kMaxW/kMaxH` and bounds GIFs by the panel
  at run time, so `AWTRIX_GIF_MAX_W/H` are gone from the patch and from `CMakeLists.txt`.
- 0002 keeps the decoded JPEG size next to upstream's new out-of-memory flag:
  `icon::draw(canvas, id, x, y, bool* outOfMemory, int* width, int* height)`.
- 0005 now sits on upstream's `iconColumn()`/`iconGap`: the gap is `iconGap` times the text scale
  (2 on the 16-row panel), `DevicePageIcon` keeps the source picture in upstream's dynamic buffer
  and scales classic tiles at blit time, and the main icon is centred through a new
  `IPageIcon::height()`. Placed icons (`icons`) use the same class, so classic 8-row art doubles
  there too while `x`/`y` stay physical pixels.
- 0006 shrank to "script JPG icons up to the panel size"; upstream's `ScriptIconSet` already sizes
  GIFs to the panel, so `AWTRIX_SCRIPT_ICON_W/H` are gone as well.
- 0008 keeps the update row behind `stats.updateImage`. Upstream's new "Download & install"
  fetches ESP32 images from its own feed; the port reports an empty `updateImage`, so the row is
  absent and the version moves to the firmware file row. The upload now requires `{"ok":true}`,
  which `/update` already returns.

Port side: `main_tc002.cpp` reconciled with `main_sim.cpp` (panel size for script icons,
press/release button hook, page icon invalidation); `Tc002Periphery` passes the select button's
held state every tick and reports a knob detent as a press followed by its release;
`Tc002ScriptHttp` serves `modbus://` reads like the simulator.

Behaviour change taken from upstream: the progress bar starts at the icon's edge and runs under
the gap. `tests/test_scaling.py` and the `icon-text-progress` golden (6 pixels) follow it; no
other golden pixel changed.

Host: 143 pytest cases and 3 CTest cases pass, three of them new: the scaled `iconGap`, placed
icons at physical coordinates, and a knob detent reaching `on_button_event()` as a consumed press
plus its release. The ARM cross build links. In a headless browser the System page shows no
update row, the version and an `.img` file input; matrix badge, icon limits and editor sizes
follow the 52 by 16 capability. `tools/check_webui.py` does not pass on a host build, and did not
before this bump: its Icons text assertion matches nothing in upstream 1.1.1 or 1.1.2, and the
MP3 uploader is hidden while the host reports no audio.

Not yet on a clock: everything above, in particular `on_button_event()` from the knob and the
select button, `timer.every()`, Modbus reads, `icons`/`iconGap` pages and JPG icons larger than
8x8.

## 2026-09-20: 1.1.2-tc002.1 on the clock

Built from `4167223` (clean tree). RAM trial first (`tools/trial.py`, 40 s, port 18081): the binary
started on stock 1.1.1 / MCU V1.0.17, reported the fixed 52 by 16 matrix, audio and an empty
`updateImage`; a notification with `icon`, `iconGap: 3` and one placed icon rendered as on the host
(text from x=22, placed icon at x=46) and a `timer.every()` script loaded. The installed
1.1.1-tc002.4 came back afterwards.

Installed with the packaged installer bundle over ADB: three vendor fingerprints verified, preflight
passed, `update.img` 4428348 bytes. The clock answered with 1.1.2-tc002.1 again about 140 s after
the write began, at 42 FPS, with its apps (Time, Battery, temperature, Date) and settings intact.
The recovery image is under `~/.awtrix-ng-tc002/192.168.100.190/20260920-222648/`.

Two installer defects met on the way, neither of which wrote anything wrong:
- `tools/install.py` run from the repository fails in `fingerprints()`: it reads
  `vendor-fingerprints.json` next to itself, which exists only in the bundle (`bundle_paths()`
  handles both layouts, this does not). Worked around by packaging the bundle and running from it.
- `flash()` runs `--install` through `adb shell` with a 120 s timeout and does not catch
  `TimeoutExpired`; adb hung when the clock rebooted under it, so the installer died with a
  traceback after the write instead of going on to `wait_for()`.

Not run on this build: docs/VALIDATION.md (cold boots, knob-hold fallback, three-strikes fallback,
restore-stock), `on_button_event()` from the physical knob and select button, Modbus reads, JPG
icons larger than 8x8.

## 2026-09-24: knob turns over MQTT (issue #4)

Issue #4: knob turns reached neither MQTT nor Home Assistant. With block buttons on, a turn left no
trace at all. A detent does not fit upstream's `state/buttons/*`. Those topics carry retained
levels, `buttonsDue()` merges a press and release that happen in the same tick, and on this clock
left and right are the −/+ rocker. So each detent now goes out on a new topic,
`<prefix>/state/knob`, as a non-retained `cw` or `ccw`. That follows the `state/screen` precedent
for non-retained messages and plain strings like `state/apps/active`.

The publish happens in `Tc002Periphery` through a new rotation hook. It runs before the script
hook and the block check, the same way `state/buttons/*` ignores both. `cw` means "next app", so
the rotate and swap settings apply to it as they do to navigation.

Patch 0009 adds `IBoard::hasEncoder()`, which defaults to false. When it is true, discovery adds
a Home Assistant `event` entity. That entity's `val_tpl` turns the plain payload into the
`{"event_type": ...}` JSON that Home Assistant wants. Upstream boards announce the same document
as before. `test_hadiscovery` (run with `uv run --with platformio pio test -e native -f
test_hadiscovery` in `build-upstream/`) and the integration suite cover the topic, the missing
retain flag, blocked navigation and the entity.

Also: `build-upstream/` held a stray local commit that only added `.tc002-stamp`. `export` would
have turned it into a patch, so the tree was re-applied with `--force` before this work.

Not yet on a clock: which physical direction is `cw` (docs/VALIDATION.md step 8), and whether
Home Assistant shows the entity.

## 2026-09-25: knob events and coffee link on the clock (development install)

Built from `b1bd3fb` plus the uncommitted issue #4 knob work and the port's Buy me a coffee link
(still versioned 1.1.2-tc002.1, so the build was identified by the served UI's `coffee-link`). RAM
trial (`tools/trial.py`, 120 s, port 18081): version and fixed 52 by 16 matrix reported, the
branded UI served, `/sim/rotary/right` answered 404 under `--hardware` as intended, no crash in
the host log, and the installed build came back afterwards. Only the UI's CSS changed after the
trial (the button's `#ea9e64` background); the binary did not.

Installed with the skill's wrapper: three vendor fingerprints verified, preflight passed,
`update.img` 4428348 bytes, clock back with 42 FPS, apps (Time, temperature, Date, Battery) and
settings intact, `updateImage` empty, MQTT connected, HA discovery on. Run directory:
`~/.awtrix-ng-tc002/192.168.100.190/20260925-002217/`.

Not yet checked: the broker needs credentials this session did not have, so neither the `knob`
entry in the retained discovery document nor a `cw` / `ccw` on `ulanzi-tc002/state/knob` from the
physical knob has been observed yet. Which physical direction publishes `cw` is still open.

## 2026-09-25: EMQX outage after the knob install, A/B capture

After the development install the owner's broker (EMQX Enterprise 6.3.1 at mqttserver.lan) went
away once: the clock logged `connection lost (state -3)` at 00:30:26, `refused` on the retry (the
broker was not listening), and a normal reconnect 68 s later. The owner saw it "freeze by itself"
with the knob untouched and never with the previous build, then switched MQTT off on the clock.
There is no broker log.

A/B test without touching the owner's broker or the installed config: RAM trials
(`tools/trial.py`, new `--config KEY=JSON` option) with MQTT pointed at a local
`emqx/emqx-enterprise:6.3.1` in Docker, behind a capture proxy that decodes and checks every MQTT
packet. 600 s each, knob idle:

| | new build | old build `b1bd3fb` |
|---|---|---|
| connections / reconnects | 1 / 0 | 1 / 0 |
| publishes | 144 | 144 (same per-topic counts) |
| `state/knob` while idle | 0 | - |
| discovery document | 7456 B (with `knob`) | 7267 B |
| framing errors | 0 | 0 |
| keepalive | PINGREQ every 15 s, all answered | same |
| EMQX restarts / log lines | 0 / 0 | 0 / 0 |

The firmware's traffic is the same apart from the extra ~190-byte discovery entry, and the same
EMQX version handled both builds without trouble. No firmware change made.

Setup lessons: this clock's adbd has no `adb reverse` and refuses a second concurrent `adb shell`
("error: closed"); killing the local adb client mid-session left adbd refusing every shell until a
power cycle. The clock reaches a broker on the WSL host over Wi-Fi only after a Windows inbound
firewall rule for the port.

Knob run (new build, 120 s, owner turning the knob): 49 `state/knob` publishes in about 30 s of
turning, all non-retained, 26-27 bytes each; a fast spin peaked at about 20 per second, each
followed by the matching `state/apps/active`. `cw` always moved to the next app and `ccw` to the
previous one. No framing errors, one connection throughout, EMQX 6.3.1 quiet (0 restarts, 0 log
lines). The first events of the owner's "clockwise first" sequence were `cw`.

Direction confirmed by the owner on the clock (installed build, live against their EMQX and Home
Assistant, 2026-09-25): turning the knob right publishes `cw`, left publishes `ccw`. MQTT and HA
discovery were switched back on at 16:28:12 (they had been off since the owner turned them off at
00:37 after the outage).

The knob events and the coffee link ship as **1.1.2-tc002.2**: `v1.1.2-tc002.1` was already
tagged and published on 2026-09-22, and the development install above still reported that
number, which made the two builds indistinguishable on the clock.

## 2026-09-25: vendor application accepted by its entry points (issues #8, #6; 1.1.2-tc002.3)

Three clocks on other stock versions could not install:

- #8: app 1.1.3 / MCU V1.0.17. The installer refused `/res/lib/libzkgui.so`, and
  `--allow-unverified` could not finish because `install.py` never passed `--force` to the
  helper.
- #6: SoC 1.0.1 / MCU V1.0.16 (`libzkgui.so` `4b8783e1…`) and SoC 1.0.8 / MCU V1.0.17
  (`de15dd84…`). On both, `libmi_ao.so` and `libzknet.so` match the recorded hashes. Both
  reporters installed by running the helper with `--force` by hand: AWTRIX boots, Wi-Fi
  connects, 42 FPS, audio library trusted. `nm -D` shows all four launcher symbols in both
  builds.

Ulanzi's own repository (UlanziTechnology/Ulanzi-U-Clock-TC002) shows what those four symbols
are:

- `onEasyUIInit`, `onEasyUIDeinit` and `onStartupApp` are the FlyThings app entry points
  (`Z21_TC002_Demo/src/Main.cpp`).
- `base::wifiOnAndWait(int)` is documented SDK API (`<base/wifi.h>`, base-utility package).

The same README documents a factory restore: hold the reset button beside the USB-C port while
powering on. It has not been tried on the test clock.

The vendor application is now accepted when it carries a recorded hash (**verified**), or when its
ELF `.dynsym` defines all four as GLOBAL or WEAK functions (**compatible**). The file is read,
never loaded:

- The update helper and `/api/v1/tc002/vendor` use `src/tc002/ElfSymbols.cpp`.
- `install.py` has a stdlib reader with the same rules.

`libmi_ao.so` and `libzknet.so` still need a recorded hash, because the port passes
hand-measured structures to them; `generate` refuses `symbols` on any other library. The 1.0.1
and 1.0.8 hashes are not recorded: they pass as compatible, and `verified` stays reserved for
builds run through docs/VALIDATION.md here.

Other changes:

- `--allow-unverified` passes `--force` to both helper runs, but only when a library was actually
  unknown. A vendor application without the entry points is refused even with the flag.
- `--restore` always passes `--force`: the image is the clock's own stock partition. It was
  refused on every force-installed clock.
- The installer prints full SHA-256 values and, per file, what would stay off.
- The preflight output now includes the helper's stderr (`2>&1`).
- The install step no longer dies on a hanging adb connection.
- The app reports the vendor application at `vendorApplicationPath()`. RAM trials on a stock clock
  used to report the post-install path as untrusted.

Tests:

- pytest: 206 passed. CTest: 5 passed. The new ELF reader test runs under ASan and UBSan, with
  every truncation and 3000 corrupted copies of a fixture library.
- The Python and C++ readers agree on the ARM launcher `dist/bin/libzkgui.so` (stripped as
  released: three entry points, no `wifiOnAndWait`) and on about 500 damaged files.
- The ARM cross build (GCC 9.2) is clean, and the installer bundle passes
  `check_trial_package.py`.
- Against a fake `adb` serving a synthetic partition, `--build-only`, `--yes` and `--restore` ran
  end to end. The helper got `IMG --force` exactly for `--allow-unverified` over an unknown audio
  library and for `--restore`.

Not run on a clock. On the 1.1.1 test clock:

- `--build-only` should print "defines the launcher's entry points" on the vendor line (the Python
  reader on the real vendor ELF32).
- The preflight should print "verified; defines the launcher's 4 entry points" (the C++ reader on
  ARM).
- `/api/v1/tc002/vendor` should show `missingSymbols: []`.

The compatible path on real 1.0.1, 1.0.8 and 1.1.3 clocks waits for the reporters.

## 2026-09-25: script heap budget sized for the clock (issue #9, 1.1.2-tc002.3)

On the TC002 the Berry VM uses the host allocator (`ScriptHeapNative.cpp`, plain malloc), and its
budget was upstream's ESP32 internal-RAM value, 96 KB. The clock reports 12 to 15 MB available
while AWTRIX runs, yet a sixth average script was refused with `insufficientStorage` (#9). The
reporter ran a 1 MiB budget on a SoC 1.0.1 clock: six scripts together, 42 FPS, free memory down
by about 330 KB.

Patch 0010 makes the budget a build-time setting, `AWTRIX_SCRIPT_HEAP_BUDGET_BYTES`. The default
stays 96 KB, so upstream, the simulator and ESP32 builds are unchanged. `CMakeLists.txt` sets
1 MiB for the clock.

The budget only refuses new installs once the shared heap is past it; running scripts can still
grow. It is not derived from free memory like upstream's PSRAM path (half the free pool), which
would be about 6 MB here. The adb installer stages about 4.5 MB in the RAM-backed `/tmp`, and the
clock has no swap, so a fixed, modest share is safer.

Measured on the host with upstream's `fatApp()` test script (about 9 KB of heap each): at 96 KB
the tenth install was refused ("shared Berry heap 101153 bytes is over the 98304 byte internal
budget"); at 1 MiB, 112 install before the budget refuses. `test_scripts_share_a_one_mebibyte_heap`
installs until refused and checks both the count and the message. The golden device state now
records `scriptHeapBudgetBytes` 1048576.

Not run on a clock. `growthBudget()` on this build is still unbounded (upstream's host
behaviour), so buffers scripts request (HTTP bodies, shared state) have no memory-based ceiling
on the clock; that is a separate change.

## 2026-09-26: 1.1.2-tc002.3 on a reporter's clock; trial ZIP ships `paths.py`

The #6 and #9 reporter ran the `v1.1.2-tc002.3-pr10` build (70ecf12) on a stock SoC 1.0.1,
MCU V1.0.16 clock. RAM trial, 180 s: `scriptHeapBudgetBytes` 1048576, seven scripts installed
together, 42 FPS on each, the installed firmware back at the end. Install with the one-line
installer over their own 1.1.2-tc002.2 build, without `--allow-unverified`:
`libulanzi-bootstrap.so` **compatible** (all four entry points), `libmi_ao.so` and `libzknet.so`
**verified**, helper preflight passed, 1.1.2-tc002.3 answering about a minute later. After the
reboot: 42 FPS, Wi-Fi, audio capabilities on, and `/data` intact (8 scripts with their config,
uploaded icons, settings).

The trial ZIP never contained `tools/paths.py`, which `try.py` imports for the adb lookup, so
every trial ZIP since 1.1.1-tc002.1 needed it copied in by hand. `check_trial_package.py` ran
`try.py --help`, which exits in argparse before that import. The import now sits with the other
imports, `package_trial.py` ships `paths.py`, and the checker requires it: a ZIP without it fails
`try.py --help`.

Two more from #6. `GET /api/v1/tc002/vendor` called the recorded reference build `stock`, which on
a 1.0.1 / V1.0.16 clock read as a wrong detection; the key is now `reference`. The clock's own
versions are not reported there: the MCU version stays inside `Tc002Hardware` and the stock app
version is not read anywhere. And `tools/install.py` now falls back to
`src/tc002/vendor-fingerprints.json` when no bundled copy sits next to it, like `bundle_paths()`
does for the binaries, so it runs from a checkout; `test_vendor_gate.py` no longer patches that
lookup out.
