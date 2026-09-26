# Release validation protocol

A build is a release candidate until every step below has been run on a real clock with that
exact build. Only then is `tools/image.py --validated YYYY-MM-DD` used, which is what turns the
manifest status from "not cold-boot validated" into a dated statement. Record the run in
[WORKLOG.md](WORKLOG.md) with the version, stock app and MCU versions, and anything that did not go to plan.

Validated stock firmware: TC002 app **1.1.1**, MCU **V1.0.17**. The firmware refuses to use the
vendor audio and network libraries, and the updater refuses to install, unless they match the
hashes in `src/tc002/vendor-fingerprints.json`. The Ulanzi application must match a hash there
or define every function listed under its `symbols` (the launcher's entry points). Run this
protocol on a clock before recording its hashes with
`uv run tools/vendor_fingerprints.py capture CLOCK_IP`, and commit the JSON: a recorded hash is
what `verified` means.

## Before flashing

1. Keep the clock on USB power for the whole protocol.
2. Have `restore-stock.img` built from this clock's own `/res` dump, and a working ADB connection.
3. `tc002-update --preflight update.img` on the clock passes without `--force`, and prints
   `verified; defines the launcher's 4 entry points` for `libulanzi-bootstrap.so`.

## Install and warm restart

4. Install through the web UI (System, Maintenance). The response must be 202 and the clock must
   come back with the new version in `/api/v1/version` within three minutes.
5. `GET /api/v1/tc002/vendor` reports every library with `status: verified` and the vendor
   application with `missingSymbols: []`. `compatible` for `libulanzi-bootstrap.so` means this
   clock's stock version has no recorded hash yet; `unknown` for `libmi_ao.so` or `libzknet.so`
   means audio or networking is deliberately disabled. Either way, stop here and capture
   fingerprints first.
6. Reboot Linux (`uv run tools/device.py CLOCK_IP shell reboot`; the web UI's Reboot only
   restarts the AWTRIX process). The clock comes back on its own within a minute.

## Cold boot

7. Unplug USB and, if the battery is fitted, hold the power control until the clock is fully
   off. Wait 60 seconds. Power on.
8. Within three minutes: the web UI answers, MQTT reports online, NTP has set the time, the
   buttons and knob work, a tone plays. With `mosquitto_sub -t '<prefix>/state/knob' -v`
   running, one clockwise detent prints one `cw` and one anticlockwise detent prints one `ccw`,
   with block buttons both off and on.
9. Repeat step 7 once more.

## Recovery without a computer

10. Power off. Hold the knob while powering on. The vendor application must start (the stock
    Ulanzi UI). Power-cycle without holding: AWTRIX starts again.
11. Simulate a crash loop over ADB: find the `awtrix-tc002` process id and `kill -9` it, wait
    for init to restart it, and repeat until the launcher's `/data/awtrix-ng/launcher.log`
    shows "attempt 3 of 3"; the next kill must produce "starting the vendor application" and
    the stock UI. A normal power cycle must start AWTRIX again. Do not use power cuts for this
    step: writes made in the first seconds after boot do not survive a power cut on this
    flash, so pulled plugs are not counted (measured 2026-09-16).

## Restore

12. Upload `restore-stock.img` through the web UI Maintenance page. The stock UI must come back
    after the reboot, and cold boot (step 7) must work on stock.
13. Reinstall AWTRIX and repeat step 8.

Anything untested stays listed under "Open checks" in [WORKLOG.md](WORKLOG.md). The serial
console recovery path has never been exercised and is not part of this protocol; a clock that
neither boots AWTRIX, nor the vendor fallback, nor answers ADB is outside what this port can
recover today.
