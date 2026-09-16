#!/usr/bin/env python3
"""Install AWTRIX NG TC002 on a Ulanzi TC002 from the clock's own partition.

Standard library only. Needs Google's adb (downloaded on demand) and squashfs-tools on this
computer, and the clock on the same network with its ADB port open.

  install.py CLOCK_IP              read the clock, build the images, preflight, ask, flash
  install.py CLOCK_IP --restore    flash the recovery image made by an earlier run
  install.py CLOCK_IP --build-only stop after building the images (nothing is flashed)

What it never does: download a firmware file (the image contains Ulanzi's own application,
so it is built here from your clock), or flash without the helper's preflight and your typed
confirmation. Everything it produces is kept under ~/.awtrix-ng-tc002/CLOCK_IP/.
"""
import argparse
import datetime
import hashlib
import json
import os
import platform
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import image  # noqa: E402

PLATFORM_TOOLS = {
    "Linux": "https://dl.google.com/android/repository/platform-tools-latest-linux.zip",
    "Darwin": "https://dl.google.com/android/repository/platform-tools-latest-darwin.zip",
    "Windows": "https://dl.google.com/android/repository/platform-tools-latest-windows.zip",
}
RES_LINE = 'mtd3: 00800000 00010000 "res"'
HELPER = "/tmp/awtrix-update-helper"
IMAGE_ON_CLOCK = "/tmp/awtrix-update.img"


class Fail(SystemExit):
    def __init__(self, message):
        super().__init__("\nerror: " + message)


def say(message):
    print(message, flush=True)


def bundle_manifest():
    path = HERE / "manifest.json"
    if path.exists():
        return json.loads(path.read_text())
    return {"version": image.firmware_version()}


def bundle_paths():
    """Binaries, web UI and CA bundle: laid out like the installer ZIP, or like the repository."""
    if (HERE / "bin/awtrix-tc002").exists():
        return HERE / "bin", HERE / "webui/index.html", HERE / "assets/cacert.pem"
    root = HERE.parent
    return root / "dist/bin", root / "build-webui/index.html", root / "assets/cacert.pem"


def find_adb(work):
    candidates = [os.environ.get("ADB"), shutil.which("adb"),
                  str(work / "platform-tools" / ("adb.exe" if platform.system() == "Windows" else "adb"))]
    for candidate in candidates:
        if candidate and os.access(candidate, os.X_OK):
            return candidate
    url = PLATFORM_TOOLS.get(platform.system())
    if not url:
        raise Fail("install Google platform-tools and set ADB to its adb executable")
    say(f"downloading Google platform-tools (adb) into {work}")
    archive = work / "platform-tools.zip"
    urllib.request.urlretrieve(url, archive)
    with zipfile.ZipFile(archive) as z:
        z.extractall(work)
    adb = candidates[-1]
    os.chmod(adb, 0o755)
    return adb


class Clock:
    def __init__(self, adb, host):
        self.adb = adb
        self.serial = host if ":" in host else host + ":5555"
        self.host = self.serial.split(":")[0]

    def run(self, *args, check=True, timeout=600):
        result = subprocess.run([self.adb, "-s", self.serial, *args], capture_output=True, text=True,
                                timeout=timeout)
        if check and result.returncode:
            raise Fail(f"adb {' '.join(args)} failed: {result.stderr.strip() or result.stdout.strip()}")
        return result.stdout

    def shell(self, command):
        return self.run("shell", command).replace("\r", "").strip()

    def connect(self):
        result = subprocess.run([self.adb, "connect", self.serial], capture_output=True, text=True, timeout=60)
        if "connected" not in result.stdout:
            raise Fail(f"cannot reach the clock's ADB at {self.serial}: {result.stdout.strip()}\n"
                       "The clock must be on this network with TCP port 5555 open (stock firmware opens it).")
        if self.shell("echo ok") != "ok":
            raise Fail("the clock's shell does not answer")


