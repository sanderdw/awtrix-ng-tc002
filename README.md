# AWTRIX NG TC002

An unofficial, community-maintained port of **[AWTRIX NG](https://blueforcer.github.io/awtrix-ng/)**
to the Ulanzi TC002's **52 × 16** pixel clock. It runs the upstream apps, renderer, Berry scripting,
HTTP API, MQTT interface and web UI natively on the clock.

![TC002](docs/tc002.jpg)

## Install

On a Linux or macOS computer (WSL on Windows) on the same Wi-Fi as the clock, with `python3`,
`curl`, `unzip` and squashfs-tools installed (`apt install squashfs-tools` or `brew install squashfs`):

```sh
curl -fsSL https://raw.githubusercontent.com/sanderdw/awtrix-ng-tc002/main/install.sh | sh -s -- CLOCK_IP
```

Replace `CLOCK_IP` with your clock's address. The installer reads your clock's application
partition, checks the Ulanzi files the port relies on, builds the firmware image and a recovery
image on your computer, and flashes only after you type `flash`. It takes about three minutes.

- Specific or pre-release version: add `--version TAG` (see [releases](https://github.com/sanderdw/awtrix-ng-tc002/releases)).
- Back to stock: add `--restore`.
- Clock does not come back: hold the knob while powering on to start the stock app.

**Read [Risks](#risks) first.** Details, requirements and recovery: [docs/INSTALL.md](docs/INSTALL.md).

### Try it first

A RAM trial runs AWTRIX for a few minutes without flashing anything, then the clock returns to its
installed app. Download the trial ZIP from a [release](https://github.com/sanderdw/awtrix-ng-tc002/releases)
and follow [docs/TRY-IT.md](docs/TRY-IT.md).

## Supported firmware

| | Status |
|---|---|
| Clock | Ulanzi TC002 |
| Stock app | **1.1.1** validated; 1.0.1 and 1.0.8 reported working |
| MCU firmware | **V1.0.17** validated; V1.0.16 reported working |

The installer checks your clock's Ulanzi files before it flashes. The audio and network libraries
must be a known build; the Ulanzi application is accepted when it is a known build (**verified**) or
provides the functions the port calls (**compatible**). `--allow-unverified` installs with an unknown
audio or network library, leaving the feature that needs it off. If the installer prints a SHA-256
it does not recognise, please report it in an [issue](https://github.com/sanderdw/awtrix-ng-tc002/issues).

## On the TC002

**Controls**

- Turn the knob to move between apps; press it to dismiss notifications, double-press for power.
- Tap **−/+** to change the speaker volume in steps of 5; a volume indicator shows briefly.
- Hold **−/+** to change the brightness.

**Display**

- Clock, date and battery apps have native 52 × 16 layouts. Other apps and notifications use fonts
  and 8 × 8 icons at twice their size; classic 32 × 8 backgrounds are fitted to the panel.
- Icons and GIFs up to 52 × 16 display at their own size. The icon editor offers 16 × 16 and 52 × 16.
- `draw` commands and Berry scripts use physical pixels: `(51,15)` is the bottom-right LED. Scripts
  written for 32 × 8 need their coordinates updated; use `width()` and `height()`.
- Berry scripts share a 1 MiB heap, room for many more scripts than on an ESP32.

**Hardware differences**

- No light, temperature or humidity sensor; brightness is set manually. GPIO settings are hidden.
- Sleep blanks the panel; there is no deep sleep. Reboot in the web UI restarts AWTRIX, not Linux.
- Updates: System → Maintenance accepts an `.img` built by the installer (`--build-only`).
  Settings, scripts and icons live in `/data/awtrix-ng` and survive updates and a restore to stock.

**Known issue**: a dim green (around `#004200`) can flicker on some LEDs. The battery icon uses a
brighter green to avoid it; other content with that colour may still flicker.

## MQTT compatibility

Topics and payloads are exactly upstream's: see the
[AWTRIX NG MQTT reference](https://blueforcer.github.io/awtrix-ng/reference/mqtt/). The default
prefix is the clock's 12-character MAC; change it in System → MQTT.

The knob adds one topic. Each detent publishes `cw` (turning right, next app) or `ccw` to
`<prefix>/state/knob`, not retained, even while navigation is blocked. With Home Assistant
discovery on, the clock gets a **Knob** event entity, so the knob can drive something else:

```yaml
mode: queued
triggers:
  - trigger: state
    entity_id: event.awtrix_ng_knob   # the entity id follows your clock's name
    not_from: [unavailable, unknown]
    not_to: [unavailable, unknown]
actions:
  - action: "media_player.volume_{{ 'up' if trigger.to_state.attributes.event_type == 'cw' else 'down' }}"
    target:
      entity_id: media_player.soundbar
```

Turn on block buttons (`blockNavigation`) if the knob should stop switching apps.

## Risks

- **Installing rewrites the clock's application partition in place**; there is no second copy.
  The images are validated and every written block is read back, but a power cut during the write
  leaves a clock that needs recovery through the stock bootloader or a serial console, which has
  never been tried.
- **Recovery without a computer**: hold the knob at power-on, or let three start-up crashes in a
  row happen; both start the stock Ulanzi app, from which `--restore` works.
- **Releases are pre-releases** until the [validation protocol](docs/VALIDATION.md) has been run
  on a real clock.
- **Ulanzi's closed libraries** handle Wi-Fi, audio and start-up. A future Ulanzi version can
  change them; the installer then turns the affected feature off or refuses to install.

## Security

The web UI and API are unauthenticated unless you enable the login in System → Web. Without it,
anyone on your network can control the clock, upload files and start a firmware update.

## Thank you, Blueforcer ❤️

This port exists thanks to **[Blueforcer (Stephan Mühl)](https://github.com/Blueforcer)**, author
of [AWTRIX NG](https://github.com/Blueforcer/awtrix-ng): the firmware, MQTT interface, scripting
engine and web UI are his work and that of the AWTRIX community. This is an independent adaptation,
not an official Blueforcer or Ulanzi release. If it is useful to you, please
[support the upstream author](https://ko-fi.com/blueforcer).

Want to thank me for the TC002 port itself?

[![Buy me a coffee](https://img.shields.io/badge/Buy%20me%20a%20coffee-f59e0b?style=flat-square&logo=data:image/svg%2Bxml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHZpZXdCb3g9IjAgMCAyNCAyNCI%2BPHBhdGggZmlsbD0id2hpdGUiIGQ9Ik0yLDIxSDIwVjE5SDJNMjAsOEgxOFY1SDIwTTIwLDNINFYxM0E0LDQgMCAwLDAgOCwxN0gxNEE0LDQgMCAwLDAgMTgsMTNWMTBIMjBBMiwyIDAgMCwwIDIyLDhWNUMyMiwzLjg5IDIxLjEsMyAyMCwzWiIvPjwvc3ZnPg%3D%3D)](https://bunq.me/sanderdw)

## Contributing

Bug reports and pull requests are welcome; see [CONTRIBUTING.md](CONTRIBUTING.md).

## License

**PolyForm Noncommercial 1.0.0**, following upstream AWTRIX NG: source-available with
noncommercial terms, not OSI open source. See [LICENSE.md](LICENSE.md) and
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
