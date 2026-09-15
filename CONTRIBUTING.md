# Contributing

Please open TC002 issues and pull requests at
[sanderdw/awtrix-ng-tc002](https://github.com/sanderdw/awtrix-ng-tc002).
Include the port version, stock app/MCU version, steps to reproduce, and expected
and actual behavior. Remove credentials and personal information from logs.

## Source layout

- `src/`: TC002 hardware, audio, networking, launcher and update adapters.
- `upstream/awtrix-ng/`: vendored AWTRIX NG core and web UI, with TC002 changes.
  The base revision is recorded in `THIRD-PARTY-NOTICES.md`; this is not a submodule.
- `vendor/` and `assets/`: dependencies, licenses and public CA certificates.
- `tools/`: build, image packaging, RAM trials and device checks.
- `tests/`: host integration, hardware packing, image validation and pixel fixtures.

## Local checks

On Linux, install a C/C++ compiler and OpenSSL development headers. Use `uv` for
Python tooling, including tests:

```sh
uv sync --locked
uv run cmake -S . -B build-host -G Ninja -DCMAKE_BUILD_TYPE=Release
uv run cmake --build build-host -j4
uv run ctest --test-dir build-host --output-on-failure
uv run pytest -q
```

The default suite runs locally without a clock or vendor firmware. MQTT tests
start a loopback broker. Leave `AWTRIX_DEVICE_SERIAL` unset for host tests.
See the README for cross-compilation and optional physical validation.

Preserve the upstream MQTT topic paths and payload contract. Rendering changes
should handle all 832 pixels and keep explicit drawing coordinates physical.
Native clock, date and battery fixtures record the approved 52 × 16 layouts;
review visible changes before replacing those fixtures.

Keep device backups, generated firmware images, credentials and personal test
artifacts out of commits. `tools/package_release.py` creates a personal firmware
package containing the owner's stock files; that ZIP is not a public release asset.

Contributions to the application and port use the existing PolyForm Noncommercial
1.0.0 terms. Preserve Blueforcer's required notice and all third-party notices.
