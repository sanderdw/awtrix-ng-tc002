"""Package the public installer bundle from an explicit list of redistributable files.

Contains this port's binaries (application, launcher, update helper), web UI, CA bundle, the
installer, the image tooling, the vendor fingerprints (hashes only) and notices. Never a device
backup, a vendor library or a firmware image: the installer builds the image from the user's clock.
"""
import argparse
import hashlib
import json
import re
import subprocess
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
NAME = 'awtrix-ng-tc002-installer'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bin', type=Path, default=ROOT / 'dist/bin')
    parser.add_argument('--output', type=Path, default=ROOT / 'dist/public')
    args = parser.parse_args()
    version = re.search(r'AWTRIX_NG_VERSION="([^"]+)"', (ROOT / 'CMakeLists.txt').read_text())[1]
    binaries = {}
    for name in ('awtrix-tc002', 'tc002-update', 'libzkgui.so'):
        data = (args.bin / name).read_bytes()
        if data[:6] != b'\x7fELF\x01\x01' or data[18:20] != b'\x28\x00':
            parser.error(f'{name} is not an ARM Linux binary')
        binaries[name] = data
    if version.encode() + b'\0' not in binaries['awtrix-tc002']:
        parser.error('the application does not contain the version declared in CMakeLists.txt')
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    dirty = bool(subprocess.check_output(['git', 'status', '--porcelain', '--untracked-files=no'],
                                         cwd=ROOT, text=True).strip())
    files = {
        'install.py': (ROOT / 'tools/install.py').read_bytes(),
        'image.py': (ROOT / 'tools/image.py').read_bytes(),
        'vendor-fingerprints.json': (ROOT / 'src/tc002/vendor-fingerprints.json').read_bytes(),
        'README.md': (ROOT / 'docs/INSTALL.md').read_bytes(),
        'bin/awtrix-tc002': binaries['awtrix-tc002'],
        'bin/tc002-update': binaries['tc002-update'],
        'bin/libzkgui.so': binaries['libzkgui.so'],
        'webui/index.html': (ROOT / 'build-webui/index.html').read_bytes(),
        'assets/cacert.pem': (ROOT / 'assets/cacert.pem').read_bytes(),
        'LICENSE.md': (ROOT / 'LICENSE.md').read_bytes(),
        'THIRD-PARTY-NOTICES.md': (ROOT / 'THIRD-PARTY-NOTICES.md').read_bytes(),
        'upstream/awtrix-ng/THIRD-PARTY-NOTICES.md': (ROOT / 'upstream/awtrix-ng/THIRD-PARTY-NOTICES.md').read_bytes(),
        'vendor/openssl/LICENSE': (ROOT / 'vendor/openssl/LICENSE').read_bytes(),
        'vendor/PubSubClient/LICENSE.txt': (ROOT / 'vendor/PubSubClient/LICENSE.txt').read_bytes(),
    }
    for path in sorted((ROOT / 'upstream/awtrix-ng/LICENSES').glob('*.txt')):
        files[path.relative_to(ROOT).as_posix()] = path.read_bytes()
    manifest = {'version': version, 'mode': 'installer',
                'source': 'https://github.com/sanderdw/awtrix-ng-tc002',
                'sourceCommit': commit, 'sourceDirty': dirty,
                'files': {p: {'bytes': len(d), 'sha256': hashlib.sha256(d).hexdigest()} for p, d in files.items()}}
    files['manifest.json'] = (json.dumps(manifest, indent=2) + '\n').encode()
    args.output.mkdir(parents=True, exist_ok=True)
    archive = args.output / (NAME + '.zip')
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as out:
        for path, data in files.items():
            info = zipfile.ZipInfo(f'{NAME}/{path}')
            info.compress_type = zipfile.ZIP_DEFLATED
            executable = path.startswith('bin/') or path == 'install.py'
            info.external_attr = (0o100755 if executable else 0o100644) << 16
            out.writestr(info, data)
    checksum = hashlib.sha256(archive.read_bytes()).hexdigest()
    archive.with_suffix('.zip.sha256').write_text(f'{checksum}  {archive.name}\n')
    print(f'Installer bundle: {archive}\nSHA256: {checksum}')


if __name__ == '__main__':
    main()
