#!/usr/bin/env python3
"""Install the locally built AWTRIX NG TC002 firmware on a clock, through the project's own installer.

  flash_dev_build.py CLOCK_IP --build-only   dump the clock, verify it, build both images; writes nothing
  flash_dev_build.py CLOCK_IP --yes          the same, then preflight on the clock and flash
  flash_dev_build.py CLOCK_IP --restore --yes   flash the latest restore-stock.img back

Why this wrapper exists: tools/install.py only finds vendor-fingerprints.json in the packaged
bundle layout, so it is packaged into a temporary directory and run from there; and its flash step
dies on a 120 s adb timeout when the clock reboots under it, although the write completes, so this
script keeps watching the clock instead of reporting a failure.

Flashing needs --yes because the installer's typed confirmation reads /dev/tty, which an agent
cannot answer. Pass it only after the person has agreed to this clock being rewritten.
Standard library only; run it from anywhere inside the repository.
"""
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
import time
import urllib.request
import zipfile
from pathlib import Path

BUNDLE = "awtrix-ng-tc002-installer"
WRITE_STARTED = "installing; the clock stops responding"


def repo_root():
    out = subprocess.run(["git", "rev-parse", "--show-toplevel"], capture_output=True, text=True)
    root = Path(out.stdout.strip()) if out.returncode == 0 else Path.cwd()
    if not (root / "tools/package_installer.py").exists():
        sys.exit("error: run this inside the awtrix-ng-tc002 repository")
    return root


def clock_version(host):
    try:
        with urllib.request.urlopen(f"http://{host}/api/v1/version", timeout=3) as r:
            return json.load(r).get("version")
    except Exception:
        return None


def find_adb(root):
    for candidate in (os.environ.get("ADB"), root / "build-deps/platform-tools/adb"):
        if candidate and os.access(candidate, os.X_OK):
            return str(candidate)
    return None  # the installer then looks on PATH or downloads Google's platform-tools itself


def check_build(root):
    """The bundle ships dist/bin; refuse a binary that is missing, of another version, or older than the sources."""
    version = re.search(r'AWTRIX_NG_VERSION="([^"]+)"', (root / "CMakeLists.txt").read_text())[1]
    binary = root / "dist/bin/awtrix-tc002"
    if not binary.exists():
        sys.exit("error: dist/bin/awtrix-tc002 is missing; run `bash tools/build.sh` first")
    if version.encode() not in binary.read_bytes():
        sys.exit(f"error: dist/bin/awtrix-tc002 does not contain {version}; run `bash tools/build.sh` first")
    sources = [root / "CMakeLists.txt", *(f for d in ("src", "patches") for f in (root / d).rglob("*") if f.is_file())]
    newest = max(sources, key=lambda f: f.stat().st_mtime)
    if newest.stat().st_mtime > binary.stat().st_mtime:
        sys.exit(f"error: {newest.relative_to(root)} is newer than dist/bin/awtrix-tc002; run `bash tools/build.sh` first")
    dirty = subprocess.run(["git", "status", "--porcelain", "--untracked-files=no"], cwd=root,
                           capture_output=True, text=True).stdout.strip()
    commit = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=root,
                            capture_output=True, text=True).stdout.strip()
    print(f"build: {version} at {commit}{' (working tree has uncommitted changes)' if dirty else ''}")
    return version


def package(root, into):
    subprocess.run(["uv", "run", "tools/package_installer.py", "--output", str(into)], cwd=root, check=True,
                   stdout=subprocess.DEVNULL)
    archive = into / f"{BUNDLE}.zip"
    recorded = (into / f"{BUNDLE}.zip.sha256").read_text().split()[0]
    if hashlib.sha256(archive.read_bytes()).hexdigest() != recorded:
        sys.exit("error: the packaged bundle does not match its checksum")
    with zipfile.ZipFile(archive) as z:
        z.extractall(into)
    for name in ("tc002-update", "awtrix-tc002"):
        os.chmod(into / BUNDLE / "bin" / name, 0o755)
    return into / BUNDLE


def wait_for_version(host, expected, seconds=300):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        version = clock_version(host)
        if version == expected:
            return True
        time.sleep(5)
    return False


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("host", help="the clock's IP address")
    mode = p.add_mutually_exclusive_group(required=True)
    mode.add_argument("--build-only", action="store_true", help="build the images and stop; nothing is written")
    mode.add_argument("--yes", action="store_true", help="flash without the typed confirmation")
    p.add_argument("--restore", action="store_true", help="flash the latest restore-stock.img instead")
    p.add_argument("--allow-unverified", action="store_true",
                   help="continue on a stock firmware the port was not verified with")
    a = p.parse_args()
    if a.restore and a.build_only:
        p.error("--restore writes to the clock; it needs --yes")

    root = repo_root()
    version = check_build(root)
    before = clock_version(a.host)
    print(f"clock: {a.host} currently reports {before or 'no AWTRIX version (stock app, or unreachable)'}")

    with tempfile.TemporaryDirectory(prefix="tc002-installer-") as tmp:
        bundle = package(root, Path(tmp))
        command = [sys.executable, str(bundle / "install.py"), a.host]
        command += ["--restore"] if a.restore else []
        command += ["--build-only"] if a.build_only else ["--yes"]
        command += ["--allow-unverified"] if a.allow_unverified else []
        env = dict(os.environ)
        adb = find_adb(root)
        if adb:
            env["ADB"] = adb
        process = subprocess.Popen(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        started, after_write = False, []
        for line in process.stdout:
            if started:
                after_write.append(line)  # held back: on the adb timeout this is only a traceback
                continue
            print(line, end="", flush=True)
            started = WRITE_STARTED in line
        code = process.wait()
        if code == 0:
            print("".join(after_write), end="", flush=True)

    if a.build_only:
        sys.exit(code)
    if code == 0:
        return
    if not started:
        sys.exit(f"\nThe installer stopped before writing anything (exit {code}).")
    print("\nThe installer lost its adb connection while the clock rebooted; the write had already started.\n"
          "Do not re-run and do not unplug the clock. Watching for it to come back...", flush=True)
    if a.restore:
        print("After a restore the stock app has no /api/v1/version; check the clock's display, or\n"
              f"`adb connect {a.host}:5555` and `adb shell getprop init.svc.zkswe` (expect: running).")
        return
    if wait_for_version(a.host, version):
        print(f"the clock is back and reports {version}")
        return
    sys.exit("The clock did not report the new version within five minutes. Give it one normal power cycle;\n"
             "if it still does not start, hold the knob while powering on for the stock app, then --restore.")


if __name__ == "__main__":
    main()
