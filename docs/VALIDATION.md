# Release validation protocol

A build is a release candidate until every step below has been run on a real clock with that
exact build. Only then is `tools/image.py --validated YYYY-MM-DD` used, which is what turns the
manifest status from "not cold-boot validated" into a dated statement. Record the run in
`WORKLOG.md` with the version, stock app and MCU versions, and anything that did not go to plan.

Supported stock firmware: TC002 app **1.1.1**, MCU **V1.0.17**. The firmware refuses to use the
vendor audio and network libraries, and the updater refuses to install, unless the files on the
clock match the hashes in `src/tc002/vendor-fingerprints.json`. Capture them once per supported
stock version with `uv run tools/vendor_fingerprints.py capture CLOCK_IP` and commit the JSON.

## Before flashing

1. Keep the clock on USB power for the whole protocol.
2. Have `restore-stock.img` built from this clock's own `/res` dump, and a working ADB connection.
3. `tc002-update --preflight update.img` on the clock passes.

## Install and warm restart

4. Install through the web UI (System, Maintenance). The response must be 202 and the clock must
   come back with the new version in `/api/v1/version` within three minutes.
5. `GET /api/v1/tc002/vendor` reports every library `trusted: true`. If any is false, audio or
   Wi-Fi provisioning is deliberately disabled: stop here and capture fingerprints first.
6. Reboot Linux (`uv run tools/device.py CLOCK_IP shell reboot`; the web UI's Reboot only
   restarts the AWTRIX process). The clock comes back on its own within a minute.

## Cold boot

7. Unplug USB and, if the battery is fitted, hold the power control until the clock is fully
   off. Wait 60 seconds. Power on.
8. Within three minutes: the web UI answers, MQTT reports online, NTP has set the time, the
   buttons and knob work, a tone plays.
9. Repeat step 7 once more.

## Recovery without a computer

10. Power off. Hold the knob while powering on. The vendor application must start (the stock
    Ulanzi UI). Power-cycle without holding: AWTRIX starts again.
11. Simulate a broken build: with ADB, `chmod 000 /res/bin/awtrix-tc002` is not possible on the
    read-only image, so instead confirm the counter path: `cat /data/awtrix-ng/boot-attempts`
    must not exist after a healthy minute of uptime. Power-cycle three times within the first
    30 seconds of each boot; the fourth boot must start the vendor application, and the following
    normal boot must start AWTRIX again.

## Restore

12. Upload `restore-stock.img` through the web UI Maintenance page. The stock UI must come back
    after the reboot, and cold boot (step 7) must work on stock.
13. Reinstall AWTRIX and repeat step 8.

Anything untested stays listed under "Remaining release validation" in `WORKLOG.md`. The serial
console recovery path has never been exercised and is not part of this protocol; a clock that
neither boots AWTRIX, nor the vendor fallback, nor answers ADB is outside what this port can
recover today.
