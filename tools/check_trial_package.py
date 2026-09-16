"""Validate a public archive (trial or installer) before publishing it. Does not contact a clock."""
import argparse
import hashlib
import json
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path, PurePosixPath

NOTICES = {
    'LICENSE.md', 'THIRD-PARTY-NOTICES.md', 'upstream/awtrix-ng/THIRD-PARTY-NOTICES.md',
    'vendor/openssl/LICENSE', 'vendor/PubSubClient/LICENSE.txt', 'manifest.json', 'README.md',
}
KINDS = {
    'awtrix-ng-tc002-trial': NOTICES | {
        'bin/awtrix-tc002', 'try.py', 'upstream/awtrix-ng/webui/index.html', 'assets/cacert.pem'},
    'awtrix-ng-tc002-installer': NOTICES | {
        'bin/awtrix-tc002', 'bin/tc002-update', 'bin/libzkgui.so', 'install.py', 'image.py',
        'vendor-fingerprints.json', 'webui/index.html', 'assets/cacert.pem'},
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    parser.add_argument('--commit', help='require a clean build from this source commit')
    args = parser.parse_args()
    checksum = args.archive.with_suffix('.zip.sha256').read_text().strip()
    expected = hashlib.sha256(args.archive.read_bytes()).hexdigest() + '  ' + args.archive.name
    if checksum != expected:
        parser.error('archive checksum does not match')
    kind = args.archive.name[:-4]
    if kind not in KINDS:
        parser.error('unknown archive kind: ' + kind)
    REQUIRED = KINDS[kind]
    prefix = kind + '/'
    with zipfile.ZipFile(args.archive) as archive:
        paths = archive.namelist()
        if archive.testzip() is not None or len(paths) != len(set(paths)):
            parser.error('damaged archive or duplicate paths')
        for path in paths:
            if not path.startswith(prefix) or '..' in PurePosixPath(path).parts:
                parser.error('unsafe archive path')
            relative = path[len(prefix):]
            license_path = PurePosixPath(relative)
            is_license = (str(license_path.parent) == 'upstream/awtrix-ng/LICENSES'
                          and license_path.suffix == '.txt')
            if relative not in REQUIRED and not is_license:
                parser.error('unexpected public archive entry: ' + relative)
        names = {path[len(prefix):] for path in paths}
        if not REQUIRED <= names:
            parser.error('missing trial files or license notices')
        manifest = json.loads(archive.read(prefix + 'manifest.json'))
        if names != set(manifest['files']) | {'manifest.json'}:
            parser.error('manifest does not describe every packaged file')
        if args.commit and (manifest['sourceCommit'] != args.commit or manifest['sourceDirty']):
            parser.error('archive was not built from the expected clean source commit')
        for name, entry in manifest['files'].items():
            data = archive.read(prefix + name)
            if len(data) != entry['bytes'] or hashlib.sha256(data).hexdigest() != entry['sha256']:
                parser.error('file checksum mismatch: ' + name)
        for entry in names:
            if entry.startswith('bin/'):
                binary = archive.read(prefix + entry)
                if binary[:6] != b'\x7fELF\x01\x01' or binary[18:20] != b'\x28\x00':
                    parser.error(entry + ' is not an ARM Linux binary')
        with tempfile.TemporaryDirectory(prefix='tc002-package-check-') as tmp:
            archive.extractall(tmp)
            script, flags = ('try.py', ('--seconds', '--binary')) if 'try.py' in names else ('install.py', ('--restore', '--build-only'))
            result = subprocess.run([sys.executable, script, '--help'],
                                    cwd=Path(tmp) / prefix, check=True, capture_output=True, text=True)
            if any(flag not in result.stdout for flag in flags):
                parser.error(script + ' CLI is incomplete')
    print(f'Validated {len(paths)} public files in {kind} and all checksums; its CLI works.')


if __name__ == '__main__':
    main()
