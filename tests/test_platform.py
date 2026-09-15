"""Exercise Linux transport and persistence through the actual native application."""
import json
import socket
import struct
import subprocess
import time
import urllib.error
import urllib.request
from pathlib import Path
import pytest

ROOT=Path(__file__).parents[1]

def available_port():
    with socket.socket() as s:
        s.bind(('127.0.0.1',0)); return s.getsockname()[1]

@pytest.fixture
def app(tmp_path):
    port=available_port()
    (tmp_path/'device.json').write_text(json.dumps({'webPort':port,'artnet':True,'ntpServer':''}))
    p=subprocess.Popen([ROOT/'build-host/awtrix-tc002','--no-matrix','--data',tmp_path,
        '--webui',ROOT/'upstream/awtrix-ng/webui/index.html','--port',str(port)],
        stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    def api(path,body=None,method='GET',headers=None):
        data=None if body is None else json.dumps(body).encode()
        r=urllib.request.Request(f'http://127.0.0.1:{port}'+path,data,headers or {'Content-Type':'application/json'},method=method)
        with urllib.request.urlopen(r,timeout=3) as response: return json.load(response)
    api.base_url = f'http://127.0.0.1:{port}'
    try:
        for _ in range(100):
            try: api('/api/v1/device'); break
            except OSError: time.sleep(.03)
        else: raise AssertionError('application did not start')
        yield api
    finally:
        p.terminate(); p.wait(timeout=10)

def test_artnet_spans_all_five_universes(app):
    time.sleep(.5)  # allow the boot power animation to finish
    colors=[(i%256,(i//52)*10,230) for i in range(832)]
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as udp:
        for universe,start in enumerate(range(0,832,170)):
            pixels=b''.join(bytes(c) for c in colors[start:start+170])
            packet=b'Art-Net\0'+struct.pack('<H',0x5000)+b'\0\x0e\x01\0'+struct.pack('<H',universe)+struct.pack('>H',len(pixels))+pixels
            udp.sendto(packet,('127.0.0.1',6454))
    expected=[(r<<16)|(g<<8)|b for r,g,b in colors]
    for _ in range(50):
        screen=app('/api/v1/display/screen')
        if screen['pixels']==expected: break
        time.sleep(.03)
    assert screen['pixels']==expected

def test_device_reports_fixed_capabilities(app):
    caps=app('/api/v1/capabilities')
    assert caps['matrix']=={'width':52,'height':16,'fixed':True}
    assert caps['gpio']['soc']=='ssd202d' and caps['gpio']['fixed']

def test_authentication_survives_process_restart(app):
    import base64
    app('/api/v1/system',{'authEnabled':True,'authUser':'tester','authPass':'test-password'},'PUT')
    authorization='Basic '+base64.b64encode(b'tester:test-password').decode()
    time.sleep(1)
    for _ in range(50):
        try:
            app('/api/v1/device')
        except urllib.error.HTTPError as error:
            assert error.code==401
            break
        except OSError:
            pass
        time.sleep(.05)
    else: raise AssertionError('authentication was not applied')
    assert app('/api/v1/device',headers={'Authorization':authorization})['boardType']=='tc002'
