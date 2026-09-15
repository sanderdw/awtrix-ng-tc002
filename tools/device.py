"""Explicit shell/push/pull through Google platform-tools.

Run: uv run tools/device.py CLOCK_IP shell 'getprop init.svc.zkswe'
Set ADB=/path/to/platform-tools/adb when adb is not on PATH.
The Python adb-shell client is incompatible with TC002 sync sessions.
"""
import argparse
import os
import shutil
import subprocess


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("host")
    p.add_argument("operation", choices=["shell", "push", "pull"])
    p.add_argument("source")
    p.add_argument("destination", nargs="?")
    a = p.parse_args()
    adb = os.environ.get("ADB") or shutil.which("adb")
    if not adb:
        p.error("Install Google platform-tools and set ADB to its adb executable")
    if a.operation != "shell" and not a.destination:
        p.error("push/pull needs a destination")
    serial = a.host if ":" in a.host else a.host+":5555"
    subprocess.run([adb, "connect", serial], check=True)
    command = [adb, "-s", serial, a.operation, a.source]
    if a.destination:
        command.append(a.destination)
    raise SystemExit(subprocess.run(command).returncode)


if __name__ == "__main__":
    main()
