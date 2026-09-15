"""Test the actual FlyThings startup plugin from RAM; automatically restore stock."""
import json,os,shutil,subprocess,tempfile,time,urllib.request
from pathlib import Path
import argparse
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('host')
a=p.parse_args()
adb=os.environ.get('ADB') or shutil.which('adb')
if not adb: p.error('set ADB to Google platform-tools adb')
serial=a.host+':5555'
def device(*args):
    return subprocess.run([adb,'-s',serial,*args],check=True,capture_output=True,text=True).stdout
subprocess.run([adb,'connect',serial],check=True)
if 'running' not in device('shell','getprop init.svc.zkswe'): p.error('stock must be running')
if 'exists' in device('shell','if [ -f /tmp/EasyUI.cfg ]; then echo exists; fi'):
    p.error('an EasyUI test override already exists; finish that trial first')
device('shell','mkdir -p /tmp/awtrix-bundle/bin /tmp/awtrix-bundle/lib /tmp/awtrix-bundle/ui')
for source,target in [('dist/bin/awtrix-tc002','bin/awtrix-tc002'),('dist/bin/libzkgui.so','lib/libzkgui.so'),
    ('upstream/awtrix-ng/webui/index.html','ui/awtrix.html'),('assets/cacert.pem','cacert.pem')]:
    device('push',source,'/tmp/awtrix-bundle/'+target)
with tempfile.TemporaryDirectory() as tmp:
    original=Path(tmp)/'original.cfg'; device('pull','/res/etc/EasyUI.cfg',str(original))
    cfg=json.loads(original.read_text()); cfg['startupLibPath']='/tmp/awtrix-bundle/lib/libzkgui.so'
    override=Path(tmp)/'trial.cfg'; override.write_text(json.dumps(cfg))
    device('push',str(override),'/tmp/awtrix-loader.cfg')
command="trap 'rm -f /tmp/EasyUI.cfg; setprop ctl.start zkswe' EXIT; " \
    "setprop ctl.stop zkswe; cp /tmp/awtrix-loader.cfg /tmp/EasyUI.cfg; " \
    "AWTRIX_CA_CERT=/tmp/awtrix-bundle/cacert.pem AWTRIX_TC002_TRIAL_ROOT=/tmp/awtrix-bundle /bin/zkgui"
with open('/tmp/awtrix-launcher-host.log','w') as log:
    process=subprocess.Popen([adb,'-s',serial,'shell',command],stdout=log,stderr=subprocess.STDOUT)
    try:
        for attempt in range(150):
            try:
                with urllib.request.urlopen(f'http://{a.host}:18081/api/v1/device',timeout=2) as r: state=json.load(r)
                print('Launcher trial ready:',state['version'],'fps',state['fps'],flush=True); break
            except OSError:
                if process.poll() is not None: raise RuntimeError('launcher exited; inspect /tmp/awtrix-launcher-host.log')
                time.sleep(.2)
        else: raise RuntimeError('launcher did not reach the web API')
        process.wait(timeout=130)
    finally:
        if process.poll() is None:
            # The bundle process replaces zkgui with this uniquely named binary.
            device('shell','killall awtrix-tc002')
            process.wait(timeout=30)
        device('shell','rm -f /tmp/EasyUI.cfg /tmp/awtrix-loader.cfg; setprop ctl.start zkswe')
        device('shell','rm -rf /tmp/awtrix-bundle')
        print('Stock service restored; loader override removed.',flush=True)
