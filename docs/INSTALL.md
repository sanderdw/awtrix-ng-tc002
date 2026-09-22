# Installing AWTRIX NG TC002

One line, run on a Linux or macOS computer (Windows: use WSL) on the same network as the clock:

```sh
curl -fsSL https://raw.githubusercontent.com/sanderdw/awtrix-ng-tc002/main/install.sh | sh -s -- CLOCK_IP
```

Replace `CLOCK_IP` with the clock's address (the stock app shows it, or look in your router).
If you prefer not to pipe into a shell, download `install.sh` from a release, read it, and run
it the same way.

## What you need

- `python3` (3.8 or newer), `curl`, `unzip`, and **squashfs-tools** (`apt install squashfs-tools`
  on Debian/Ubuntu, `brew install squashfs` on macOS). Google's `adb` is downloaded for you.
- The clock connected to your Wi-Fi with the stock Ulanzi app, on USB power, with TCP port 5555
  reachable. Stock firmware opens it; if the installer cannot connect, stop there.
- Supported stock firmware: app **1.1.1**, MCU **V1.0.17**. The installer checks the clock's
  own files against recorded fingerprints and refuses anything else unless told
  `--allow-unverified`, in which case audio and Wi-Fi provisioning stay off on the clock.

## What it does

1. Downloads the installer bundle of a release and verifies its checksum.
2. Reads your clock's application partition (8 MiB) over ADB and checks the vendor files.
   The stock application is checked at `/res/lib/libzkgui.so`. After installation, its
   preserved copy is checked at `/res/lib/libulanzi-bootstrap.so` against the same hash.
3. Builds two images **on your computer** from that partition: `update.img` (this port next to
   the vendor application) and `restore-stock.img` (the vendor layout, to go back).
   No firmware file is ever downloaded: the image contains Ulanzi's own application, which
   cannot be redistributed, so it must come from your clock.
4. Runs the update helper's preflight on the clock, which writes nothing.
5. Asks you to type `flash`, writes the partition, and waits for the clock to come back.

Everything it makes is kept under `~/.awtrix-ng-tc002/CLOCK_IP/DATE/`, including the partition
dump and `restore-stock.img`.

## Going back to stock

```sh
curl -fsSL https://raw.githubusercontent.com/sanderdw/awtrix-ng-tc002/main/install.sh | sh -s -- CLOCK_IP --restore
```

This flashes the `restore-stock.img` from your latest run through the same helper.

## If the clock does not come back

- **Hold the knob while powering on**: the launcher starts the stock Ulanzi app instead. Its
  Wi-Fi and ADB work, so `--restore` works too.
- **Three start-up crashes in a row** do the same automatically on the fourth start.
- The Wi-Fi pixel in the top-left corner may pulse for up to 90 seconds after a cold start
  before the clock has associated; that is normal. A pulsing pixel after three minutes means
  the clock has opened its own access point (`awtrixng-…`); it retries your network every two
  minutes, or you can join the access point and set Wi-Fi at `http://192.168.4.1`.
- A power cut during the write leaves a partition that needs the stock bootloader's own update
  path or a serial connection; that has never been exercised. Do not unplug during the write.

## Risks, plainly

Installing rewrites the clock's application partition in place; there is no second copy on
the device. The helper validates the image, checks the flash geometry and the stock firmware,
and verifies every block it writes, and the launcher can fall back to the stock app, but a
firmware you flash onto a clock is yours to recover. Read the
[README's Risks section](https://github.com/sanderdw/awtrix-ng-tc002#risks) first.
