"""Exercise Linux transport and persistence through the actual native application."""
import json
import socket
import struct
import subprocess
import time
import urllib.error
import urllib.request
from pathlib import Path
import sys
import pytest

ROOT=Path(__file__).parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from paths import webui

def available_port():
    with socket.socket() as s:
        s.bind(('127.0.0.1',0)); return s.getsockname()[1]

@pytest.fixture
def app(tmp_path):
    port=available_port()
    (tmp_path/'device.json').write_text(json.dumps({'webPort':port,'artnet':True,'ntpServer':''}))
    p=subprocess.Popen([ROOT/'build-host/awtrix-tc002','--no-matrix','--data',tmp_path,
        '--webui',webui(),'--port',str(port)],
        stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    def api(path,body=None,method='GET',headers=None):
        data=None if body is None else json.dumps(body).encode()
        r=urllib.request.Request(f'http://127.0.0.1:{port}'+path,data,headers or {'Content-Type':'application/json'},method=method)
        with urllib.request.urlopen(r,timeout=3) as response: return json.load(response)
    api.base_url = f'http://127.0.0.1:{port}'
    api.port = port
    api.pid = p.pid
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

def upload_mp3(app, name, content, query=''):
    boundary = 'awtrix-mp3-test'
    body = (f'--{boundary}\r\nContent-Disposition: form-data; name="file"; '
            f'filename="{name}"\r\nContent-Type: audio/mpeg\r\n\r\n').encode()
    body += content + f'\r\n--{boundary}--\r\n'.encode()
    request = urllib.request.Request(app.base_url + '/api/v1/audio/mp3' + query, body,
        {'Content-Type': f'multipart/form-data; boundary={boundary}'}, method='POST')
    with urllib.request.urlopen(request, timeout=3) as response:
        return json.load(response)

def test_audio_mp3_routes(app):
    assert app('/api/v1/audio')['stations'] == []
    assert app('/api/v1/audio/melodies')['melodies'] == []
    assert app('/api/v1/audio/mp3')['files'] == []
    content = b'ID3' + bytes(20)
    assert upload_mp3(app, 'door-bell_1.mp3', content, '?dir=/ICONS')['ok']
    listing = app('/api/v1/audio/mp3?dir=/ICONS')
    assert listing['files'] == [{'name': 'door-bell_1.mp3', 'size': len(content)}]
    assert listing['usedBytes'] >= len(content)
    with urllib.request.urlopen(app.base_url + '/MP3/door-bell_1.mp3') as response:
        assert response.read() == content
        assert response.headers['Content-Type'] == 'audio/mpeg'
    assert app('/api/v1/files?dir=/ICONS')['files'] == []
    assert app('/api/v1/audio/mp3/door-bell_1', method='DELETE')['ok']
    assert app('/api/v1/audio/mp3')['files'] == []
    with pytest.raises(urllib.error.HTTPError) as error:
        app('/api/v1/audio/mp3/door-bell_1', method='DELETE')
    assert error.value.code == 404

@pytest.mark.parametrize('name,content,status', [
    ('bad name.mp3', b'ID3test', 400),
    ('../escape.mp3', b'ID3test', 400),
    ('/ICONS/escape.mp3', b'ID3test', 400),
    ('tone.wav', b'ID3test', 400),
    ('tone.mp3', b'not audio', 415),
])
def test_audio_rejects_invalid_uploads(app, name, content, status):
    with pytest.raises(urllib.error.HTTPError) as error:
        upload_mp3(app, name, content)
    assert error.value.code == status
    assert app('/api/v1/audio/mp3')['files'] == []

@pytest.mark.parametrize('path,method,status', [
    ('/api/v1/audio/mp3', 'DELETE', 405),
    ('/api/v1/audio/mp3/tone', 'GET', 405),
    ('/api/v1/audio/mp3/invalid.name', 'DELETE', 400),
    ('/api/v1/audio/mp3', 'POST', 400),
])
def test_audio_mp3_rejects_invalid_requests(app, path, method, status):
    with pytest.raises(urllib.error.HTTPError) as error:
        app(path, method=method)
    assert error.value.code == status

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


def raw_status(app, method, path, headers, body=b''):
    """Send a request by hand and return the status code, without ever sending a full body."""
    with socket.create_connection(('127.0.0.1', app.port), timeout=5) as s:
        head = f'{method} {path} HTTP/1.1\r\nHost: clock\r\n' + ''.join(f'{k}: {v}\r\n' for k, v in headers.items()) + '\r\n'
        s.sendall(head.encode() + body)
        return int(s.recv(4096).split(b' ')[1])


def rss_kib(pid):
    with open(f'/proc/{pid}/status') as status:
        return next(int(line.split()[1]) for line in status if line.startswith('VmRSS:'))


def test_oversized_bodies_are_rejected_before_they_are_read(app):
    headers = {'Content-Type': 'application/json', 'Content-Length': str(9 * 1024 * 1024)}
    assert raw_status(app, 'POST', '/api/v1/notifications', headers) == 413
    headers['Content-Length'] = '200000'   # over the 64 KiB cap for JSON routes
    assert raw_status(app, 'POST', '/api/v1/notifications', headers) == 413
    assert raw_status(app, 'POST', '/api/v1/notifications',
                      {'Content-Type': 'application/json', 'Transfer-Encoding': 'chunked'}) == 411
    assert app('/api/v1/notifications', {'text': 'still fine'}, 'POST')['ok']


def test_large_mp3_upload_streams_to_disk_within_memory_budget(app):
    import os
    content = b'ID3' + os.urandom(6 * 1024 * 1024)
    before = rss_kib(app.pid)
    assert upload_mp3(app, 'long-track.mp3', content)['ok']
    after = rss_kib(app.pid)
    assert after - before < 2048, f'RSS grew by {after - before} KiB during a 6 MiB upload'
    assert app('/api/v1/audio/mp3')['files'] == [{'name': 'long-track.mp3', 'size': len(content)}]


def test_a_second_large_upload_is_refused_while_one_is_in_flight(app):
    import threading
    boundary = 'awtrix-slow-upload'
    body = (f'--{boundary}\r\nContent-Disposition: form-data; name="file"; filename="slow.mp3"\r\n'
            f'Content-Type: audio/mpeg\r\n\r\n').encode() + b'ID3' + bytes(300000) + f'\r\n--{boundary}--\r\n'.encode()
    head = (f'POST /api/v1/audio/mp3 HTTP/1.1\r\nHost: clock\r\nContent-Type: multipart/form-data; boundary={boundary}\r\n'
            f'Content-Length: {len(body)}\r\n\r\n').encode()
    with socket.create_connection(('127.0.0.1', app.port), timeout=10) as slow:
        slow.sendall(head + body[:2000])   # the slow client holds the upload slot with a partial body
        time.sleep(0.4)
        with pytest.raises(urllib.error.HTTPError) as refused:
            upload_mp3(app, 'quick.mp3', b'ID3' + bytes(64))
        assert refused.value.code == 409
        slow.sendall(body[2000:])
        assert int(slow.recv(4096).split(b' ')[1]) == 200
    assert {f['name'] for f in app('/api/v1/audio/mp3')['files']} == {'slow.mp3'}


def test_vendor_status_lists_the_files_the_port_depends_on(app):
    status = app('/api/v1/tc002/vendor')
    # The stock build the hashes were recorded from, not the clock's own version.
    assert status['reference'] == {'app': '1.1.1', 'mcu': 'V1.0.17'} and 'stock' not in status
    assert set(status['libraries']) >= {'libulanzi-bootstrap.so', 'libmi_ao.so', 'libzknet.so'}
    # Without hardware nothing is loaded, so nothing can be trusted.
    assert not any(lib['trusted'] for lib in status['libraries'].values())
    assert {lib['status'] for lib in status['libraries'].values()} == {'unchecked'}
    # Only the vendor application can be accepted by the entry points the launcher calls.
    app = status['libraries']['libulanzi-bootstrap.so']
    assert app['requiredSymbols'] == ['onEasyUIInit', 'onEasyUIDeinit', 'onStartupApp', '_ZN4base13wifiOnAndWaitEi']
    assert app['missingSymbols'] == []
    assert 'requiredSymbols' not in status['libraries']['libmi_ao.so']


def fat_script():
    # Upstream's fatApp() from test_scripthost: about 9 KB of Berry heap per installed copy.
    body = ''.join(f'def m{i}(a, b)\n  var t = a * {i} + b\n  var u = "literal {i} padding"\n  return t + size(u)\nend\n'
                   for i in range(40))
    return ('class App\n' + body + 'def draw() end\nend\nreturn App()').encode()


def test_scripts_share_a_one_mebibyte_heap(app):
    # Issue #9: with the ESP32's 96 KB the ninth of these scripts was refused, on a clock with
    # megabytes free. The budget still holds; it is just sized for the clock.
    device = app('/api/v1/device')
    assert (device['scriptHeapPool'], device['scriptHeapBudgetBytes']) == ('internal', 1024 * 1024)
    installed, refusal = 0, None
    for i in range(200):
        request = urllib.request.Request(f'{app.base_url}/api/v1/apps/script/fat{i}', data=fat_script(),
                                         method='PUT', headers={'Content-Type': 'text/plain'})
        try:
            with urllib.request.urlopen(request, timeout=10) as response:
                response.read()
        except urllib.error.HTTPError as error:
            refusal = (error.code, json.load(error)['error'])
            break
        installed += 1
    assert installed >= 40
    assert refusal is not None and refusal[0] == 507 and refusal[1]['code'] == 'insufficientStorage'
    assert '1048576 byte internal budget' in refusal[1]['message']
