"""Locations shared by tools and tests."""
import os
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = ROOT / 'upstream/awtrix-ng'          # pinned submodule, never edited
PATCHED = ROOT / 'build-upstream'               # upstream + patches/, made by tools/upstream.py
WEBUI = ROOT / 'build-webui/index.html'         # patched UI + branding, made by tools/webui_brand.py


def webui():
    if not WEBUI.exists():
        raise SystemExit('build-webui/index.html is missing; configure the build or run tools/webui_brand.py')
    return WEBUI


def adb():
    """Google's platform-tools adb: $ADB, then PATH, then build-deps/platform-tools (tools/fetch_platform_tools.sh)."""
    candidate = os.environ.get('ADB') or shutil.which('adb') or str(ROOT / 'build-deps/platform-tools/adb')
    if not os.access(candidate, os.X_OK):
        raise SystemExit('adb not found: run tools/fetch_platform_tools.sh or set ADB to Google platform-tools adb')
    return candidate
