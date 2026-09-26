# Development

## Source layout

- `upstream/awtrix-ng/`: AWTRIX NG as a git submodule, pinned and never edited
  (`git submodule update --init` after cloning).
- `patches/`: the TC002 changes to upstream. How they are made and applied: [PORTING.md](PORTING.md).
- `src/tc002/`: the platform layer (board, buttons, audio, Wi-Fi, DNS, mDNS, HTTP routes, native
  52 × 16 layouts, entry point). `src/loader/` is the vendor-app launcher, `src/updater/` the flash
  helper `tc002-update`.
- `vendor/` and `assets/`: dependencies, licences and the public CA bundle.
- `tools/`: build, image packaging, RAM trials, installer and device checks.
- `tests/`: host integration, hardware packing, image validation and pixel fixtures.
- `docs/`: [INSTALL.md](INSTALL.md) and [TRY-IT.md](TRY-IT.md) are shipped as the README of the
  installer and trial ZIPs; [TRIAL-RELEASE.md](TRIAL-RELEASE.md) and `releases/<tag>.md` make up the
  release notes; [VALIDATION.md](VALIDATION.md) is the on-clock release protocol;
  [WORKLOG.md](WORKLOG.md) records what was measured on hardware.

## Host build and checks

Needs a C/C++ compiler, Ninja, OpenSSL development headers and `uv`; Python always runs through uv.
No clock or vendor firmware is needed.

```sh
uv sync --locked
uv run cmake -S . -B build-host -G Ninja -DCMAKE_BUILD_TYPE=Release   # add -DOPENSSL_ROOT_DIR=... if needed
uv run cmake --build build-host -j4
uv run ctest --test-dir build-host --output-on-failure
uv run pytest -q
```

Configuring applies `patches/` to the submodule into `build-upstream/` and writes the branded web UI
to `build-webui/`. MQTT tests start a loopback broker; leave `AWTRIX_DEVICE_SERIAL` unset.

## ARM build

Needs `uv`, a C/C++ build environment, Perl, curl, tar and squashfs-tools.

```sh
bash tools/build.sh
```

It downloads the SHA-256-pinned Arm GNU 9.2-2019.12 toolchain and OpenSSL 3.5.8, cross-compiles and
writes stripped binaries to `dist/bin/`. It does not contact a clock. Reuse existing dependencies
with `TC002_TOOLCHAIN=/path/to/gcc9 TC002_TLS=/path/to/openssl-build bash tools/build.sh`.

## On a clock

Use Google's platform-tools `adb`: the Python adb libraries cannot sync files with the TC002's old
ADB, and `adb reverse` is unsupported. `bash tools/fetch_platform_tools.sh` downloads it into
`build-deps/`, where every tool finds it; `ADB=/path/to/adb` overrides that. Port 5555 must be
reachable. Run one trial or test at a time. The `install-tc002-firmware` skill in `.claude/skills/`
wraps the trial-then-install flow below.

**RAM trial of a local build** (writes no flash; the installed app returns when it ends):

```sh
uv run tools/trial.py CLOCK_IP --binary dist/bin/awtrix-tc002 --seconds 180 --tone
```

The web UI is at `http://CLOCK_IP:18081`. The trial stops the `zkswe` launcher service, uses a data
directory in `/tmp`, and expires on the clock even if the host disconnects.

**MQTT integration tests against the ARM binary**: cross-compile `tools/mqtt_test_relay.c`, then

```sh
AWTRIX_DEVICE_SERIAL=CLOCK_IP:5555 \
AWTRIX_TEST_RELAY=/path/to/arm-relay AWTRIX_TEST_BINARY=dist/bin/awtrix-tc002 \
uv run pytest -q tests/test_integration.py
```

The relay listens on loopback on the clock and reaches a local broker through ADB forwarding.

## Firmware images by hand

`tools/install.py` (what `install.sh` runs) builds the images from the clock's live partition. To
build them from a saved dump instead, keep your own backups in the ignored `device-private/`
directory; vendor files and images are never committed or published.

```sh
uv run tools/image.py --live-res device-private/live-res.bin --build dist/bin --output dist
```

`--stock update.img` optionally takes the container header from a stock Ulanzi update instead of
synthesizing it.

This writes `dist/update.img` (AWTRIX plus the preserved vendor application), `dist/restore-stock.img`
(the live partition as it was) and `dist/manifest.json`. The packer checks the container header,
CRC, MD5, filesystem bounds and the 8 MiB partition limit.

The update helper validates and installs:

```sh
build-host/tc002-update --validate dist/update.img              # host or clock, writes nothing
/tmp/awtrix-update-helper --preflight /tmp/awtrix-update.img    # on the clock, also checks flash geometry
/tmp/awtrix-update-helper --install /tmp/awtrix-update.img      # writes the res partition and reboots
```

(copy `dist/bin/tc002-update` to `/tmp/awtrix-update-helper` and `chmod 700` it first). `--install`
refuses stock firmware whose vendor files fail the fingerprint gate unless `--force` follows the
image path; the installer's `--allow-unverified` and `--restore` add it, the web UI never does.
Never force an install over a vendor application without the launcher's entry points: the knob-hold
and three-strikes fallbacks need them. Stock is restored with `restore-stock.img` and `--force`.
The helper only writes the res partition, one erase block at a time with read-back. Keep stable
power; if a write fails with ADB still up, inspect the error before retrying.

## Vendor fingerprints

`src/tc002/vendor-fingerprints.json` holds the SHA-256 of the vendor files a build was validated on.
Record a new stock version only after running [VALIDATION.md](VALIDATION.md) on it:
`uv run tools/vendor_fingerprints.py capture CLOCK_IP`.

## Releasing

The firmware version is `AWTRIX_NG_VERSION` in `CMakeLists.txt`. Add the release notes as
`docs/releases/v<version>.md`, commit, then tag and push:

```sh
git tag -a v1.1.2-tc002.4 -m 'Release 1.1.2-tc002.4'
git push origin main v1.1.2-tc002.4
```

`.github/workflows/experimental-trial.yml` checks that the tag matches the version, runs the tests,
builds, and publishes a pre-release with the trial and installer ZIPs and their checksums. Pushes to
`main` only run checks. Never move a published tag or replace its downloads; make a new version.

A pull request from a branch of this repository publishes a test pre-release `v<version>-pr<number>`,
installable with `install.sh --version v<version>-pr<number> CLOCK_IP`. Each push replaces it and
closing the pull request deletes it. The clock reports the plain version, so give testers the commit
from the release notes. Pull requests from forks are built and tested only.

`tools/package_trial.py` and `tools/package_installer.py` build the public ZIPs into `dist/public/`
locally. `tools/package_release.py` builds a personal package containing your own stock files;
never publish it.
