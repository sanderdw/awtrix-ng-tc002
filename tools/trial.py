"""Run AWTRIX from RAM and restart the installed launcher on exit. Never flashes.
Usage: ADB=/path/to/adb uv run tools/trial.py CLOCK_IP --seconds 120 --tone
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import urllib.request

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('host')
p.add_argument('--seconds',type=int,default=120)
p.add_argument('--binary',type=Path,default=Path('build-tc002/awtrix-tc002'))
p.add_argument('--tone',action='store_true')
a=p.parse_args()
if not 20<=a.seconds<=600: p.error('seconds must be between 20 and 600')
adb=os.environ.get('ADB') or shutil.which('adb')
if not adb: p.error('set ADB to Google platform-tools adb')
serial=a.host+':5555'
def device(*args):
    return subprocess.run([adb,'-s',serial,*args],check=True,capture_output=True,text=True).stdout
subprocess.run([adb,'connect',serial],check=True)
if 'running' not in device('shell','getprop init.svc.zkswe'):
    p.error('stock zkswe service must be running before a trial')
device('shell','mkdir -p /tmp/awtrix-trial-data')
with tempfile.TemporaryDirectory() as tmp:
    config=Path(tmp)/'device.json'
    config.write_text(json.dumps({'mqttEnabled':False,'ntpServer':'','wifiSsid':'','panelWidth':52,'panels':1}))
    for source,target in [(a.binary,'/tmp/awtrix-trial-bin'),(config,'/tmp/awtrix-trial-data/device.json'),
        (next(w for w in (Path('build-webui/index.html'),Path('upstream/awtrix-ng/webui/index.html')) if w.exists()),'/tmp/awtrix-trial.html'),
        (Path('assets/cacert.pem'),'/tmp/awtrix-cacert.pem')]:
        device('push',str(source),target)
device('shell','chmod 700 /tmp/awtrix-trial-bin')
log_path=Path(tempfile.gettempdir())/'awtrix-trial-host.log'
log=open(log_path,'w')
command="trap 'setprop ctl.start zkswe' EXIT; setprop ctl.stop zkswe; " \
    "AWTRIX_CA_CERT=/tmp/awtrix-cacert.pem /tmp/awtrix-trial-bin --hardware --no-matrix " \
    "--data /tmp/awtrix-trial-data --webui /tmp/awtrix-trial.html --port 18081 " \
    f"--run-for {a.seconds} --pidfile /tmp/awtrix-trial.pid"
process=subprocess.Popen([adb,'-s',serial,'shell',command],stdout=log,stderr=subprocess.STDOUT)
try:
    url=f'http://{a.host}:18081'
    for attempt in range(100):
        try:
            with urllib.request.urlopen(url+'/api/v1/device',timeout=2) as response:
                state=json.load(response)
            break
        except OSError:
            if process.poll() is not None: raise RuntimeError(f'app exited; inspect {log_path}')
            time.sleep(.2)
    else: raise RuntimeError('web API did not become ready')
    print(f'Trial ready: {url}, {a.seconds} seconds',flush=True)
    if a.tone:
        req=urllib.request.Request(url+'/api/v1/audio/play',
            json.dumps({'rtttl':'Test:d=4,o=5,b=100:c,e,g,p,c,e,g'}).encode(),
            {'Content-Type':'application/json'},method='POST')
        with urllib.request.urlopen(req,timeout=4) as response:
            print('TONE STARTED: '+response.read().decode(),flush=True)
    process.wait(timeout=a.seconds+30)
finally:
    if process.poll() is None:
        device('shell','if [ -f /tmp/awtrix-trial.pid ]; then kill -TERM $(cat /tmp/awtrix-trial.pid); fi')
        process.wait(timeout=30)
    device('shell','setprop ctl.start zkswe')
    device('shell','rm -f /tmp/awtrix-trial-bin /tmp/awtrix-trial.html /tmp/awtrix-trial.pid')
    log.close()
    print('Stock service restored.',flush=True)
