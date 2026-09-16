# How this port is built on upstream

Three layers, strictly separated:

| Layer | Where | Rule |
|---|---|---|
| Upstream AWTRIX NG | `upstream/awtrix-ng` (git submodule) | Pinned to one upstream commit. Never edited. |
| Patch series | `patches/NNNN-*.patch` | Small, ordered `git format-patch` commits on top of the pin. Each is either a hook upstream could take as-is (with the upstream default unchanged) or guarded by `AWTRIX_TC002` / a build-time macro. |
| Port | `src/tc002`, `src/loader`, `src/updater`, `tools`, `tests`, `docs` | Everything TC002-specific. Compiles against the patched tree, never against the submodule directly. |

`tools/upstream.py apply` clones the submodule at its pin into `build-upstream/` and applies the
series with `git am -3`; CMake runs it at configure time, and `tools/webui_brand.py` then writes the
branded `build-webui/index.html`. Neither generated directory is committed.

## The patches

| Patch | Kind | Purpose |
|---|---|---|
| 0001 panel size macros | upstream candidate | `AWTRIX_MATRIX_HEIGHT`, `AWTRIX_PANEL_WIDTH`, `AWTRIX_GIF_MAX_W/H`; defaults unchanged |
| 0002 icon size reporting | upstream candidate | `icon::draw` reports the decoded JPEG size |
| 0003 server extension | upstream candidate | `SimHttpExtension` so a host port adds auth, gates and routes without patching |
| 0004 MP3 routes | upstream candidate | simulator parity with the device's `/api/v1/audio/mp3` |
| 0005 16-row scaling | `AWTRIX_TC002` | scaled fonts, decorations, indicators and page icons; inert at scale 1 |
| 0006 script icon tiles | build-time macro | `AWTRIX_SCRIPT_ICON_W/H`, default 8x8 |
| 0007 fixed hardware config | `AWTRIX_TC002` | refuse wiring/matrix changes with 422 |
| 0008 fixed-matrix web UI | upstream candidate | UI follows `capabilities.matrix` |

Branding (title, repository link, TC002 help texts, licence footer) is not a patch: `tools/webui_brand.py`
applies it with anchored replacements and fails the build if an anchor moved.

## Editing upstream code

Work inside `build-upstream/` as ordinary git commits on top of the pin, then
`uv run tools/upstream.py export` to regenerate `patches/`. CI checks that `apply` succeeds, that the
submodule is clean, and that `export` is idempotent.

## Bumping upstream

```sh
uv run tools/upstream.py rebase v1.1.1      # moves the submodule, re-applies the series
# resolve conflicts inside build-upstream/ with `git am --continue`
uv run tools/upstream.py export
uv run tools/check_main_drift.py            # then carry upstream's main_sim.cpp changes into src/tc002/main_tc002.cpp
```

`src/tc002/main_tc002.cpp` is a fork of upstream's `src/sim/main_sim.cpp`; its `reconciled-with:` marker
names the upstream commit it was last compared against, and `tests/test_upstream_tracking.py` fails
when upstream's file has moved past it.
