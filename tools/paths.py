"""Locations shared by tools and tests. The upstream tree is patched into build-upstream/."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = ROOT / 'upstream/awtrix-ng'
PATCHED = ROOT / 'build-upstream'


def patched_upstream():
    """The upstream tree with the TC002 patch series applied (falls back to the vendored copy)."""
    return PATCHED if (PATCHED / 'webui/index.html').exists() else UPSTREAM


def webui():
    return patched_upstream() / 'webui/index.html'
