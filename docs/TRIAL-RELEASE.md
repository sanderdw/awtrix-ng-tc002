# Experimental RAM trial @TAG@

Try **AWTRIX NG TC002** for three minutes without installing firmware. The trial
temporarily controls the display and buttons, then restarts the app already
installed on your clock. No compilation is needed.

**This is an experimental build of `@TAG@`.** Automated tests do not
replace physical testing on every TC002 revision. Hardware validation so far
covers stock app 1.1.1 and MCU V1.0.17.

## Download

- [Trial ZIP](https://github.com/sanderdw/awtrix-ng-tc002/releases/download/@TAG@/awtrix-ng-tc002-trial.zip)
- [SHA-256 checksum](https://github.com/sanderdw/awtrix-ng-tc002/releases/download/@TAG@/awtrix-ng-tc002-trial.zip.sha256)

The download contains the prebuilt ARM application, web UI, CA certificates,
trial runner and license notices. It contains no vendor runtime libraries,
personal device backup or firmware update image.

## Quick start

1. Connect your TC002 to Wi-Fi using its stock app. Keep the clock on USB power
   and use a computer on the same LAN.
2. Install [Google Platform-Tools](https://developer.android.com/tools/releases/platform-tools)
   (`adb`) and [uv](https://docs.astral.sh/uv/getting-started/installation/).
   Put both on your PATH.
3. Download and extract the ZIP. Open a terminal in the extracted
   `awtrix-ng-tc002-trial` folder containing `try.py`.
4. Replace the example IP below with your clock's IP, then run:

```sh
adb connect 192.168.100.190:5555
uv run --no-project try.py 192.168.100.190 --binary bin/awtrix-tc002 --seconds 180
```

Once the terminal prints **Trial ready**, open
**http://192.168.100.190:18081** using your clock's IP. Leave the terminal open;
after three minutes the installed app restarts. Port 5555 must already be
reachable; if ADB cannot connect, stop here.

**During the trial, avoid Wi-Fi changes, factory reset, maintenance/update and
reboot controls.** These are real hardware controls; a firmware update would
leave the temporary trial path. Saving system configuration can restart the
application and its trial timer. Run only one trial at a time.

## Returning to the installed app

Normally, wait for the trial to end. If its time has expired but the installed
app has not returned, and ADB still works:

```sh
adb -s 192.168.100.190:5555 shell setprop ctl.start zkswe
```

If Wi-Fi drops during this RAM trial, a power cycle boots the firmware already
installed in flash. This does **not** mean a firmware flash can be interrupted.

[Full trial guide](https://github.com/sanderdw/awtrix-ng-tc002/blob/main/docs/TRY-IT.md)

## Versioned releases

Each version gets a separate tag and release. GitHub Actions runs host regression
tests, builds the tagged ARM application and validates the archive before
publishing its downloads. Existing tags and release assets are preserved.
Each ZIP records its version, source commit and file checksums in `manifest.json`.
Choose a newer release to try a newer build. Your clock does not install updates
automatically.
