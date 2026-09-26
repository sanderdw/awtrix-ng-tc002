# Contributing

Please open TC002 issues and pull requests at
[sanderdw/awtrix-ng-tc002](https://github.com/sanderdw/awtrix-ng-tc002). Include the port version,
stock app and MCU versions, steps to reproduce, and expected and actual behaviour. Remove
credentials and personal information from logs.

Building, testing and releasing: [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md). How the port sits on
upstream: [docs/PORTING.md](docs/PORTING.md).

Ground rules:

- Upstream code changes only through `patches/`, each either keeping upstream's default behaviour
  or guarded by `AWTRIX_TC002`. Propose a hook upstream rather than an unguarded edit.
- Keep the upstream MQTT topics and payloads unchanged.
- Rendering handles all 832 pixels and keeps explicit drawing coordinates physical. The native
  clock, date and battery fixtures are the approved layouts; review visible changes before
  replacing them.
- Keep device backups, firmware images, vendor files and credentials out of commits.
- Contributions use the PolyForm Noncommercial 1.0.0 terms. Preserve Blueforcer's Required Notice
  and all third-party notices.