def http_version(host):
    try:
        with urllib.request.urlopen(f"http://{host}/api/v1/version", timeout=3) as r:
            return json.load(r).get("version")
    except Exception:
        return None


def check_tools():
    for tool in ("unsquashfs", "mksquashfs"):
        if not shutil.which(tool):
            raise Fail(f"{tool} is missing: install squashfs-tools (Debian/Ubuntu: apt install squashfs-tools, "
                       "macOS: brew install squashfs)")


def check_clock(clock):
    say(f"checking the clock at {clock.host}")
    if "running" not in clock.shell("getprop init.svc.zkswe"):
        raise Fail("the clock's application service is not running; power-cycle it and try again")
    if RES_LINE not in clock.shell("cat /proc/mtd"):
        raise Fail("this clock does not have the expected 8 MiB res partition; not a supported TC002")
    version = http_version(clock.host)
    say(f"  application partition found; currently running {version or 'the stock Ulanzi app'}")
    return version


def dump_partition(clock, out):
    say("reading the clock's application partition (8 MiB, about half a minute)")
    clock.run("pull", "/dev/block/mtdblock3", str(out), timeout=600)
    data = out.read_bytes()
    if len(data) != image.MAX_RES or data[:4] != b"hsqs":
        raise Fail("the partition dump is not an 8 MiB squashfs; stopping")
    return data


