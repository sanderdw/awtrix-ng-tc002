"""Archive source and a PERSONAL firmware package, preserving license notices.

The firmware ZIP contains stock device files. Do not upload it as a public release.
"""
from pathlib import Path
import hashlib
import json
import tarfile
import zipfile

root=Path(__file__).resolve().parents[1]
dist=root/'dist'
manifest=json.loads((dist/'manifest.json').read_text())
files=['update.img','restore-stock.img','manifest.json','bin/tc002-update','bin/awtrix-tc002','bin/libzkgui.so']
for name in files:
    if name=='manifest.json': continue
    payload=(dist/name).read_bytes()
    assert len(payload)==manifest[name]['bytes'] and hashlib.sha256(payload).hexdigest()==manifest[name]['sha256'],name+' does not match manifest'
source=['.gitignore','CMakeLists.txt','cmake','src','tools','tests','patches','upstream','vendor','assets','docs','pyproject.toml','uv.lock','README.md','CONTRIBUTING.md','WORKLOG.md','LICENSE.md','THIRD-PARTY-NOTICES.md']
def include(info):
    return None if any(x in info.name.split('/') for x in ['__pycache__','.pio','.pytest_cache','node_modules','.git']) else info
with tarfile.open(dist/'awtrix-tc002-source.tar.gz','w:gz') as archive:
    for name in source: archive.add(root/name,arcname='awtrix-tc002/'+name,filter=include)
with zipfile.ZipFile(dist/'awtrix-tc002-release.zip','w',compression=zipfile.ZIP_DEFLATED) as archive:
    for name in files: archive.write(dist/name,name)
    notices=[root/name for name in ['README.md','WORKLOG.md','LICENSE.md','THIRD-PARTY-NOTICES.md','upstream/awtrix-ng/THIRD-PARTY-NOTICES.md','vendor/openssl/LICENSE']]
    for path in notices+list((root/'upstream/awtrix-ng/LICENSES').iterdir()):
        if path.is_file(): archive.write(path,str(path.relative_to(root)))
checks=[]
for path in sorted(dist.iterdir()):
    if path.is_file() and path.name!='SHA256SUMS': checks.append(hashlib.sha256(path.read_bytes()).hexdigest()+'  '+path.name)
(dist/'SHA256SUMS').write_text('\n'.join(checks)+'\n')
print('Release, source and SHA256SUMS written.')
