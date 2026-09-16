# AWTRIX NG TC002

An unofficial, community-maintained native Linux/ARMv7 port of **AWTRIX NG 1.1.1** for
the Ulanzi TC002's **52 × 16** pixel matrix. It runs the upstream core, renderer, Berry
scripting, HTTP API, MQTT dispatcher and web UI directly on the clock.

**Read [Risks](#risks) before installing anything.** Installing replaces the clock's
application partition; the RAM trial does not.

Upstream is tracked as a pinned, unmodified git submodule plus a short patch series;
everything TC002-specific lives in this repository. See [docs/PORTING.md](docs/PORTING.md).

## Thank you, Blueforcer ❤️

This port exists thanks to **[Blueforcer (Stephan Mühl)](https://github.com/Blueforcer)**,
the author of **[AWTRIX NG](https://blueforcer.github.io/awtrix-ng/)**.
The firmware core, MQTT interface, scripting engine and web UI build on his work
and the contributions of the AWTRIX community. Huge thanks for making AWTRIX NG
available to study, adapt and enjoy on more hardware!

Explore the [original project](https://github.com/Blueforcer/awtrix-ng), read the
[AWTRIX NG documentation](https://blueforcer.github.io/awtrix-ng/), and give
Blueforcer a star if you enjoy this port. This is an independent, unofficial TC002
adaptation, maintained here; it is not an official Blueforcer or Ulanzi release.

The web UI keeps the upstream **Ko-fi** button, and its documentation button and icon
editor load from Blueforcer's hosting. If this port is useful to you, please
[support the upstream author](https://ko-fi.com/blueforcer): the work you are using
is his. AWTRIX NG is licensed under the
[PolyForm Noncommercial License 1.0.0](https://polyformproject.org/licenses/noncommercial/1.0.0);
this port carries its Required Notice and the same licence.

![TC002](docs/tc002.jpg)

## MQTT compatibility

The MQTT interface uses the exact upstream topic paths and payload handling:
[AWTRIX NG MQTT reference](https://blueforcer.github.io/awtrix-ng/reference/mqtt/).
No TC002 topic prefix or translation service is added. The default prefix is the
clock's 12-character MAC; it can be changed in System → MQTT.

## Supported hardware and firmware

| | Supported | Enforced by |
|---|---|---|
| Clock | Ulanzi TC002 (SSD202D, 52 × 16 panel) | flash geometry check in the updater |
| Stock app | **1.1.1** | SHA-256 of the vendor files in `src/tc002/vendor-fingerprints.json` |
| MCU firmware | **V1.0.17** | version handshake at start-up (logged, not gated) |

The firmware only calls into the vendor audio and network libraries when the file it loaded
matches a recorded hash, and the updater refuses to install on an unrecognised stock firmware
unless told `--force`. `GET /api/v1/tc002/vendor` shows what was checked. The recorded hashes were captured from
the test clock on 2026-09-16 with `uv run tools/vendor_fingerprints.py capture CLOCK_IP`; a
clock with different files shows them as untrusted and keeps audio and vendor Wi-Fi
provisioning off.

## Risks

- **Installing writes the `res` flash partition in place.** There is no A/B copy on the
  clock. The helper validates the image, checks the partition geometry, refuses unknown stock
  firmware, and verifies every erase block by read-back, but a power cut during the write
  leaves a partition that needs the recovery steps below.
- **Cold boot is not validated for every release.** A release is a candidate until the
  protocol in [docs/VALIDATION.md](docs/VALIDATION.md) has been run on a real clock; the
  manifest says so.
- **Recovery without a computer** is the knob-hold at power-on, or three start-up crashes in
  a row, both of which start the vendor application instead of AWTRIX. Both were exercised
  on the test clock on 2026-09-16. Recovery over a serial console has never been tried.
- **Vendor coupling.** Wi-Fi provisioning, audio and the launcher use Ulanzi's closed
  libraries. A future Ulanzi update can change them; the fingerprint gate then turns those
  features off instead of guessing.

## Security

The web UI and API are unauthenticated unless you enable the login in System → Web. Without
it anyone on your network can control the clock, upload files and start a firmware update.
Request bodies are capped per route before they are read; MP3 and firmware uploads stream to
storage and are limited to one at a time.

## Current status

The current candidate is **1.1.1-tc002.5** (upstream 1.1.1). It restructures the source tree, hardens the
HTTP server, adds the launcher fallback and the vendor fingerprint gate, and has not yet been
installed on a clock; the host test suite and golden screens pass. The previous candidate,
1.1.0-tc002.7, was installed and verified after a Linux reboot on the test clock on
2026-09-15. AWTRIX starts automatically from flash, serves the web UI on port 80 and stores
settings in `/data/awtrix-ng`. See [WORKLOG.md](WORKLOG.md) for measured results and
outstanding physical checks.

Confirmed on TC002 stock app 1.1.1 / MCU V1.0.17 with 1.1.0-tc002.7:

- Correct 52 × 16 RGB output using the full app; about 42 FPS. See the display
  flicker note below for a known issue at some green levels.
- Speaker playback of two sets of three rising notes, confirmed by the owner.
- MP3 file playback and local HTTP MP3 streaming, checked at zero volume.
- Battery percentage from the MCU and live Wi-Fi status/scanning.
- Left/right buttons, rotary navigation, mDNS discovery and NTP synchronization.
- Certificate-verified HTTPS with the bundled OpenSSL 3.5.8 implementation.
- Upstream dashboard with a 520 × 160 preview, checked in a real browser.
- All 21 MQTT/platform integration checks, including replies, retained state,
  Home Assistant discovery and silent rejection of unknown topics.
- Temporary vendor launcher replacement starts AWTRIX successfully and restores
  stock afterward.

Implemented platform services include Wi-Fi configuration, DHCP/static addressing,
NTP/timezone, mDNS announcements, Art-Net, buttons/rotary input, speaker RTTTL/MP3,
HTTP(S) MP3 radio, playlists, configuration persistence and a res-only update helper.
Services marked as unverified in WORKLOG.md need a physical check before relying
on them unattended.

Hardware differences: the TC002 has fixed wiring and no light, temperature or
humidity sensor; those GPIO controls are hidden. Brightness remains adjustable.
Sleep blanks the panel and pauses services for the requested duration; it does
**not** enter ESP32-style deep sleep. Reboot restarts AWTRIX's process. Linux,
the bootloader and MCU firmware are preserved.

### Display flicker workaround (2026-09-15)

The owner observed a flickering green LED on the bottom row, pixel 5 from the
left, while showing the battery app. The web preview stays steady. Palette tests
also reproduce it with RGB **0, 66, 0** (`#004200`) and **0, 75, 0** (`#004B00`),
including at maximum brightness. The cause is still unknown: the display driver,
panel or controller needs further investigation.

As a temporary workaround, the native battery icon's green border uses RGB
**0, 122, 0** (`#007A00`), the owner's chosen level where the flicker is no longer
visible. These are colours before brightness and gamma processing. Battery fill
and red/orange warning colours retain their existing behaviour. This masks the
observed symptom; it does not fix the underlying cause or other affected colours.

Investigation is deferred. Next, compare the same colours and physical LED with
the original stock firmware. The stock restore was paused before any flashing;
the original stock recovery and a current AWTRIX/configuration backup are saved
locally.

## Display sizing (1.1.0-tc002.3)

- The approved clock, date and battery layouts render directly at 52 × 16 with
  uniform 2 × 2 font pixels and fixed, balanced spacing.
- Clock/calendar: a 16 × 16 tile with two header rows, centred day at baseline 14,
  time starting at column 17, and seven 4 × 2 weekday markers starting at column
  17 with one-pixel gaps. The marker colours, week start and separator animation
  follow the existing settings.
- Numeric date (`DD.MM.YY`): 50-pixel text width with equal one-pixel side margins,
  equal 2 × 2 dots, and seven 6 × 2 weekday markers with equal outer margins.
- Battery: a symmetric 10 × 16 icon centred in a 16-column area; percentage text
  centres in the remaining 34-column area at every value, including 0% and 100%.
  Charge colours, fractional fill and the low-battery colour override are retained.
- Alternate clock modes (big/binary), date formats too wide for the native layout,
  and sensor layouts retain the previous fitted rendering. Wider source layouts
  expand before fitting to avoid edge cropping.
- Pushed apps and notifications use fonts at twice their original size, including
  matching text measurement, centring and scrolling. Effects, transitions, charts
  and overlays render on the physical 52 × 16 canvas.
- Existing 8 × 8 page icons automatically display at 16 × 16. Classic 32 × 8
  backgrounds fit to 52 × 16. Native GIF/JPEG assets up to 52 × 16 retain their
  source pixels; the web UI converts PNG uploads to GIF. Animated GIFs support
  the same sizes. Text reserves the actual icon width plus a two-pixel gap.
- The icon editor starts at 16 × 16 and offers 52 × 16, 8 × 8 and 32 × 8 presets.
  Its live still-frame preview uses the same scaling as saved icons.
- Explicit MQTT/HTTP `draw` coordinates and Berry drawing/text coordinates remain
  physical pixels: `(51,15)` is the bottom-right LED. Berry `icon()` now supports
  native assets through 52 × 16; it draws their original dimensions, so scripts
  can position multiple icons precisely. Existing scripts with hardcoded 32 × 8
  layouts need their coordinates updated; use `width()` and `height()`.
- The live web preview has the correct 52:16 aspect ratio. Progress bars and
  status indicators are two pixels thick; dense bar charts retain their final
  sample and fill the available width.

## Physical controls (1.1.0-tc002.4)

- Turn the knob to move between apps.
- Tap **−/+** to lower/raise speaker volume by 5 percentage points on release.
  Tone, MP3 and radio volume each change by the same amount, within 0–100%.
  Since 1.1.0-tc002.6, a speaker icon and percentage appear for 1.5 seconds after
  each tap (with a mute symbol at 0%). The percentage follows MP3/radio while
  playing, or tone volume when idle. The current app resumes automatically afterward.
- Hold **−/+** for 0.7 seconds to lower/raise brightness by 10 (on the 1–255
  scale), repeating every 0.2 seconds while held. A hold does not change volume.
- Knob/select presses retain notification dismissal and double-press power control.

Volume and brightness changes use the normal settings persistence and MQTT state
updates. Physical −/+ labels keep their meaning when the display is rotated;
rotation and button-swap settings still apply to knob navigation.

## Audio and local DNS fixes (1.1.0-tc002.5)

The Audio page supports listing, uploading and deleting MP3 files through the
upstream audio API. AWTRIX uses the DNS servers supplied by Wi-Fi DHCP, or the
configured servers for static addressing, so local broker names can resolve.
Its resolver file lives in RAM in a private mount namespace; the read-only
system image and other processes keep their original resolver file.

## Build on Linux x86-64

Install `uv`, a C/C++ build environment, Perl, curl, tar, and `squashfs-tools`.
Python tooling always runs through uv.

```sh
git clone --recurse-submodules https://github.com/sanderdw/awtrix-ng-tc002.git
cd awtrix-ng-tc002
bash tools/build.sh
```

The `upstream/awtrix-ng` submodule must be checked out (`git submodule update --init` on an
existing clone). Configuring applies `patches/` onto it into `build-upstream/`.

The script downloads SHA-256-pinned Arm GNU 9.2-2019.12 and OpenSSL 3.5.8,
uses `uv sync --locked`, cross-compiles, and writes stripped binaries into
`dist/bin/`. It does not contact or flash a clock.

Existing build dependencies can be reused:

```sh
TC002_TOOLCHAIN=/path/to/gcc9 TC002_TLS=/path/to/openssl-build bash tools/build.sh
```

## Install

```sh
curl -fsSL https://raw.githubusercontent.com/sanderdw/awtrix-ng-tc002/main/install.sh | sh -s -- CLOCK_IP
```

Needs `python3`, `curl`, `unzip` and squashfs-tools on a Linux or macOS computer (WSL on
Windows); `adb` is downloaded for you. The installer reads your clock's application partition,
verifies the stock firmware, builds `update.img` and `restore-stock.img` on your computer, runs
the helper's preflight, and flashes only after you type `flash`. It never downloads a firmware
image, because the image contains Ulanzi's own application. Details, requirements, the way back
to stock and what to do if the clock does not come back: [docs/INSTALL.md](docs/INSTALL.md).
Prefer trying it from RAM first; that flashes nothing.

## Try from RAM first

**[Choose an experimental trial release](https://github.com/sanderdw/awtrix-ng-tc002/releases)**
to try it without compiling or flashing firmware. Install
[Google Platform-Tools](https://developer.android.com/tools/releases/platform-tools)
and [uv](https://docs.astral.sh/uv/getting-started/installation/), extract the ZIP,
and open a terminal in its `awtrix-ng-tc002-trial` folder. With `adb` and `uv` on
your PATH, substitute your clock's IP:

```sh
adb connect 192.168.100.190:5555
uv run --no-project try.py 192.168.100.190 --binary bin/awtrix-tc002 --seconds 180
```

Open `http://192.168.100.190:18081` when the terminal prints `Trial ready`. The
installed app restarts after three minutes. During the trial, avoid Wi-Fi,
factory-reset, firmware-update and reboot controls; those affect real hardware.

Each release has a separate version tag, such as `v1.1.0-tc002.7`. GitHub Actions
tests and builds that tag before publishing its own ZIP and checksum. Published
tags and downloads are preserved; `manifest.json` records the exact source
commit. Pushes to `main` run checks without replacing a release. These are
experimental builds, not automatically installed updates for your clock.

For an overview, prerequisites and recovery commands, see
[the temporary trial guide](docs/TRY-IT.md). Maintainers can prepare a prebuilt
public trial ZIP after building with:

```sh
uv run tools/package_trial.py
```

The archive and checksum are written to `dist/public/`. It contains the native
application, web UI, CA certificates, trial runner and license notices. It does
not contain vendor device files, personal backups or a firmware updater. Extract
it and run the commands in its README; no compilation is needed to try it.

Use Google Android platform-tools ADB; the Python adb libraries cannot do file
sync with the TC002's old ADB, and `adb reverse` is unsupported. Port 5555 must
be reachable on your local network. `bash tools/fetch_platform_tools.sh`
downloads adb into `build-deps/`, where every tool finds it; `ADB=/path/to/adb`
overrides that.

```sh
uv run tools/trial.py CLOCK_IP \
  --binary dist/bin/awtrix-tc002 --seconds 180 --tone
```

Replace `CLOCK_IP` with your clock's address and open `http://CLOCK_IP:18081`
during the trial. The script temporarily stops the `zkswe` launcher service,
uses an isolated `/tmp` data directory, and restarts the installed service when
AWTRIX exits (stock on an unmodified clock, AWTRIX after installation).
The trial expires on the device even if the host test
client disconnects. It does not modify `/res` or permanent AWTRIX configuration.
A power cycle starts the firmware currently installed in flash.

## Build update and recovery images

Keep a backup of your own clock's live `/res` partition and a valid stock TC002
ZKSWE `update.img`. The latter supplies the device's container metadata; recovery
is generated from the **live backup**, not from an older stock update.
Store your inputs locally in the ignored `device-private/` directory. Vendor
firmware, device backups and ready-made update/recovery images are not distributed
in this repository. Build images from your own clock's backup.

```sh
uv run tools/image.py --stock device-private/stock-update.img \
  --live-res device-private/live-res.bin --build dist/bin --output dist
```

Outputs:

- `dist/update.img`: AWTRIX plus the original vendor bootstrap in the res partition.
- `dist/restore-stock.img`: the original live res filesystem.
- `dist/manifest.json`: lengths, SHA-256 checksums and upstream revision.

The packer checks the TC002 platform header, CRC, payload MD5, filesystem bounds
and 8 MiB res partition limit. It preserves the stock compressor and block size.
The native updater independently validates the container and checks the live
NOR flash layout before erasing. It writes and reads back one erase block at a
time. Neither tool writes other flash partitions.

### Installation and recovery

Permanent installation and automatic startup after reboot have passed on the test
clock. Keep stable USB power and the recovery image available on the host.
The helper stops the GUI and its Bluetooth helper before unmounting `/res`;
if unmounting fails, it aborts before erasing and restarts the GUI.

The built helper accepts:

```sh
# Validation only; available on host and clock.
build-host/tc002-update --validate dist/update.img
# On the clock, additionally check its partition geometry without writing:
/tmp/awtrix-update-helper --preflight /tmp/awtrix-update.img
```

For installation, copy `dist/bin/tc002-update` to `/tmp/awtrix-update-helper` and
the selected image to `/tmp/awtrix-update.img`, then execute on the clock:

```sh
chmod 700 /tmp/awtrix-update-helper
/tmp/awtrix-update-helper --install /tmp/awtrix-update.img
```

**`--install` writes flash and reboots.** It first checks the vendor files on the clock
against the recorded fingerprints and refuses an unrecognised stock firmware unless `--force`
is added. Use `restore-stock.img` with the same helper to restore stock. If a write fails
and ADB remains available, keep power on and inspect the error before retrying. Do not
interrupt power during a write.

If AWTRIX does not come up after an install:

- **Hold the knob while powering on.** The launcher starts the vendor application instead;
  its stock UI, updater and ADB are back. Power-cycle without holding to try AWTRIX again.
- **Three crashes in a row** at start-up (the app exiting before a minute of uptime, so
  that init restarts it) do the same automatically on the fourth start. Pulling the power
  does not count: on this clock's flash a write made in the first seconds after boot does
  not survive a power cut, so a boot-loop caused by power cuts still needs the knob.
- If neither the vendor application nor ADB comes back, recovery needs the stock
  bootloader's update path or a serial connection; this has not been validated.

After installation the normal web UI is at the clock's address on the configured
port (default 80). System → Maintenance accepts TC002 `.img` updates: the upload streams
to `/data/awtrix-ng/staging` (flash, not RAM), the helper's preflight must pass before the
install starts, and the log is at `/data/awtrix-ng/update.log`, which survives the reboot.
Assets and configuration live in `/data/awtrix-ng` and survive res updates.
Restoring stock leaves that AWTRIX data directory intact.

## MQTT example

Enable MQTT and set the broker in the web UI. With prefix `awtrixNG`:

```sh
mosquitto_pub -h BROKER -t 'awtrixNG/cmd/notify' \
  -m '{"text":"Hello TC002","durationMs":5000}'
mosquitto_sub -h BROKER -t 'awtrixNG/cmd/notify/result'
mosquitto_pub -h BROKER -t 'awtrixNG/cmd/screen/get' -m ''
```

The screen reply is `awtrixNG/state/screen`, with width 52, height 16 and 832
pixels. See the upstream reference for the complete topic list.

## Development checks

A host build requires OpenSSL development headers:

```sh
uv run cmake -S . -B build-host -G Ninja -DCMAKE_BUILD_TYPE=Release   # add -DOPENSSL_ROOT_DIR=... if OpenSSL is not system-wide
uv run cmake --build build-host -j4
uv run ctest --test-dir build-host --output-on-failure
uv run pytest -q
```

For MQTT tests against the actual ARM binary, compile `tools/mqtt_test_relay.c`
with the cross compiler, then:

```sh
AWTRIX_DEVICE_SERIAL=CLOCK_IP:5555 \
AWTRIX_TEST_RELAY=/path/to/arm-relay AWTRIX_TEST_BINARY=dist/bin/awtrix-tc002 \
uv run pytest -q tests/test_integration.py
```

The relay is confined to loopback on the clock and uses ADB forwarding to a
local broker. Tests restart the installed launcher service on exit. Do not run
two physical trials at once.

## Contributing

Bug reports and improvements for the TC002 port are welcome in
[this repository](https://github.com/sanderdw/awtrix-ng-tc002/issues).
See [CONTRIBUTING.md](CONTRIBUTING.md) for the source layout and validation steps.

## License

Published under **PolyForm Noncommercial 1.0.0**, following upstream AWTRIX NG.
This is **source-available**, with noncommercial terms, rather than OSI-approved
open-source software. Upstream copyright and third-party licenses are retained. See
[LICENSE.md](LICENSE.md) and [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
