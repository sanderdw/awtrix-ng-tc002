---
name: install-tc002-firmware
description: Install, flash, trial or roll back the AWTRIX NG TC002 firmware built from this repository on a real Ulanzi TC002 clock over the network. Use this whenever the user asks to flash, install, deploy, push, put or try a build on the clock, gives a clock IP or URL together with a build or branch, asks for a RAM trial or a test on hardware, wants to restore the stock Ulanzi app, or asks why the clock did not come back after an update — even if they never say "install". Not for end users installing a published release (that is install.sh in docs/INSTALL.md) and not for building or bumping upstream.
---

# Installing a development build on a TC002

The clock has one 8 MiB application partition and no second copy. Installing erases and rewrites it
in place, so the procedure is arranged to find problems *before* the write: prove the binary starts
on the hardware from RAM, then let the installer verify the clock and preflight the image right
before it writes. Follow the order; each step is cheap compared with a clock that does not boot.

The project's own tools do the work. This skill adds the order, two known installer defects, and
what to do when something looks wrong. Run every command from the repository root; the tools use
relative paths.

## What the request authorizes

Three levels, from harmless to hard to undo:

- **Looking** (git state, `curl` to the clock, building): always fine.
- **RAM trial and `--build-only`**: write nothing to flash, but connect over adb, and the trial
  stops the clock's application for its duration. Run them when the user asked to try or install a
  build on a clock. For a question like "is it ready?", answer from what you can see and *offer*
  the trial instead.
- **Flashing or restoring**: only when the user asked for it on this clock in this conversation.
  Say which clock and which commit you are about to write before you do.

## 1. Know what you are about to install

```sh
git status --short && git log --oneline -3          # is the change the user means actually in HEAD?
bash tools/build.sh                                  # incremental; produces build-tc002/ and dist/bin/
curl -s -m 5 http://CLOCK_IP/api/v1/version          # what the clock runs now (empty: stock app or unreachable)
```

If the user names a change ("the brightness fix", a branch, a PR), confirm the checkout contains it
before building; ask rather than switching branches on a guess.

`tools/build.sh` strips the binaries into `dist/bin/`, and that is what gets installed. The version
is `AWTRIX_NG_VERSION` in `CMakeLists.txt`. When it equals what the clock already reports, tell the
user: old and new cannot be told apart by version afterwards, so plan another way to prove the new
build is running (a behaviour you can observe, or bump the version first). A dirty tree is allowed
but worth mentioning, because the bundle manifest records it. If the build fails on a toolchain
path that no longer exists, `rm -rf build-tc002` and build again.

## 2. RAM trial (writes nothing)

```sh
uv run tools/trial.py CLOCK_IP --seconds 120        # 20..600; add --tone to hear the buzzer
```

The command blocks until the trial ends, so start it in the background and do your checks while it
runs. It stops the clock's application service, runs `build-tc002/awtrix-tc002` from `/tmp` on
**port 18081** with throw-away data, then restarts what was installed. Against
`http://CLOCK_IP:18081`:

- `/api/v1/version` reports the new version; `/api/v1/capabilities` shows
  `matrix: {width: 52, height: 16, fixed: true}` and the expected audio flags.
- Exercise what the build changes and read the result back instead of assuming it:

  ```sh
  curl -s -X POST http://CLOCK_IP:18081/api/v1/notifications -H 'Content-Type: application/json' \
       -d '{"text":"Hi","hold":true,"stack":false}'
  curl -s http://CLOCK_IP:18081/api/v1/display/screen   # "pixels": 832 colours, row-major, 52 wide
  ```

  Script apps go in with `PUT /api/v1/apps/script/NAME` (body: the Berry source, `text/plain`).
- Afterwards read `awtrix-trial-host.log` in this computer's temp directory (`$TMPDIR`, else `/tmp`)
  for crashes or an early exit.

Know the trial's limits: it runs with `--no-matrix`, so the panel stays dark, `fps` reads low, and
the screen endpoint returns colours *before* brightness and gamma. Brightness, panel output and
anything else only visible on the LEDs cannot be proven here — say so, and check it after the
install with the user's eyes on the clock.

When the trial ends, confirm port 80 answers with the *old* version again before going on. If the
binary exits early or the log shows a crash, stop: do not flash a build that cannot survive two
minutes from RAM.

`tools/trial.py` finds adb from `$ADB`, then `PATH`, then `build-deps/platform-tools/adb`
(`tools/fetch_platform_tools.sh` fetches it). The clock needs TCP 5555 reachable; adb answers both
under the stock app and under this port.

## 3. Install

Use the bundled wrapper, not `tools/install.py` directly:

```sh
python3 .claude/skills/install-tc002-firmware/scripts/flash_dev_build.py CLOCK_IP --build-only   # optional
python3 .claude/skills/install-tc002-firmware/scripts/flash_dev_build.py CLOCK_IP --yes
```

Why the wrapper: run from the repository, `tools/install.py` fails looking for
`vendor-fingerprints.json` (it only exists in the packaged bundle), so the wrapper packages the
bundle with `tools/package_installer.py` into a temporary directory and runs the installer from
there. The installer's flash step uses a 120 s `adb shell` that can hang while the clock reboots
under it; the installer now tolerates that and keeps waiting for the clock, and the wrapper still
keeps polling if an installer dies *after the write has started* instead of reporting failure. It uses `$ADB` or
`build-deps/platform-tools/adb`; failing those, the installer looks on `PATH` and otherwise
downloads Google's platform-tools into `~/.awtrix-ng-tc002/`.

