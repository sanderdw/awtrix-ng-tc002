"""Locations shared by tools and tests."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = ROOT / 'upstream/awtrix-ng'          # pinned submodule, never edited
PATCHED = ROOT / 'build-upstream'               # upstream + patches/, made by tools/upstream.py
WEBUI = ROOT / 'build-webui/index.html'         # patched UI + branding, made by tools/webui_brand.py


def webui():
    if not WEBUI.exists():
        raise SystemExit('build-webui/index.html is missing; configure the build or run tools/webui_brand.py')
    return WEBUI