def fingerprints():
    return json.loads((HERE / "vendor-fingerprints.json").read_text())


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def verify_vendor(clock, dump, work):
    """Hash the vendor files the port depends on; they must match a firmware this port was verified with."""
    recorded = fingerprints()
    say(f"verifying the clock's stock firmware (expected app {recorded['stock']['app']}, MCU {recorded['stock']['mcu']})")
    results = {}
    with tempfile.TemporaryDirectory(prefix="tc002-verify-") as tmp:
        tree = Path(tmp) / "res"
        subprocess.run(["unsquashfs", "-no-progress", "-d", str(tree), str(dump)], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        gui = tree / (image.VENDOR_KEPT_AS if image.has_port_installed(tree) else image.VENDOR_GUI)
        results["libulanzi-bootstrap.so"] = sha256_file(gui) if gui.exists() else ""
        for name, entry in recorded["libraries"].items():
            if name == "libulanzi-bootstrap.so" or not entry.get("path"):
                continue
            local = Path(tmp) / name
            clock.run("pull", entry["path"], str(local))
            results[name] = sha256_file(local)
    ok = True
    for name, digest in results.items():
        good = digest in recorded["libraries"][name]["sha256"]
        ok = ok and good
        say(f"  {name:24s} {'verified' if good else 'UNKNOWN BUILD'} {digest[:16]}")
    return ok


def confirm(prompt, word):
    say(prompt)
    try:
        stream = open("/dev/tty") if not sys.stdin.isatty() and os.path.exists("/dev/tty") else sys.stdin
    except OSError:
        stream = sys.stdin
    answer = stream.readline().strip()
    return answer == word


def flash(clock, helper, img, expected_version, yes, what):
    say(f"pushing the update helper and {what} to the clock")
    clock.run("push", str(helper), HELPER)
    clock.run("push", str(img), IMAGE_ON_CLOCK)
    clock.shell(f"chmod 700 {HELPER}")
    say("running the helper's preflight (nothing is written)")
    out = clock.run("shell", f"{HELPER} --preflight {IMAGE_ON_CLOCK}; echo \"exit=$?\"").replace("\r", "")
    say("  " + out.strip().replace("\n", "\n  "))
    if "exit=0" not in out:
        raise Fail("preflight refused; nothing was written")
    say("\nThe clock's application partition will now be erased and rewritten in place. There is no\n"
        "second copy on the clock. Keep it on USB power and do not unplug it until it has rebooted.\n"
        "If it does not come back: hold the knob while powering on to start the stock app.")
    if not yes and not confirm("Type  flash  to continue, anything else to stop:", "flash"):
        raise Fail("stopped before writing anything")
    say("installing; the clock stops responding for one to two minutes and then reboots")
    subprocess.run([clock.adb, "-s", clock.serial, "shell", f"{HELPER} --install {IMAGE_ON_CLOCK}"],
                   capture_output=True, text=True, timeout=120)
    return wait_for(clock, expected_version)


def wait_for(clock, expected_version, timeout=300):
    import time
    deadline = time.monotonic() + timeout
    time.sleep(30)
    while time.monotonic() < deadline:
        version = http_version(clock.host)
        if expected_version and version == expected_version:
            say(f"the clock is back and reports {version}")
            return True
        if not expected_version and version is None:
            try:
                subprocess.run([clock.adb, "connect", clock.serial], capture_output=True, timeout=30)
                if "running" in clock.shell("getprop init.svc.zkswe"):
                    say("the clock is back on the stock application")
                    return True
            except Exception:
                pass
        time.sleep(5)
    say("the clock did not answer within five minutes; give it a normal power cycle, and if the\n"
        "Wi-Fi pixel keeps pulsing, wait two more minutes before judging")
    return False


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("host", help="the clock's IP address")
    p.add_argument("--work", type=Path, default=Path.home() / ".awtrix-ng-tc002",
                   help="where dumps, images and adb are kept")
    p.add_argument("--restore", action="store_true", help="flash the recovery image from the latest run")
    p.add_argument("--build-only", action="store_true", help="build the images and stop")
    p.add_argument("--yes", action="store_true", help="do not ask for the typed confirmation")
    p.add_argument("--allow-unverified", action="store_true",
                   help="continue on a stock firmware this port was not verified with (audio and Wi-Fi "
                        "provisioning stay off on the clock)")
    a = p.parse_args()
    manifest = bundle_manifest()
    version = manifest["version"]
    say(f"AWTRIX NG TC002 installer, version {version}")
    check_tools()
    a.work.mkdir(parents=True, exist_ok=True)
    adb = find_adb(a.work)
    clock = Clock(adb, a.host)
    clock.connect()
    clock_dir = a.work / clock.host
    if a.restore:
        runs = sorted(d for d in clock_dir.glob("*/restore-stock.img"))
        if not runs:
            raise Fail(f"no recovery image under {clock_dir}; nothing to restore")
        img = runs[-1]
        say(f"restoring the stock application partition from {img}")
        check_clock(clock)
        flash(clock, bundle_paths()[0] / "tc002-update", img, None, a.yes, "the recovery image")
        return
    check_clock(clock)
    run_dir = clock_dir / datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    run_dir.mkdir(parents=True)
    dump = run_dir / "res-partition.bin"
    live = dump_partition(clock, dump)
    if not verify_vendor(clock, dump, a.work):
        if not a.allow_unverified:
            raise Fail("this clock's stock firmware is not one this port was verified with; stopping.\n"
                       "Re-run with --allow-unverified to install anyway with audio and Wi-Fi provisioning off.")
        say("  continuing on an unverified stock firmware (--allow-unverified)")
    build, webui, cacert = bundle_paths()
    say("building update.img and restore-stock.img from your clock's partition")
    built = image.build_images(live, build, webui, cacert, run_dir, version)
    say(f"  update.img        {built['update.img']['bytes']} bytes")
    say(f"  restore-stock.img {built['restore-stock.img']['bytes']} bytes  (keep this: it puts the stock app back)")
    say(f"  kept under {run_dir}")
    if a.build_only:
        return
    helper = build / "tc002-update"
    if flash(clock, helper, run_dir / "update.img", version, a.yes, "update.img"):
        say(f"\nDone. Web UI: http://{clock.host}/   Restore stock later with:\n"
            f"  {sys.argv[0]} {clock.host} --restore")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        raise SystemExit("\nstopped")
