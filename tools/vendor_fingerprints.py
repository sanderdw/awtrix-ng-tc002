"""Maintain src/tc002/vendor-fingerprints.json and generate the header the firmware compiles in.

  generate --output PATH   write VendorFingerprints.h from the JSON (CMake runs this)
  capture CLOCK_IP         locate the listed rootfs libraries on a clock over ADB, hash them locally
                           and record path and hash; nothing from the clock is committed but hashes
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
JSON = ROOT / 'src/tc002/vendor-fingerprints.json'


def load():
    return json.loads(JSON.read_text())


def generate(output):
    data = load()
    lines = ['// Generated from src/tc002/vendor-fingerprints.json by tools/vendor_fingerprints.py; do not edit.',
             '#pragma once', '', 'namespace tc002 {', '',
             'struct VendorFingerprint { const char* library; const char* path; const char* sha256; };',
             f'inline constexpr const char* kStockApp = "{data["stock"]["app"]}";',
             f'inline constexpr const char* kStockMcu = "{data["stock"]["mcu"]}";',
             'inline constexpr VendorFingerprint kVendorFingerprints[] = {']
    for name, entry in data['libraries'].items():
        # A library without a recorded hash is still listed, so the firmware reports it as
        # unverified instead of silently trusting whatever the clock has.
        for digest in entry['sha256'] or ['']:
            if digest and (len(digest) != 64 or not entry.get('path')):
                raise SystemExit(f'{name}: a hash needs a 64-character digest and a path')
            lines.append(f'  {{"{name}", "{entry.get("path") or ""}", "{digest}"}},')
    lines += ['  {nullptr, nullptr, nullptr},', '};', '', '}', '']
    text = '\n'.join(lines)
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    if not output.exists() or output.read_text() != text:
        output.write_text(text)
    print(f'vendor fingerprints: {sum(len(e["sha256"]) for e in data["libraries"].values())} recorded')


def capture(host):
    adb = os.environ.get('ADB') or shutil.which('adb')
    if not adb:
        raise SystemExit('set ADB to Google platform-tools adb')
    serial = host if ':' in host else host + ':5555'
    subprocess.run([adb, 'connect', serial], check=True)

    def shell(command):
        return subprocess.run([adb, '-s', serial, 'shell', command], check=True,
                              capture_output=True, text=True).stdout.strip()
    data = load()
    with tempfile.TemporaryDirectory(prefix='tc002-vendor-') as tmp:
        for name, entry in data['libraries'].items():
            path = entry.get('path') or shell(f"find /lib /usr/lib /res/lib -name {name} 2>/dev/null | head -1")
            if not path:
                print(f'{name}: not found on the clock')
                continue
            local = Path(tmp) / name
            subprocess.run([adb, '-s', serial, 'pull', path, str(local)], check=True, capture_output=True)
            digest = hashlib.sha256(local.read_bytes()).hexdigest()
            entry['path'] = path
            if digest not in entry['sha256']:
                entry['sha256'].append(digest)
            print(f'{name}: {path} {digest[:16]}...')
    JSON.write_text(json.dumps(data, indent=2) + '\n')
    print(f'updated {JSON}; rebuild so the firmware trusts these files')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='command', required=True)
    sub.add_parser('generate').add_argument('--output', required=True)
    sub.add_parser('capture').add_argument('host')
    args = parser.parse_args()
    if args.command == 'generate':
        generate(args.output)
    else:
        capture(args.host)


if __name__ == '__main__':
    main()
