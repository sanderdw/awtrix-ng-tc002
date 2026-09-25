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
import struct
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
VENDOR_APP = "libulanzi-bootstrap.so"
ISSUES = "https://github.com/sanderdw/awtrix-ng-tc002/issues"
# Little-endian ELF header, section header and symbol layouts, by EI_CLASS (1: ELF32, 2: ELF64).
ELF_LAYOUTS = {1: ("<16sHHIIIIIHHHHHH", "<10I", "<IIIBBH"), 2: ("<16sHHIQQQIHHHHHH", "<IIQQQQIIQQ", "<IBBHQQ")}
ELF_MAX = 64 << 20


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


def elf_defined_functions(data):
    """Names a little-endian ELF shared object defines as GLOBAL or WEAK functions in .dynsym, or None
    when it is not one or a table lies outside the file. Nothing is loaded or run. The same rules as
    src/tc002/ElfSymbols.cpp, which the update helper applies on the clock."""
    size = len(data)
    if not 16 <= size <= ELF_MAX or data[:4] != b"\x7fELF" or data[5] != 1 or data[4] not in ELF_LAYOUTS:
        return None
    ehdr, shdr, sym = (struct.Struct(layout) for layout in ELF_LAYOUTS[data[4]])

    def inside(offset, length):
        return offset <= size and length <= size - offset
    if size < ehdr.size:
        return None
    header = ehdr.unpack_from(data)
    e_type, e_shoff, e_shentsize, e_shnum = header[1], header[6], header[11], header[12]
    if e_type != 3 or e_shentsize != shdr.size or e_shnum == 0 or not inside(e_shoff, e_shnum * shdr.size):
        return None
    # Section header fields: name, type, flags, addr, offset, size, link, info, addralign, entsize.
    sections = [shdr.unpack_from(data, e_shoff + i * shdr.size) for i in range(e_shnum)]
    dynsym = next((s for s in sections if s[1] == 11), None)  # SHT_DYNSYM
    if (dynsym is None or dynsym[9] != sym.size or dynsym[5] % sym.size or not inside(dynsym[4], dynsym[5])
            or dynsym[6] >= e_shnum):
        return None
    strtab = sections[dynsym[6]]
    if strtab[1] != 3 or not inside(strtab[4], strtab[5]):  # SHT_STRTAB
        return None
    strings = data[strtab[4]:strtab[4] + strtab[5]]
    defined = set()
    for offset in range(dynsym[4] + sym.size, dynsym[4] + dynsym[5], sym.size):  # entry 0 is the null symbol
        fields = sym.unpack_from(data, offset)
        name, info, shndx = (fields[0], fields[3], fields[5]) if data[4] == 1 else (fields[0], fields[1], fields[3])
        end = strings.find(b"\0", name) if name < len(strings) else -1
        if end < 0:
            return None
        if info >> 4 in (1, 2) and info & 0xF == 2 and shndx != 0:  # GLOBAL or WEAK, FUNC, defined
            defined.add(strings[name:end].decode("latin-1"))
    return defined


def check_vendor_file(entry, path):
    """verified: a recorded hash. compatible: no recorded hash, but it defines every function the entry
    requires (only the vendor application lists any). UNKNOWN BUILD: neither."""
    data = path.read_bytes() if path.is_file() else None
    digest = hashlib.sha256(data).hexdigest() if data is not None else ""
    required = entry.get("symbols") or []
    defined = elf_defined_functions(data) if required and data is not None else None
    missing = [name for name in required if defined is None or name not in defined]
    if digest and digest in entry["sha256"]:
        status = "verified"
    elif required and not missing:
        status = "compatible"
    else:
        status = "UNKNOWN BUILD"
    return {"status": status, "sha256": digest, "required": required, "missing": missing,
            "readable": defined is not None, "exists": data is not None}


def listed(names):
    return names[0] if len(names) == 1 else ", ".join(names[:-1]) + " and " + names[-1]


def why_unusable(result):
    if not result["exists"]:
        return "it is missing from the partition"
    if not result["readable"]:
        return "it is not a readable ELF shared library"
    return "it does not define " + listed(result["missing"])


def report(name, entry, result):
    status, digest = result["status"], result["sha256"]
    line = f"  {name:24s} {status:14s} {digest[:16] if status == 'verified' else digest or '(no file)'}"
    if status == "verified" and result["required"]:
        line += ("  defines the launcher's entry points" if not result["missing"]
                 else f"  (symbol check disagrees: {why_unusable(result)})")
    say(line)
    indent = " " * 27
    if status == "compatible":
        say(f"{indent}not a recorded build, but it defines {listed(result['required'])}: everything the launcher calls")
    elif status == "UNKNOWN BUILD":
        if result["required"]:
            say(f"{indent}{why_unusable(result)}")
        say(f"{indent}{entry['purpose']}")


