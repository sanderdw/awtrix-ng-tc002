"""Fail when upstream's simulator entry point moved past the version main_tc002.cpp was reconciled with.

src/tc002/main_tc002.cpp is a fork of upstream's src/sim/main_sim.cpp. It records the upstream
commit it was last compared against in a `reconciled-with:` comment; after an upstream bump the
two must be compared by hand and the marker updated.
"""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAIN = ROOT / 'src/tc002/main_tc002.cpp'
UPSTREAM = ROOT / 'upstream/awtrix-ng'


def blob(commit):
    return subprocess.check_output(['git', 'rev-parse', f'{commit}:src/sim/main_sim.cpp'],
                                   cwd=UPSTREAM, text=True).strip()


def main():
    match = re.search(r'^// reconciled-with: ([0-9a-f]{40})$', MAIN.read_text(), re.M)
    if not match:
        sys.exit('main_tc002.cpp lacks a reconciled-with marker')
    reconciled = match.group(1)
    head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=UPSTREAM, text=True).strip()
    if blob(reconciled) != blob(head):
        sys.exit(f'upstream src/sim/main_sim.cpp changed between {reconciled[:12]} and {head[:12]}; '
                 'carry the change into src/tc002/main_tc002.cpp and update its reconciled-with marker')
    print(f'main_tc002.cpp is reconciled with upstream {head[:12]}')


if __name__ == '__main__':
    main()
