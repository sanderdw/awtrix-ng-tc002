"""Exercise TC002 button gestures through the real application event loop."""
import json
import time

import pytest
from test_platform import app


VOLUMES = ('buzzerVolume', 'mp3Volume', 'radioVolume')


def setup_controls(app, **settings):
    app('/api/v1/settings', {'autoTransition': False, 'transitionDurationMs': 0, 'brightness': 120,
        'buzzerVolume': 50, 'mp3Volume': 60, 'radioVolume': 70, **settings}, 'PATCH')
    app('/api/v1/apps/active', {'name': 'Time', 'fast': True}, 'PUT')


def current(app):
    return app('/api/v1/device')['currentApp']


@pytest.mark.parametrize('button,delta', [('left', -5), ('right', 5)])
def test_tap_changes_volume_only_on_release(app, button, delta):
    setup_controls(app)
    app('/sim/button/'+button, {'durationMs': 300}, 'POST')
    time.sleep(.15)
    assert app('/api/v1/settings')['buzzerVolume'] == 50
    time.sleep(.3)
    settings = app('/api/v1/settings')
    assert [settings[k] for k in VOLUMES] == [50+delta, 60+delta, 70+delta]
    assert settings['brightness'] == 120
    assert current(app) == 'Time'


@pytest.mark.parametrize('button,direction', [('left', -1), ('right', 1)])
def test_hold_repeats_brightness_without_volume_or_navigation(app, button, direction):
    setup_controls(app)
    app('/sim/button/'+button, {'durationMs': 1300}, 'POST')
    time.sleep(.55)
    assert app('/api/v1/settings')['brightness'] == 120
    time.sleep(.95)
    settings = app('/api/v1/settings')
    assert (settings['brightness']-120)*direction >= 20
    assert [settings[k] for k in VOLUMES] == [50, 60, 70]
    assert current(app) == 'Time'
    time.sleep(.3)
    assert app('/api/v1/settings')['brightness'] == settings['brightness']


@pytest.mark.parametrize('button,volume,brightness,expected_volume,expected_brightness', [
    ('left', 2, 5, 0, 1), ('right', 98, 250, 100, 255)])
def test_controls_clamp_at_limits(app, button, volume, brightness, expected_volume, expected_brightness):
    setup_controls(app, brightness=brightness, **dict.fromkeys(VOLUMES, volume))
    app('/sim/button/'+button, {'durationMs': 100}, 'POST')
    time.sleep(.25)
    assert all(app('/api/v1/settings')[k] == expected_volume for k in VOLUMES)
    app('/sim/button/'+button, {'durationMs': 1000}, 'POST')
    time.sleep(1.2)
    assert app('/api/v1/settings')['brightness'] == expected_brightness


def test_knob_navigation_is_separate_from_buttons(app):
    setup_controls(app)
    app('/sim/rotary/right', {}, 'POST')
    time.sleep(.15)
    assert current(app) != 'Time'
    app('/sim/rotary/left', {}, 'POST')
    time.sleep(.15)
    assert current(app) == 'Time'
    settings = app('/api/v1/settings')
    assert settings['brightness'] == 120
    assert [settings[k] for k in VOLUMES] == [50, 60, 70]


def test_navigation_lock_keeps_volume_controls_available(app):
    setup_controls(app, blockNavigation=True)
    app('/sim/rotary/right', {}, 'POST')
    app('/sim/button/right', {'durationMs': 100}, 'POST')
    time.sleep(.3)
    assert current(app) == 'Time'
    assert app('/api/v1/settings')['buzzerVolume'] == 55


def test_button_settings_are_saved(app, tmp_path):
    setup_controls(app)
    app('/sim/button/right', {'durationMs': 100}, 'POST')
    time.sleep(.25)
    app('/sim/button/left', {'durationMs': 1000}, 'POST')
    time.sleep(1.2)
    expected = app('/api/v1/settings')
    deadline = time.monotonic()+3
    while time.monotonic() < deadline:
        try:
            saved = json.loads((tmp_path/'settings.json').read_text())
            if all(saved[k] == expected[k] for k in (*VOLUMES, 'brightness')):
                return
        except (FileNotFoundError, json.JSONDecodeError):
            pass
        time.sleep(.05)
    raise AssertionError('button volume and brightness settings were not persisted')