def verify_vendor(clock, dump, work):
    """Check the vendor files the port depends on against vendor-fingerprints.json; {name: result}."""
    recorded = fingerprints()
    say(f"verifying the clock's stock firmware (this port was verified on app {recorded['stock']['app']}, "
        f"MCU {recorded['stock']['mcu']})")
    results = {}
    with tempfile.TemporaryDirectory(prefix="tc002-verify-") as tmp:
        tree = Path(tmp) / "res"
        subprocess.run(["unsquashfs", "-no-progress", "-d", str(tree), str(dump)], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for name, entry in recorded["libraries"].items():
            if name == VENDOR_APP:
                local = tree / (image.VENDOR_KEPT_AS if image.has_port_installed(tree) else image.VENDOR_GUI)
            elif entry.get("path"):
                local = Path(tmp) / name
                clock.run("pull", entry["path"], str(local))
            else:
                continue
            results[name] = check_vendor_file(entry, local)
    for name, result in results.items():
        report(name, recorded["libraries"][name], result)
    return results


def vendor_decision(results, allow_unverified):
    """Whether the update helper must be told --force; raises Fail when the install must not go ahead."""
    app = results.get(VENDOR_APP)
    if app and app["status"] == "UNKNOWN BUILD":
        raise Fail(f"the stock application on this clock is not a build this port knows, and {why_unusable(app)}.\n"
                   "The launcher hands over to AWTRIX through its entry points, and falls back to them when the knob is\n"
                   "held at power-on or AWTRIX fails to start three times. Without them, a clock where AWTRIX does not\n"
                   "start could only be recovered with Ulanzi's reset-button factory restore or a serial console,\n"
                   "neither of which this project has tried.\n"
                   "Nothing was written, and --allow-unverified does not change this. Please report the SHA-256 above\n"
                   f"with the app and MCU versions from the clock's stock web page: {ISSUES}")
    recorded = fingerprints()["libraries"]
    unknown = [f"  {name}: {recorded[name]['purpose']}" for name, result in results.items()
               if result["status"] == "UNKNOWN BUILD"]
    if not unknown:
        return False
    if not allow_unverified:
        raise Fail("this clock has vendor files this port was not verified with; nothing was written.\n" +
                   "\n".join(unknown) + "\n"
                   "Re-run with --allow-unverified to install anyway with that left off. The update helper then\n"
                   "runs with --force, and later updates must also go through this installer: the web UI's update\n"
                   "refuses on such a clock.")
    say("  continuing because of --allow-unverified (the update helper runs with --force); on the clock:\n" +
        "\n".join("  " + line for line in unknown))
    return True


def confirm(prompt, word):
    say(prompt)
    try:
        stream = open("/dev/tty") if not sys.stdin.isatty() and os.path.exists("/dev/tty") else sys.stdin
    except OSError:
        stream = sys.stdin
    answer = stream.readline().strip()
    return answer == word


def helper_command(mode, force):
    # The helper only takes --force after the image.
    return f"{HELPER} {mode} {IMAGE_ON_CLOCK}" + (" --force" if force else "")


def flash(clock, helper, img, expected_version, yes, what, force=False):
    say(f"pushing the update helper and {what} to the clock")
    clock.run("push", str(helper), HELPER)
    clock.run("push", str(img), IMAGE_ON_CLOCK)
    clock.shell(f"chmod 700 {HELPER}")
    say("running the helper's preflight (nothing is written)")
    out = clock.run("shell", helper_command("--preflight", force) + ' 2>&1; echo "exit=$?"').replace("\r", "")
    say("  " + out.strip().replace("\n", "\n  "))
    if "exit=0" not in out:
        raise Fail("preflight refused; nothing was written")
    say("\nThe clock's application partition will now be erased and rewritten in place. There is no\n"
        "second copy on the clock. Keep it on USB power and do not unplug it until it has rebooted.\n"
        "If it does not come back: hold the knob while powering on to start the stock app.")
    if not yes and not confirm("Type  flash  to continue, anything else to stop:", "flash"):
        raise Fail("stopped before writing anything")
    say("installing; the clock stops responding for one to two minutes and then reboots")
    try:
        clock.run("shell", helper_command("--install", force), check=False, timeout=120)
    except subprocess.TimeoutExpired:
        pass  # the connection can hang while the clock reboots; wait_for decides
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
    p.add_argument("--restore", action="store_true",
                   help="flash the recovery image from the latest run (the update helper runs with --force: it is "
                        "this clock's own stock partition)")
    p.add_argument("--build-only", action="store_true", help="build the images and stop")
    p.add_argument("--yes", action="store_true", help="do not ask for the typed confirmation")
    p.add_argument("--allow-unverified", action="store_true",
                   help="install even if libmi_ao.so or libzknet.so is not a build this port was verified with; "
                        "what needs it stays off (audio, or DHCP after the first lease, static addressing and the "
                        "fallback access point) and the update helper runs with --force. A stock application "
                        "without the launcher's entry points is refused regardless")
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
        say("  the update helper runs with --force: restore-stock.img is this clock's own stock partition, and\n"
            "  the fingerprint check protects the port's calls into vendor code, which the stock app does not make")
        flash(clock, bundle_paths()[0] / "tc002-update", img, None, a.yes, "the recovery image", force=True)
        return
    check_clock(clock)
    run_dir = clock_dir / datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    run_dir.mkdir(parents=True)
    dump = run_dir / "res-partition.bin"
    live = dump_partition(clock, dump)
    force = vendor_decision(verify_vendor(clock, dump, a.work), a.allow_unverified)
    build, webui, cacert = bundle_paths()
    say("building update.img and restore-stock.img from your clock's partition")
    built = image.build_images(live, build, webui, cacert, run_dir, version)
    say(f"  update.img        {built['update.img']['bytes']} bytes")
    say(f"  restore-stock.img {built['restore-stock.img']['bytes']} bytes  (keep this: it puts the stock app back)")
    say(f"  kept under {run_dir}")
    if a.build_only:
        return
    helper = build / "tc002-update"
    if flash(clock, helper, run_dir / "update.img", version, a.yes, "update.img", force=force):
        say(f"\nDone. Web UI: http://{clock.host}/   Restore stock later with:\n"
            f"  {sys.argv[0]} {clock.host} --restore")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        raise SystemExit("\nstopped")