`--build-only` dumps the partition, verifies the vendor fingerprints and builds both images, then
stops. It does **not** run the on-clock preflight: that happens inside the `--yes` run, immediately
before the write, and a refusal there still stops with nothing written. `--yes` is required because
the installer's typed `flash` confirmation reads `/dev/tty`, which you cannot answer; the user's
request is the confirmation.

A healthy run prints, in order: the current version; three vendor files `verified` (the Ulanzi
application, `libulanzi-bootstrap.so`, may instead read `compatible` with its full SHA-256 on a
stock version other than 1.1.1: it has no recorded hash but defines the launcher's entry points,
and that is fine); the sizes of
`update.img` and `restore-stock.img` and the run directory under `~/.awtrix-ng-tc002/CLOCK_IP/`;
`Preflight passed: ... nothing written`; the warning; "installing". Stops before "installing" wrote
nothing and are safe to investigate and retry:

- *the stock application is not a build this port knows* (`libulanzi-bootstrap.so UNKNOWN BUILD`,
  "does not define …") — a hard stop. `--allow-unverified` does not change it, and never run the
  helper with `--force` by hand to get around it: without those entry points the knob-hold and
  three-strikes fallbacks cannot bring the stock app back. Give the user the printed SHA-256 and
  missing names to report in an issue, with the app and MCU versions from the stock web page.
- *vendor files this port was not verified with* (`libmi_ao.so` or `libzknet.so` `UNKNOWN BUILD`)
  — do not add `--allow-unverified` on your own: explain what the installer printed would stay off
  (audio for libmi_ao; DHCP after the first lease, static addressing and the fallback access point
  for libzknet), that the flag makes the helper run with `--force`, and that later updates must
  then go through the installer, not the web UI. Let the user decide.
- *preflight refused* — read the helper's message; it names the check that failed.
- *application service is not running* — power-cycle the clock and start again.

Once "installing" has printed, the write is under way. Do not re-run, do not start a trial, do not
reboot the clock. It goes dark for one to two minutes and then reboots.

## 4. Verify, and hand over the recovery path

```sh
curl -s http://CLOCK_IP/api/v1/version       # the new version
curl -s http://CLOCK_IP/api/v1/device        # fps in the 40s on a healthy clock, currentApp, updateImage == ""
curl -s http://CLOCK_IP/api/v1/apps          # their apps are still there
curl -s http://CLOCK_IP/api/v1/settings      # their settings survived (they live in /data, not in the image)
```

`updateImage` must stay empty: that is what keeps upstream's browser "Download & install" (ESP32
images) out of this clock's web UI. Report the version, that apps and settings survived, anything
the trial could not prove and the user should look at, and the path of `restore-stock.img` from
this run — they need it to go back.

Then record it. This project treats anything not run on a clock as unvalidated, so add a dated
WORKLOG.md entry with the commit, what the trial and the install showed, and what was *not* checked.
A development install is not a `docs/VALIDATION.md` run: cold boots, knob-hold fallback,
three-strikes fallback and restore-stock remain separate owner steps before tagging a release.
Leave committing to the user unless asked.

## Going back to stock

```sh
python3 .claude/skills/install-tc002-firmware/scripts/flash_dev_build.py CLOCK_IP --restore --yes
```

This flashes the newest `~/.awtrix-ng-tc002/CLOCK_IP/*/restore-stock.img`, always with the helper's
`--force` (the image is the clock's own stock partition). It is a write like any other and needs
the same explicit request. Afterwards the stock app has no `/api/v1/version`; check
the display, or `adb shell getprop init.svc.zkswe` (expect `running`).

## If the clock does not come back

Do not flash again as a first reaction. First find out: the clock's IP (it may have changed — have
the user check the router, and try `adb connect IP:5555`, which can answer when the web UI does
not), how long ago the install was, and whether `--allow-unverified` was used and for which file (for
`libzknet.so` there is no fallback access point, so the one below will not appear). Several steps need hands on the
clock or a phone on its access point; those are the user's, so give them as instructions.

1. **Right after an install: wait.** The Wi-Fi pixel (top-left) may pulse for 90 seconds after a
   cold start.
2. **Pulsing for more than three minutes** means the clock opened its own access point
   `awtrixng-…` and retries the network every two minutes. The user joins it and sets Wi-Fi at
   `http://192.168.4.1`. Usual cause: changed Wi-Fi credentials or a router problem, not the flash.
3. **One normal power cycle** (user).
4. **Hold the knob while powering on** (user): the launcher starts the stock Ulanzi app, whose
   Wi-Fi and ADB work, so `--restore` works from there once the user asks for it. Three crashed
   start-ups in a row do the same on the fourth.
5. A power cut *during* the write is the one case with no tested recovery (it needs the stock
   bootloader's update path or a serial console). Say so plainly rather than improvising.

## Reporting back

Lead with the outcome: which version the clock now reports, or exactly where it stopped and that
nothing was written. Then what you checked on the hardware, what you could not, and where the
recovery image is. Mention any tool misbehaviour you met so it can be fixed in `tools/`.
