"""Golden screens and API shapes captured from the real host build.

These pin behaviour across the restructure of the source tree. Regenerate only
when a change is intended: UPDATE_GOLDENS=1 uv run pytest tests/test_goldens.py
"""
import base64
import hashlib
import json
import os
import sys
import time
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from paths import webui  # noqa: E402
from test_platform import app  # noqa: E402,F401
from test_scaling import gif  # noqa: E402

GOLDEN = ROOT / 'tests/fixtures/screens-baseline.json'
UPDATE = os.environ.get('UPDATE_GOLDENS') == '1'
VOLATILE = {'uptimeSeconds', 'freeHeapBytes', 'minFreeHeapBytes', 'largestFreeBlockBytes',
            'ipAddress', 'uid', 'hostname', 'wifiRssi', 'fps', 'batteryPercent', 'batteryVoltage',
            'batteryPinMillivolts', 'lowBattery', 'wifi', 'mqtt', 'currentApp', 'resetReason',
            'lastRefusal', 'timeSynced', 'time', 'timezone', 'logs'}

ICON16 = gif(16, 16, [[1 if (x + y) % 3 else 2 for y in range(16) for x in range(16)]])
ICON8 = gif(8, 8, [[3] * 64])
FULL = gif(52, 16, [[1 + (x // 13 + y // 8) % 3 for y in range(16) for x in range(52)]])

SCENES = {
    'icon-text-progress': dict(icon=ICON16, text='Hi', progress=60, progressColor='#0000FF'),
    'centered-large-text': dict(text='12:34', font='large'),
    'small-icon-long-word': dict(icon=ICON8, text='Wo', textCenter=False),
    'bars-with-icon': dict(icon=ICON16, barChart=[1, 5, 3, 8, 2, 7, 4]),
    'bars-dense': dict(barChart=list(range(1, 17))),
    'line-chart': dict(lineChart=[3, 1, 4, 1, 5, 9, 2, 6, 5, 3, 5]),
    'colored-fragments': dict(text=[{'text': 'R', 'color': '#FF0000'}, {'text': 'G', 'color': '#00FF00'},
                                    {'text': 'B', 'color': '#0000FF'}]),
    'full-screen-icon': dict(icon=FULL),
    'text-in-front-of-bars': dict(text='X', barChart=[9, 9, 9, 9, 9, 9, 9, 9, 9, 9], textInFront=True),
}


def stable_screen(app, timeout=4):
    """Wait until three consecutive reads agree and the panel is not blank."""
    deadline = time.monotonic() + timeout
    history = []
    while time.monotonic() < deadline:
        pixels = app('/api/v1/display/screen')['pixels']
        history = (history + [pixels])[-3:]
        if len(history) == 3 and history[0] == history[1] == history[2] and any(pixels):
            return pixels
        time.sleep(.04)
    raise AssertionError('screen did not settle')


def capture(app):
    time.sleep(.6)  # boot power animation
    app('/api/v1/settings', {'autoTransition': False, 'transitionDurationMs': 0, 'brightness': 255,
        'uppercase': False}, 'PATCH')
    screens = {}
    for name, payload in SCENES.items():
        app('/api/v1/notifications', {'text': '', 'hold': True, 'stack': False, **payload}, 'POST')
        screens[name] = stable_screen(app)
        app('/api/v1/notifications/active', method='DELETE')
        time.sleep(.1)
    for slot, color in ((1, '#FF0000'), (2, '#00FF00'), (3, '#0000FF')):
        app(f'/api/v1/indicators/{slot}', {'on': True, 'color': color}, 'PUT')
    app('/api/v1/notifications', {'text': '', 'hold': True, 'stack': False, 'icon': ICON16, 'text': 'ind'}, 'POST')
    screens['indicators'] = stable_screen(app)
    device = {k: v for k, v in app('/api/v1/device').items() if k not in VOLATILE}
    system = {k: v for k, v in app('/api/v1/system').items()
              if k not in {'hostname', 'authPass', 'mqttPass', 'wifiPass', 'wifiSsid', 'uid', 'webPort'}}
    return {'screens': screens, 'capabilities': app('/api/v1/capabilities'),
            'version': app('/api/v1/version'), 'device': device, 'system': system,
            'webuiSha256': hashlib.sha256(webui().read_bytes()).hexdigest()}


def test_goldens(app):
    actual = capture(app)
    if UPDATE or not GOLDEN.exists():
        GOLDEN.write_text(json.dumps(actual, indent=1) + '\n')
        pytest.skip('goldens recorded')
    expected = json.loads(GOLDEN.read_text())
    for name, pixels in expected['screens'].items():
        got = actual['screens'][name]
        diff = [(i % 52, i // 52, hex(g), hex(w)) for i, (g, w) in enumerate(zip(got, pixels)) if g != w]
        assert not diff, (name, diff[:10], len(diff))
    for key in ('capabilities', 'version', 'device', 'system'):
        assert actual[key] == expected[key], key
    assert actual['webuiSha256'] == expected['webuiSha256']
