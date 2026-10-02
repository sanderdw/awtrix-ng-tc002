# Installing AWTRIX NG TC002

One line, run on a Linux or macOS computer (Windows: use WSL) on the same network as the clock:

```sh
curl -fsSL https://raw.githubusercontent.com/sanderdw/awtrix-ng-tc002/main/install.sh | sh -s -- CLOCK_IP
```

Replace `CLOCK_IP` with the clock's address (the stock app shows it, or look in your router).
If you prefer not to pipe into a shell, download `install.sh` from a release, read it, and run
it the same way.

## Choosing a release

Without options the installer takes the newest **stable** release, the one GitHub marks as
Latest. Pre-releases are only installed when you ask for one by tag:

```sh
curl -fsSL https://raw.githubusercontent.com/sanderdw/awtrix-ng-tc002/main/install.sh | sh -s -- --version TAG CLOCK_IP
```

`--version` may come before or after `CLOCK_IP`; the tag is listed on the
[releases page](https://github.com/sanderdw/awtrix-ng-tc002/releases). The environment variable
`AWTRIX_TC002_VERSION=TAG` does the same. The installer prints which release it downloads and
whether it is the stable one or the one you requested.

## What you need

- `python3` (3.8 or newer), `curl`, `unzip`, and **squashfs-tools** (`apt install squashfs-tools`
  on Debian/Ubuntu, `brew install squashfs` on macOS). Google's `adb` is downloaded for you.
- The clock connected to your Wi-Fi with the stock Ulanzi app, on USB power, with TCP port 5555
  reachable. Stock firmware opens it; if the installer cannot connect, stop there.
- Stock firmware: validated on app **1.1.1**, MCU **V1.0.17**; other versions install when the
  clock's own Ulanzi files pass the checks below (1.0.1 and 1.0.8 have been reported working).
  - The audio and network libraries must match a recorded SHA-256. If one does not, the
    installer stops and says what would stay off (audio; or DHCP after the first lease, static
    addressing and the fallback access point). `--allow-unverified` installs anyway with that
    off, and the update helper then runs with `--force`.
  - The Ulanzi application is **verified** by its SHA-256, or **compatible** when it defines
    the four functions the launcher calls. An application without them is refused, with or
    without `--allow-unverified`: holding the knob or three failed starts could not bring the
    stock app back. The installer prints its full SHA-256; please report it in an
    [issue](https://github.com/sanderdw/awtrix-ng-tc002/issues).

## What it does

1. Downloads the installer bundle of the chosen release and verifies its checksum.
2. Reads your clock's application partition (8 MiB) over ADB and checks the vendor files.
   The stock application is checked at `/res/lib/libzkgui.so`, by its SHA-256 or by the entry
   points in its ELF dynamic symbol table (read, never loaded). After installation, its
   preserved copy at `/res/lib/libulanzi-bootstrap.so` is checked the same way.
3. Builds two images **on your computer** from that partition: `update.img` (this port next to
   the vendor application) and `restore-stock.img` (the vendor layout, to go back).
   No firmware file is ever downloaded: the image contains Ulanzi's own application, which
   cannot be redistributed, so it must come from your clock.
4. Runs the update helper's preflight on the clock, which writes nothing.
5. Asks you to type `flash`, writes the partition, and waits for the clock to come back.

Everything it makes is kept under `~/.awtrix-ng-tc002/CLOCK_IP/DATE/`, including the partition
dump and `restore-stock.img`.

## Going back to stock

Use the computer you installed this port from, connected to the same network as the clock.
The restore command needs the `restore-stock.img` saved during installation under
`~/.awtrix-ng-tc002/CLOCK_IP/DATE/`, so keep that backup on your computer.

Replace `CLOCK_IP` below with your clock's current IP address. If its address has changed since
installation, copy the backup directory `~/.awtrix-ng-tc002/OLD_IP/` to
`~/.awtrix-ng-tc002/CURRENT_IP/`, preserving the dated subdirectories and their contents, before
running the command. Use only the backup from this clock.

```sh
curl -fsSL https://raw.githubusercontent.com/sanderdw/awtrix-ng-tc002/main/install.sh | sh -s -- CLOCK_IP --restore
```

This flashes the `restore-stock.img` from your latest run through the same helper, with
`--force`: the image is your clock's own stock partition, so the fingerprint check (which
protects the port's calls into vendor code) does not apply.

Type `flash` when prompted. Keep the clock on USB power and do not unplug it until it has rebooted.
If AWTRIX will not start, hold the knob while powering on to start the stock Ulanzi app, then run
the restore command. More recovery options are below under [If the clock does not come back](#if-the-clock-does-not-come-back).

### Switching to official AWTRIX firmware

After restoring stock, follow Blueforcer's installation instructions when the official TC002
firmware is released. A link to those instructions will be added here when they are available.

## Updating from the web UI

System, Maintenance takes an `update.img` built by this installer (`--build-only`). The clock's
installed helper checks the stock firmware again and never forces. Use the one-line installer
instead if the clock was installed with `--allow-unverified`, or if it runs a release before
1.1.2-tc002.3 that was installed by running the helper with `--force` by hand.

## If the clock does not come back

- **Hold the knob while powering on**: the launcher starts the stock Ulanzi app instead. Its
  Wi-Fi and ADB work, so `--restore` works too.
- **Three start-up crashes in a row** do the same automatically on the fourth start.
- The Wi-Fi pixel in the top-left corner may pulse for up to 90 seconds after a cold start
  before the clock has associated; that is normal. A pulsing pixel after three minutes means
  the clock has opened its own access point (`awtrixng-…`); it retries your network every two
  minutes, or you can join the access point and set Wi-Fi at `http://192.168.4.1`.
- A power cut during the write leaves a partition that needs the stock bootloader's own update
  path: Ulanzi's USB recovery below, or a serial connection. Do not unplug during the write.

### Ulanzi's USB recovery

Ulanzi's procedure reinstalls the stock firmware without the network. It has not been tried with
this port yet.

1. Prepare a 16 GB USB drive and format it as FAT32.
2. Place the upgrade files and `zkautoupgrade` in the root directory of the USB drive.
3. With the base unit powered via the pin connector, power on the device, then insert the USB
   drive into the left-side USB-C port and wait for the device to restart and recover.
4. Once the device restarts and the "Ulanzi" logo appears, remove the USB drive. The device
   should return to normal.

## Risks

Installing rewrites the clock's application partition in place; there is no second copy on the
device. Read the [README's Risks section](https://github.com/sanderdw/awtrix-ng-tc002#risks) first.
