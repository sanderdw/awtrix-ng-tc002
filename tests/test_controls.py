"""Exercise TC002 button gestures through the real application event loop."""
import json
import time
import urllib.request

import pytest
from test_platform import app


VOLUMES = ('buzzerVolume', 'mp3Volume', 'radioVolume')


def setup_controls(app, **settings):
    app('/api/v1/settings', {'autoTransition': False, 'transitionDurationMs': 0, 'brightness': 120,
        'buzzerVolume': 50, 'mp3Volume': 60, 'radioVolume': 70, **settings}, 'PATCH')
    app('/api/v1/apps/active', {'name': 'Time', 'fast': True}, 'PUT')


def current(app):
    return app('/api/v1/device')['currentApp']


def screen(app):
    return app('/api/v1/display/screen')['pixels']


def has_speaker(pixels):
    return pixels[6*52+4] == 0xFFFFFF and pixels[2*52+4] == 0 and pixels[2*52+10] == 0xFFFFFF


def wait_screen(app, predicate, timeout=2):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        pixels = screen(app)
        if predicate(pixels):
            return pixels
        time.sleep(.03)
    raise AssertionError('expected display feedback did not appear')


def test_volume_feedback_updates_extends_and_restores_app(app):
    setup_controls(app)
    # A static app makes restoration distinguishable from a changing clock.
    app('/api/v1/apps/pushed/control-test', {'text': 'TEST', 'textColor': '#FF0000'}, 'PUT')
    app('/api/v1/apps/active', {'name': 'control-test', 'fast': True}, 'PUT')
    baseline = wait_screen(app, lambda p: 0xFF0000 in p)
    app('/sim/button/right', {'durationMs': 100}, 'POST')
    first = wait_screen(app, has_speaker)
    assert current(app) == 'control-test'
    time.sleep(.9)
    app('/sim/button/right', {'durationMs': 100}, 'POST')
    second = wait_screen(app, lambda p: has_speaker(p) and p != first)
    assert app('/api/v1/settings')['buzzerVolume'] == 60
    time.sleep(.8)  # past the first tap's timeout, inside the second tap's timeout
    assert screen(app) == second
    wait_screen(app, lambda p: p == baseline)
    assert current(app) == 'control-test'


@pytest.mark.parametrize('button,volume', [('left', 0), ('right', 100)])
def test_volume_feedback_at_limits(app, button, volume):
    setup_controls(app, **dict.fromkeys(VOLUMES, volume))
    app('/sim/button/'+button, {'durationMs': 100}, 'POST')
    pixels = wait_screen(app, has_speaker)
    # A limit press still gives feedback; zero has a mute cross instead of waves.
    assert pixels[2*52+16] == (0 if volume == 0 else 0xFFFFFF)
    assert any(pixels[y*52+x] for y in range(16) for x in range(21, 52))
    assert app('/api/v1/settings')['buzzerVolume'] == volume


def test_volume_feedback_keeps_powered_off_panel_dark(app):
    setup_controls(app)
    app('/api/v1/display', {'power': False}, 'PATCH')
    wait_screen(app, lambda p: not any(p))
    app('/sim/button/right', {'durationMs': 100}, 'POST')
    time.sleep(.3)
    assert app('/api/v1/settings')['buzzerVolume'] == 55
    assert not any(screen(app))


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


def test_knob_detent_is_a_press_and_release_for_script_events(app):
    setup_controls(app)
    script = ('class Knob\n var seen\n def init()\n self.seen = []\n end\n'
              ' def on_button_event(btn, event)\n self.seen.push(btn + ":" + event)\n return true\n end\n'
              ' def draw()\n clear()\n'
              ' if self.seen.find("right:press") != nil pixel(0, 0, rgb(0, 255, 0)) end\n'
              ' if self.seen.find("right:release") != nil pixel(1, 0, rgb(0, 0, 255)) end\n'
              ' end\nend\nreturn Knob()')
    req = urllib.request.Request(app.base_url + '/api/v1/apps/script/knob', script.encode(),
                                 {'Content-Type': 'text/plain'}, method='PUT')
    with urllib.request.urlopen(req) as response:
        assert response.status == 200
    app('/api/v1/apps/active', {'name': 'knob', 'fast': True}, 'PUT')
    wait_screen(app, lambda px: current(app) == 'knob' and not any(px))
    app('/sim/rotary/right', {}, 'POST')
    wait_screen(app, lambda px: px[0] == 0x00FF00 and px[1] == 0x0000FF)
    # The script took the press, so the detent did not also change the app.
    assert current(app) == 'knob'


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
