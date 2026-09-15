"""Package a public RAM trial using an explicit list of redistributable files.

Includes our application, UI, CA bundle and notices. Never includes a device
backup, vendor runtime libraries, credentials, updater or firmware image.
"""
import argparse
import hashlib
import json
import re
import subprocess
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=ROOT / 'dist/bin/awtrix-tc002')
    parser.add_argument('--output', type=Path, default=ROOT / 'dist/public')
    args = parser.parse_args()
    binary = args.binary.read_bytes()
    # ELF32, little-endian, ARM: reject accidentally packaging a host build.
    if binary[:6] != b'\x7fELF\x01\x01' or binary[18:20] != b'\x28\x00':
        parser.error('--binary must be the compiled ARM TC002 application')
    version = re.search(r'AWTRIX_NG_VERSION="([^"]+)"', (ROOT / 'CMakeLists.txt').read_text())[1]
    if version.encode() + b'\0' not in binary:
        parser.error('binary does not contain the version declared in CMakeLists.txt')
    name = 'awtrix-ng-tc002-trial'
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    dirty = bool(subprocess.check_output(
        ['git', 'status', '--porcelain', '--untracked-files=no'], cwd=ROOT, text=True).strip())
    files = {
        'bin/awtrix-tc002': binary,
        'try.py': (ROOT / 'tools/trial.py').read_bytes(),
        'README.md': (ROOT / 'docs/TRY-IT.md').read_bytes(),
        'upstream/awtrix-ng/webui/index.html': (ROOT / 'upstream/awtrix-ng/webui/index.html').read_bytes(),
        'assets/cacert.pem': (ROOT / 'assets/cacert.pem').read_bytes(),
        'LICENSE.md': (ROOT / 'LICENSE.md').read_bytes(),
        'THIRD-PARTY-NOTICES.md': (ROOT / 'THIRD-PARTY-NOTICES.md').read_bytes(),
        'upstream/awtrix-ng/THIRD-PARTY-NOTICES.md': (ROOT / 'upstream/awtrix-ng/THIRD-PARTY-NOTICES.md').read_bytes(),
        'vendor/openssl/LICENSE': (ROOT / 'vendor/openssl/LICENSE').read_bytes(),
        'vendor/PubSubClient/LICENSE.txt': (ROOT / 'vendor/PubSubClient/LICENSE.txt').read_bytes(),
    }
    for path in sorted((ROOT / 'upstream/awtrix-ng/LICENSES').glob('*.txt')):
        files[path.relative_to(ROOT).as_posix()] = path.read_bytes()
    manifest = {'version': version, 'mode': 'temporary RAM trial',
                'source': 'https://github.com/sanderdw/awtrix-ng-tc002',
                'sourceCommit': commit, 'sourceDirty': dirty,
                'files': {path: {'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
                          for path, data in files.items()}}
    files['manifest.json'] = (json.dumps(manifest, indent=2) + '\n').encode()
    args.output.mkdir(parents=True, exist_ok=True)
    archive = args.output / (name + '.zip')
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as output:
        for path, data in files.items():
            info = zipfile.ZipInfo(f'{name}/{path}')
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = (0o100755 if path == 'bin/awtrix-tc002' else 0o100644) << 16
            output.writestr(info, data)
    checksum = hashlib.sha256(archive.read_bytes()).hexdigest()
    archive.with_suffix('.zip.sha256').write_text(f'{checksum}  {archive.name}\n')
    print(f'Public trial bundle: {archive}\nSHA256: {checksum}')


if __name__ == '__main__':
    main()
