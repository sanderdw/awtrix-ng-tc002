#!/bin/sh
# AWTRIX NG TC002 one-line installer. Downloads a released installer bundle, verifies its
# checksum, and runs its install.py, which builds the firmware image from your own clock.
#
#   curl -fsSL https://raw.githubusercontent.com/sanderdw/awtrix-ng-tc002/main/install.sh | sh -s -- CLOCK_IP
#
# Options after CLOCK_IP are passed to install.py (--build-only, --restore, --yes).
# AWTRIX_TC002_VERSION=v1.1.1-tc002.5 pins a release; the default is the newest with a bundle.
set -eu
REPO="sanderdw/awtrix-ng-tc002"
NAME="awtrix-ng-tc002-installer"
HOME_DIR="${HOME:-/tmp}/.awtrix-ng-tc002"
need() { command -v "$1" >/dev/null 2>&1 || { echo "error: $1 is required" >&2; exit 1; }; }
need curl; need python3; need unzip
if [ "$#" -lt 1 ]; then
  echo "usage: install.sh CLOCK_IP [install.py options]" >&2; exit 2
fi
tag="${AWTRIX_TC002_VERSION:-}"
if [ -z "$tag" ]; then
  tag=$(curl -fsSL "https://api.github.com/repos/$REPO/releases?per_page=20" | python3 -c '
import json,sys
for r in json.load(sys.stdin):
    if any(a["name"]=="'"$NAME"'.zip" for a in r.get("assets",[])):
        print(r["tag_name"]); break')
  [ -n "$tag" ] || { echo "error: no release with an installer bundle found" >&2; exit 1; }
fi
dir="$HOME_DIR/installer/$tag"
mkdir -p "$dir"
base="https://github.com/$REPO/releases/download/$tag"
echo "downloading $NAME $tag"
curl -fsSL -o "$dir/$NAME.zip" "$base/$NAME.zip"
curl -fsSL -o "$dir/$NAME.zip.sha256" "$base/$NAME.zip.sha256"
cd "$dir"
if command -v sha256sum >/dev/null 2>&1; then sha256sum -c "$NAME.zip.sha256" >/dev/null
else shasum -a 256 -c "$NAME.zip.sha256" >/dev/null; fi
rm -rf "$NAME" && unzip -q "$NAME.zip"
if [ -t 0 ]; then exec python3 "$dir/$NAME/install.py" "$@"
else exec python3 "$dir/$NAME/install.py" "$@" </dev/tty; fi
