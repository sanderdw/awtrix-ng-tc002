"""Compare actual native built-ins pixel-for-pixel with user-approved browser mockups."""
import json
from pathlib import Path
import subprocess

import pytest

ROOT=Path(__file__).resolve().parents[1]
FIXTURES=json.loads((ROOT/'tests/fixtures/approved-layouts.json').read_text())

@pytest.mark.parametrize('case',FIXTURES,ids=lambda c:'-'.join(map(str,c['args'])))
def test_approved_layout(case):
    result=subprocess.check_output([ROOT/'build-host/test-layout-render',*map(str,case['args'])])
    actual=json.loads(result)
    differences=[(i%52,i//52,got,want) for i,(got,want) in enumerate(zip(actual,case['pixels'])) if got!=want]
    assert len(actual)==832
    assert not differences, differences[:12]
