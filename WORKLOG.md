# TC002 port validation

Latest validated candidate: **1.1.0-tc002.4**, with 112 passing pytest cases and
one passing hardware CTest. The sections below record successive validation
rounds; earlier version numbers and test counts describe those earlier rounds.
Paths under `device-private/` and `dist/` refer to local evidence and artifacts
that are not included in the public repository.

Upstream: AWTRIX NG 1.1.1, commit `73b4582e157484a737397bb0fa616da62212fe9e` (earlier rounds: 1.1.0, `4ff1de83428ed13cae6e210dfcbf9d186a09bc60`).
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
