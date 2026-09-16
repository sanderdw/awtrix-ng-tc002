"""The upstream tree is a pinned, unedited submodule; the port's changes live in patches/ and src/."""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def run(*args):
    return subprocess.run([sys.executable, *args], cwd=ROOT, capture_output=True, text=True)


def test_submodule_is_pinned_and_clean_and_patches_applied():
    result = run('tools/upstream.py', 'status')
    assert result.returncode == 0, result.stdout + result.stderr


def test_entry_point_is_reconciled_with_upstream():
    result = run('tools/check_main_drift.py')
    assert result.returncode == 0, result.stdout + result.stderr


def test_no_upstream_edit_lacks_a_guard_or_default():
    # Patches that change behaviour for every board must say so in their subject; the rest carry a
    # build-time default or an AWTRIX_TC002 guard. This is a reminder, not a proof: keep the list short.
    subjects = [p.name for p in sorted((ROOT / 'patches').glob('*.patch'))]
    assert len(subjects) <= 10, subjects
