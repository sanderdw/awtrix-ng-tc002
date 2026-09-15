"""Check PCM playback state without making sound. Uses an isolated RAM trial.
Generate a two-second MP3 fixture with ffmpeg, then run this tool during a trial.
"""
import argparse,json,time,urllib.request
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('url'); p.add_argument('mp3',type=Path); a=p.parse_args()
base=a.url.rstrip('/')
def api(path,body=None,method='GET'):
    req=urllib.request.Request(base+path,None if body is None else json.dumps(body).encode(),{'Content-Type':'application/json'},method=method)
    with urllib.request.urlopen(req,timeout=5) as r: return json.load(r)
settings=api('/api/v1/settings')
try:
    api('/api/v1/settings',{'mp3Volume':0,'radioVolume':0},'PATCH')
    boundary='TC002MP3'
    body=(f'--{boundary}\r\nContent-Disposition: form-data; name="file"; filename="test.mp3"\r\nContent-Type: audio/mpeg\r\n\r\n').encode()+a.mp3.read_bytes()+f'\r\n--{boundary}--\r\n'.encode()
    req=urllib.request.Request(base+'/api/v1/files?dir=/MP3',body,{'Content-Type':f'multipart/form-data; boundary={boundary}'},method='POST')
    with urllib.request.urlopen(req,timeout=5) as r: assert r.status==200
    for payload,key in [({'mp3':'test'},'mp3'),({'url':'http://127.0.0.1:18081/MP3/test.mp3'},'radio')]:
        assert api('/api/v1/audio/play',payload,'POST')['ok']
        time.sleep(.25)
        during=api('/api/v1/audio')
        assert during[key]['playing'],during
        time.sleep(3)
        after=api('/api/v1/audio')
        assert not after[key]['playing'] and not after['radio']['error'],after
        print(key+': decoded, played for the fixture duration, and completed without errors',flush=True)
finally:
    api('/api/v1/audio/stop',{},'POST')
    api('/api/v1/settings',{'mp3Volume':settings['mp3Volume'],'radioVolume':settings['radioVolume']},'PATCH')
