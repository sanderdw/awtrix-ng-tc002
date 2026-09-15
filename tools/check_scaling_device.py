"""Check an installed TC002 with brief, silent notifications; preserve settings."""
import argparse
import json
from pathlib import Path
import sys
import time
import urllib.error
import urllib.request

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tests'))
from test_scaling import gif, screen_until

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('url')
p.add_argument('--output',default='device-private/scaling-live-screens.json')
a=p.parse_args()

def api(path,body=None,method='GET'):
    req=urllib.request.Request(a.url.rstrip('/')+path,
        None if body is None else json.dumps(body).encode(),
        {'Content-Type':'application/json'},method=method)
    with urllib.request.urlopen(req,timeout=5) as response:return json.load(response)

before=api('/api/v1/device')
settings=api('/api/v1/settings')
assert before['version'].startswith('1.1.0-tc002.'),before['version']
screens={}
name='tc002-scaling-check'
try:
    for w,h in [(8,8),(16,16),(52,16)]:
        raw=[1]*(w*h);raw[-1]=2
        api('/api/v1/notifications',{'name':name,'text':'','icon':gif(w,h,[raw]),
            'durationMs':2000,'stack':False},'POST')
        dw,dh=(min(52,w*2),h*2) if h<=8 else (w,h)
        expected=[0]*832
        palette=[0,0xff0000,0x00ff00,0x0000ff]
        for y in range(dh):
            for x in range(dw):
                expected[y*52+x]=palette[raw[(y*h//dh)*w+x*w//dw]]
        screens[f'icon-{w}x{h}']=screen_until(api,lambda px:px==expected,timeout=4)
        print(f'Live icon {w}x{h} -> {dw}x{dh}: every pixel verified')
    api('/api/v1/notifications',{'name':name,'text':'H','durationMs':2000,'stack':False},'POST')
    px=screen_until(api,lambda px:any(c==0xffffff for c in px[8*52:]))
    lit=[(i%52,i//52) for i,c in enumerate(px) if c==0xffffff]
    assert max(y for x,y in lit)-min(y for x,y in lit)+1>=10
    assert abs(min(x for x,y in lit)+max(x for x,y in lit)-51)<=1
    screens['notification-text']=px
    print('Live notification: enlarged font and centring verified')
    api('/api/v1/notifications/'+name,method='DELETE')
    for app in ['Time','Date','Battery']:
        api('/api/v1/apps/active',{'name':app,'fast':True},'PUT')
        time.sleep(.08)
        assert api('/api/v1/device')['currentApp']==app
        def doubled(px):
            if app == 'Battery' and before['version'] != '1.1.0-tc002.2':
                # Native percentage is vertically centred at rows 3..12.
                return (any(px[3*52+17:13*52]) and
                    all(px[y*52:y*52+16]==px[(y+1)*52:(y+1)*52+16] for y in range(0,16,2)) and
                    all(px[y*52+17:(y+1)*52]==px[(y+1)*52+17:(y+2)*52] for y in range(3,13,2)))
            return (any(px[:8*52]) and any(px[8*52:]) and
                all(px[y*52:(y+1)*52]==px[(y+1)*52:(y+2)*52] for y in range(0,16,2)))
        screens[app]=screen_until(api,doubled)
        print(f'Live {app}: full-height layout verified')
finally:
    try:
        api('/api/v1/notifications/'+name,method='DELETE')
    except urllib.error.HTTPError as error:
        if error.code!=404:raise
    api('/api/v1/apps/active',{'name':before['currentApp'],'fast':True},'PUT')
assert api('/api/v1/settings')==settings,'settings changed during verification'
after=api('/api/v1/device')
print('Version:',after['version'],'FPS:',after['fps'],'MQTT:',after['mqtt']['state'])
Path(a.output).write_text(json.dumps({'version':after['version'],'width':52,'height':16,'screens':screens}))
