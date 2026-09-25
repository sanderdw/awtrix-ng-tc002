#!/bin/sh
# AWTRIX NG TC002 one-line installer. Downloads a released installer bundle, verifies its
# checksum, and runs its install.py, which builds the firmware image from your own clock.
#
#   curl -fsSL https://raw.githubusercontent.com/sanderdw/awtrix-ng-tc002/main/install.sh | sh -s -- CLOCK_IP
#
# The default is the newest stable release (not a pre-release) with an installer bundle.
# --version TAG (or AWTRIX_TC002_VERSION=TAG) installs that release instead, pre-releases included:
#
#   curl -fsSL https://raw.githubusercontent.com/sanderdw/awtrix-ng-tc002/main/install.sh | sh -s -- --version v1.1.2-tc002.2 CLOCK_IP
#
# Other options are passed to install.py (--build-only, --restore, --yes).
set -eu
REPO="sanderdw/awtrix-ng-tc002"
NAME="awtrix-ng-tc002-installer"
HOME_DIR="${HOME:-/tmp}/.awtrix-ng-tc002"
need() { command -v "$1" >/dev/null 2>&1 || { echo "error: $1 is required" >&2; exit 1; }; }
need curl; need python3; need unzip
tag="${AWTRIX_TC002_VERSION:-}"
# Take --version out of the arguments; everything else goes to install.py in its original order.
n=$#
while [ "$n" -gt 0 ]; do
  arg=$1; shift; n=$((n - 1))
  case "$arg" in
    --version)
      [ "$n" -gt 0 ] || { echo "error: --version needs a release tag, e.g. v1.1.2-tc002.2" >&2; exit 2; }
      tag=$1; shift; n=$((n - 1)) ;;
    --version=*) tag=${arg#--version=} ;;
    *) set -- "$@" "$arg" ;;
  esac
done
if [ "$#" -lt 1 ]; then
  echo "usage: install.sh [--version TAG] CLOCK_IP [install.py options]" >&2; exit 2
fi
if [ -n "$tag" ]; then
  case "$tag" in v*) ;; *) tag="v$tag" ;; esac
  kind="requested"
else
  tag=$(curl -fsSL "https://api.github.com/repos/$REPO/releases?per_page=50" | python3 -c '
import json,sys
for r in json.load(sys.stdin):
    if r.get("draft") or r.get("prerelease"): continue
    if any(a["name"]=="'"$NAME"'.zip" for a in r.get("assets",[])):
        print(r["tag_name"]); break')
  [ -n "$tag" ] || { echo "error: no stable release with an installer bundle found; pick one with --version TAG" >&2; exit 1; }
  kind="stable"
fi
dir="$HOME_DIR/installer/$tag"
mkdir -p "$dir"
base="https://github.com/$REPO/releases/download/$tag"
echo "downloading $NAME $tag ($kind)"
curl -fsSL -o "$dir/$NAME.zip" "$base/$NAME.zip" ||
  { echo "error: release $tag has no installer bundle; see https://github.com/$REPO/releases" >&2; exit 1; }
curl -fsSL -o "$dir/$NAME.zip.sha256" "$base/$NAME.zip.sha256"
cd "$dir"
if command -v sha256sum >/dev/null 2>&1; then sha256sum -c "$NAME.zip.sha256" >/dev/null
else shasum -a 256 -c "$NAME.zip.sha256" >/dev/null; fi
rm -rf "$NAME" && unzip -q "$NAME.zip"
# Piped through sh, stdin is the script itself; take the confirmation from the terminal when
# there is one. Without a terminal, install.py still runs (use --yes or --build-only).
if [ -t 0 ] || ! ( : </dev/tty ) 2>/dev/null; then exec python3 "$dir/$NAME/install.py" "$@"
else exec python3 "$dir/$NAME/install.py" "$@" </dev/tty; fi
