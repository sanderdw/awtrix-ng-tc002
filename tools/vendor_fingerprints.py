"""Maintain src/tc002/vendor-fingerprints.json and generate the header the firmware compiles in.

  generate --output PATH   write VendorFingerprints.h from the JSON (CMake runs this)
  capture CLOCK_IP         locate the listed rootfs libraries on a clock over ADB, hash them locally
                           and record path and hash; nothing from the clock is committed but hashes
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
JSON = ROOT / 'src/tc002/vendor-fingerprints.json'
sys.path.insert(0, str(Path(__file__).resolve().parent))


def load():
    return json.loads(JSON.read_text())


# Only the vendor application may be accepted by the functions it defines: the launcher calls
# nothing else in it. The other libraries get hand-measured structures and need an exact build.
SYMBOLS_ALLOWED = {'libulanzi-bootstrap.so'}
SYMBOL_NAME = re.compile(r'[A-Za-z_][A-Za-z0-9_]*')


def check_symbols(name, entry):
    symbols = entry.get('symbols')
    if symbols is None:
        return []
    if name not in SYMBOLS_ALLOWED:
        raise SystemExit(f'{name}: symbols are only accepted for the vendor application; '
                         'the port passes hand-measured structures to the others')
    if (not isinstance(symbols, list) or not symbols or len(set(symbols)) != len(symbols)
            or not all(isinstance(s, str) and SYMBOL_NAME.fullmatch(s) for s in symbols)):
        raise SystemExit(f'{name}: symbols must be a non-empty list of distinct C identifiers')
    return symbols


def generate(output):
    data = load()
    lines = ['// Generated from src/tc002/vendor-fingerprints.json by tools/vendor_fingerprints.py; do not edit.',
             '#pragma once', '', 'namespace tc002 {', '',
             'struct VendorFingerprint { const char* library; const char* path; const char* sha256; };',
             'struct VendorFile { const char* library; const char* path; };',
             'struct VendorSymbol { const char* library; const char* name; };',
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
    lines += ['  {nullptr, nullptr, nullptr},', '};', '// One entry per library, in the order of the JSON.',
              'inline constexpr VendorFile kVendorFiles[] = {']
    lines += [f'  {{"{name}", "{entry.get("path") or ""}"}},' for name, entry in data['libraries'].items()]
    lines += ['  {nullptr, nullptr},', '};',
              '// Functions a library may define in its ELF dynamic symbol table instead of carrying a recorded hash.',
              'inline constexpr VendorSymbol kVendorSymbols[] = {']
    symbols = 0
    for name, entry in data['libraries'].items():
        for symbol in check_symbols(name, entry):
            lines.append(f'  {{"{name}", "{symbol}"}},')
            symbols += 1
    lines += ['  {nullptr, nullptr},', '};', '', '}', '']
    text = '\n'.join(lines)
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    if not output.exists() or output.read_text() != text:
        output.write_text(text)
    print(f'vendor fingerprints: {sum(len(e["sha256"]) for e in data["libraries"].values())} recorded, '
          f'{symbols} required symbols')


def capture(host):
    from paths import adb as find_adb
    adb = find_adb()
    serial = host if ':' in host else host + ':5555'
    subprocess.run([adb, 'connect', serial], check=True)

    def shell(command):
        return subprocess.run([adb, '-s', serial, 'shell', command], check=True,
                              capture_output=True, text=True).stdout.strip()
    data = load()
    with tempfile.TemporaryDirectory(prefix='tc002-vendor-') as tmp:
        for name, entry in data['libraries'].items():
            path = entry.get('path')
            if not path:
                # The clock's BusyBox has no find; try the directories the firmware uses.
                for directory in ('/lib', '/res/lib', '/usr/lib'):
                    if shell(f'ls {directory}/{name} 2>/dev/null').strip() == f'{directory}/{name}':
                        path = f'{directory}/{name}'
                        break
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
