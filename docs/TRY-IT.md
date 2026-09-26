# Try AWTRIX NG TC002 without installing firmware

This timed trial runs the application from RAM. It temporarily takes over the
display and controls, then restarts the app already installed on your clock.
It does not flash firmware. Trial assets and application settings use `/tmp`;
normal AWTRIX settings and the stock application are not replaced.

Hardware validation so far covers TC002 stock app 1.1.1 and MCU V1.0.17. This is
an experimental port, not a guarantee that every TC002 revision will work.

Download the trial ZIP from a [release](https://github.com/sanderdw/awtrix-ng-tc002/releases);
`manifest.json` inside records its version and source commit.

## Before starting

1. Connect the TC002 to Wi-Fi using its stock app and find its IP address.
2. Use a computer on the same LAN and keep the clock on stable USB power.
3. Install [Google SDK Platform-Tools](https://developer.android.com/tools/releases/platform-tools)
   and put its `adb` executable on your PATH. Android Studio is not needed.
4. Install [uv](https://docs.astral.sh/uv/getting-started/installation/).
5. Extract the trial ZIP and open a terminal in the extracted folder containing
   `try.py`.

Use your clock's IP in place of `192.168.100.190` below. TCP port 5555 must already
be available on the clock. If ADB cannot connect, stop here; this trial does not
unlock a bootloader or enable debugging on unsupported firmware.

## Run a three-minute trial

These commands work with `adb` and `uv` on PATH:

```sh
adb connect 192.168.100.190:5555
uv run --no-project try.py 192.168.100.190 --binary bin/awtrix-tc002 --seconds 180
```

When the terminal prints `Trial ready`, open:

```text
http://192.168.100.190:18081
```

Leave the terminal open. The application exits after three minutes and the
installed launcher restarts. The timer also runs on the clock, so a dropped
computer connection does not remove the time limit. The supported duration is
20–600 seconds. Sound playback is optional; the command above does not play a
startup tone.

Explore the dashboard, display, icons and audio controls. **Avoid Wi-Fi changes,
factory reset, maintenance/update and reboot controls during this trial.** The
UI still exposes hardware administration; changing Wi-Fi affects the real
network connection, and a firmware update would leave the temporary-trial path.
Saving system configuration can restart the app and restart its trial timer.

## Return to the installed app

Normally, wait for the trial to end. If the launcher does not return and ADB is
still reachable, use:

```sh
adb -s 192.168.100.190:5555 shell setprop ctl.start zkswe
```

If the clock loses Wi-Fi during the RAM trial, a power cycle boots the firmware
already installed in flash. This advice applies to the RAM trial above, not to
interrupting a firmware installation. Start only one trial at a time.

## Permanent installation

The trial ZIP does not install anything. To install, use the one-line installer:
https://github.com/sanderdw/awtrix-ng-tc002/blob/main/docs/INSTALL.md

The web UI's documentation button and icon editor load from the upstream author's hosting.
Please support AWTRIX NG at https://ko-fi.com/blueforcer. This archive includes the licence and
third-party notices; the clock's proprietary libraries are not included.
