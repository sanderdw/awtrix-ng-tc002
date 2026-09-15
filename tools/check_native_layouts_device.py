"""Compare the installed default layouts with deterministic native rendering.

Reads the clock timezone from an existing private config backup. Briefly selects
Time, Date and Battery, then restores the original app without changing settings.
"""
import argparse
import functools
import json
import os
from pathlib import Path
import subprocess
import time
import urllib.request

ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('url')
p.add_argument('--config',type=Path,required=True)
p.add_argument('--output',type=Path,default=ROOT/'device-private/approved-layouts-live.json')
a=p.parse_args()
os.environ['TZ']=json.loads(a.config.read_text())['tz'];time.tzset()
def api(path,body=None,method='GET'):
    req=urllib.request.Request(a.url.rstrip('/')+path,None if body is None else json.dumps(body).encode(),
                               {'Content-Type':'application/json'},method=method)
    with urllib.request.urlopen(req,timeout=5) as response:return json.load(response)
@functools.lru_cache(maxsize=32)
def expected(*args):
    return json.loads(subprocess.check_output([ROOT/'build-host/test-layout-render',*map(str,args)]))
before=api('/api/v1/device');settings=api('/api/v1/settings')
assert before['version']=='1.1.0-tc002.3',before['version']
assert settings['timeMode']==1 and settings['timeLeadingZero'] and settings['time24h']
assert settings['dateOrder']=='dayMonthYear' and settings['dateSeparator']=='dot'
assert settings['dateYearMode']=='twoDigit' and not settings['dateShowWeekday'] and not settings['dateMonthNames']
assert settings['weekdayBar']['show'] and settings['weekdayBar']['startOnMonday']
for field in ['timeColor','dateColor','batteryColor']:
    assert settings[field] is None,'Verifier expects the approved default colours'
screens={}
try:
    for name in ['Time','Date','Battery']:
        api('/api/v1/apps/active',{'name':name,'fast':True},'PUT')
        time.sleep(.1)
        deadline=time.monotonic()+4
        while time.monotonic()<deadline:
            state=api('/api/v1/device')
            pixels=api('/api/v1/display/screen')['pixels']
            if state['currentApp']!=name:raise AssertionError('Active app changed during verification')
            candidates=[]
            if name=='Battery':
                assert not state['lowBattery'],'Verifier expects normal battery state'
                candidates=[expected(name,state['batteryPercent'])]
            else:
                for delta in [-60,0,60]:
                    now=time.localtime(time.time()+delta);weekday=(now.tm_wday+1)%7
                    args=(now.tm_hour,now.tm_min,now.tm_mday,weekday) if name=='Time' else (now.tm_year,now.tm_mon,now.tm_mday,weekday)
                    candidates.append(expected(name,*args))
            # The live separator pulses; geometry and all other pixels must match.
            mask={y*52+x for y in [4,5,8,9] for x in [33,34]} if name=='Time' else set()
            if any(all(v==ref[i] for i,v in enumerate(pixels) if i not in mask) for ref in candidates):
                if mask:assert len({pixels[i] for i in mask})==1
                screens[name]=pixels
                print(name+': installed layout matches expected pixels'+(' (animated colon checked separately)' if mask else ''),flush=True)
                break
            time.sleep(.08)
        else:raise AssertionError(name+' did not match the approved geometry')
finally:
    api('/api/v1/apps/active',{'name':before['currentApp'],'fast':True},'PUT')
assert api('/api/v1/settings')==settings,'settings changed'
state=api('/api/v1/device')
a.output.write_text(json.dumps({'version':state['version'],'width':52,'height':16,'screens':screens}))
print('FPS:',state['fps'],'MQTT:',state['mqtt']['state'],'settings unchanged')
