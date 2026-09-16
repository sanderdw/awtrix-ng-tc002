"""Static checks on the shipped web UI that need no clock or browser."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
UI = (ROOT / 'upstream/awtrix-ng/webui/index.html').read_text(encoding='utf-8')


def test_upstream_support_button_is_present():
    # The Ko-fi button belongs to the upstream author. The port must never remove it.
    assert UI.count('id="kofibtn"') == 1
    assert "$('#kofibtn').addEventListener('click',()=>window.open('https://ko-fi.com/blueforcer'" in UI
    assert '<symbol id="i-kofi"' in UI


def test_required_notice_and_licence_are_shown():
    # PolyForm Noncommercial 1.0.0 obliges distributors to pass on the Required Notice.
    assert 'Stephan M&uuml;hl (Blueforcer)' in UI
    assert 'https://github.com/Blueforcer/awtrix-ng' in UI
    assert 'polyformproject.org/licenses/noncommercial/1.0.0' in UI


def test_port_link_comes_after_the_support_button():
    assert UI.index('id="kofibtn"') < UI.index('id="github-link"')
