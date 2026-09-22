"""Pixel-level regression checks against the real 52x16 renderer and decoder."""
import base64
import struct
import time
import urllib.request

import pytest
from test_platform import app


def gif(w, h, frames):
    """Tiny independent GIF encoder, fixed-width LZW with a clear per pixel."""
    data = bytearray(b'GIF89a' + struct.pack('<HH', w, h) + b'\x81\0\0')
    data += bytes.fromhex('000000 ff0000 00ff00 0000ff')
    for pixels in frames:
        data += b'\x21\xf9\x04\x04\x0a\0\0\0'
        data += b'\x2c' + struct.pack('<HHHH', 0, 0, w, h) + b'\0\x02'
        codes = [code for pixel in pixels for code in (4, pixel)] + [5]
        packed = sum(code << (3*i) for i, code in enumerate(codes))
        encoded = packed.to_bytes((len(codes)*3+7)//8, 'little')
        for start in range(0, len(encoded), 255):
            chunk = encoded[start:start+255]
            data += bytes([len(chunk)]) + chunk
        data += b'\0'
    return base64.b64encode(data + b'\x3b').decode()


def screen_until(app, predicate, timeout=3):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        pixels = app('/api/v1/display/screen')['pixels']
        if predicate(pixels):
            return pixels
        time.sleep(.025)
    raise AssertionError('expected display pixels not rendered')


def notify(app, **payload):
    app('/api/v1/notifications', {'text': '', 'hold': True, 'stack': False, **payload}, 'POST')


@pytest.mark.parametrize('w,h', [(8,8), (16,16), (24,16), (32,8), (52,16)])
def test_icon_dimensions_and_last_pixel(app, w, h):
    source = [1]*(w*h)
    source[-1] = 2
    notify(app, icon=gif(w,h,[source]))
    dw,dh = (min(52,w*2),h*2) if h<=8 else (w,h)
    expected = [0]*832
    palette = [0,0xff0000,0x00ff00,0x0000ff]
    for y in range(dh):
        for x in range(dw):
            expected[y*52+x] = palette[source[(y*h//dh)*w+x*w//dw]]
    screen_until(app, lambda px: px==expected)


def test_full_screen_animation_reaches_bottom_right(app):
    notify(app, icon=gif(52,16,[[1]*832,[3]*832]))
    screen_until(app, lambda px: px==[0xff0000]*832)
    screen_until(app, lambda px: px==[0x0000ff]*832)


@pytest.mark.parametrize('font', ['small','large'])
def test_notification_font_has_double_height_and_centering(app, font):
    notify(app, text='H', font=font)
    px=screen_until(app, lambda px: any(px[8*52:]))
    lit=[(i%52,i//52) for i,c in enumerate(px) if c]
    assert max(y for x,y in lit)-min(y for x,y in lit)+1 >= 10
    assert abs(min(x for x,y in lit)+max(x for x,y in lit)-51)<=1
    for y in range(0,16,2):
        assert px[y*52:(y+1)*52]==px[(y+1)*52:(y+2)*52]


def test_icon_text_spacing_and_progress(app):
    notify(app, icon=gif(16,16,[[1]*256]), text='H', textCenter=False,
           progress=100, progressColor='#0000FF')
    px=screen_until(app, lambda px: px[15*52+51]==0x0000ff)
    for y in range(16):
        assert px[y*52:y*52+16]==[0xff0000]*16
    # The gap stays clear of text; since upstream 1.1.2 the progress bar runs under it.
    for y in range(14):
        assert px[y*52+16:y*52+18]==[0,0]
    white=[i%52 for i,c in enumerate(px) if c==0xffffff]
    assert white and min(white)>=18
    assert px[14*52+16:15*52]==[0x0000ff]*36
    assert px[15*52+16:16*52]==[0x0000ff]*36


def test_icon_gap_scales_with_the_text(app):
    notify(app, icon=gif(16,16,[[1]*256]), text='H', textCenter=False, iconGap=3)
    px=screen_until(app, lambda px: 0xffffff in px)
    assert min(i%52 for i,c in enumerate(px) if c==0xffffff)==16+3*2
    for y in range(16):
        assert px[y*52+16:y*52+22]==[0]*6


def test_placed_icons_keep_physical_coordinates(app):
    # Native 16-row art is not rescaled; a classic tile doubles like the main icon.
    notify(app, icons=[{'icon':gif(4,16,[[2]*64]),'x':40,'y':0},
                       {'icon':gif(2,2,[[3]*4]),'x':10,'y':6}])
    px=screen_until(app, lambda px: px[40]==0x00ff00 and px[6*52+10]==0x0000ff)
    for y in range(16):
        assert px[y*52+40:y*52+44]==[0x00ff00]*4
    for y in range(6,10):
        assert px[y*52+10:y*52+14]==[0x0000ff]*4
    assert sum(1 for c in px if c)==4*16+4*4


def test_explicit_drawing_keeps_physical_coordinates(app):
    notify(app, draw=[['pixel',51,15,'#00FF00'],['pixel',31,7,'#FF0000']])
    expected=[0]*832;expected[-1]=0x00ff00;expected[7*52+31]=0xff0000
    screen_until(app,lambda px:px==expected)


@pytest.mark.parametrize('mode', range(7))
def test_all_clock_layouts_use_both_halves(app, mode):
    app('/api/v1/settings',{'autoTransition':False,'timeMode':mode},'PATCH')
    app('/api/v1/apps/active',{'name':'Time','fast':True},'PUT')
    px=screen_until(app,lambda px:any(px[:8*52]) and any(px[8*52:]))
    for y in range(0,16,2):
        assert px[y*52:(y+1)*52]==px[(y+1)*52:(y+2)*52]


@pytest.mark.parametrize('name',['Date'])
def test_other_builtin_layouts_have_double_height(app,name):
    app('/api/v1/settings',{'autoTransition':False},'PATCH')
    app('/api/v1/apps/active',{'name':name,'fast':True},'PUT')
    px=screen_until(app,lambda px:any(px[:8*52]) and any(px[8*52:]))
    for y in range(0,16,2):
        assert px[y*52:(y+1)*52]==px[(y+1)*52:(y+2)*52]


def test_bar_chart_fills_available_width(app):
    notify(app, barChart=[8,8,8], chartColor='#00FF00')
    screen_until(app,lambda px:px[0]==0x00ff00 and px[51]==0x00ff00 and px[831]==0x00ff00)


def test_dense_chart_keeps_last_sample_visible_after_icon(app):
    notify(app, icon=gif(44,16,[[1]*704]), barChart=[0]*15+[8], chartColor='#00FF00')
    screen_until(app,lambda px:px[51]==0x00ff00 and px[831]==0x00ff00)


def test_script_icon_keeps_native_dimensions_and_coordinates(app,tmp_path):
    (tmp_path/'ICONS').mkdir(exist_ok=True)
    source=[1]*256;source[-1]=2
    (tmp_path/'ICONS/native.gif').write_bytes(base64.b64decode(gif(16,16,[source])))
    script='class Native\n def draw()\n clear()\n icon("native",36,0)\n end\nend\nreturn Native()'
    req=urllib.request.Request(app.base_url+'/api/v1/apps/script/native',script.encode(),
                               {'Content-Type':'text/plain'},method='PUT')
    with urllib.request.urlopen(req) as response:
        assert response.status==200
    app('/api/v1/settings',{'autoTransition':False},'PATCH')
    app('/api/v1/apps/active',{'name':'native','fast':True},'PUT')
    expected=[0]*832
    for y in range(16):
        expected[y*52+36:y*52+52]=[0xff0000]*16
    expected[-1]=0x00ff00
    screen_until(app,lambda px:px==expected)


def test_pushed_unicode_text_uses_large_metrics_and_scrolls(app):
    app('/api/v1/apps/pushed/scaled', {'text':'ÉÉÉÉÉÉÉÉÉÉÉÉÉÉÉÉ',
        'scroll':{'holdMs':0,'speed':200}}, 'PUT')
    app('/api/v1/settings',{'autoTransition':False},'PATCH')
    app('/api/v1/apps/active',{'name':'scaled','fast':True},'PUT')
    px=screen_until(app,lambda px:any(px[8*52:]))
    screen_until(app,lambda other:other!=px and any(other[8*52:]))


def test_indicators_are_scaled_at_physical_corners(app):
    notify(app)
    app('/api/v1/indicators/1',{'color':'#FF0000'},'PUT')
    px=screen_until(app,lambda px:px[0*52+51]==0xff0000)
    for y in range(2):
        assert px[y*52+48:y*52+52]==[0xff0000]*4
    for y in range(2,4):
        assert px[y*52+50:y*52+52]==[0xff0000]*2
