#!/usr/bin/env bash
# Downloads Google's platform-tools (adb) into build-deps/, where tools/paths.py looks for it.
# The archive is Google's rolling "latest" build, so its hash is printed rather than pinned.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build-deps
archive=build-deps/platform-tools-latest-linux.zip
curl -fL --retry 3 -o "$archive" https://dl.google.com/android/repository/platform-tools-latest-linux.zip
sha256sum "$archive"
rm -rf build-deps/platform-tools
unzip -q "$archive" -d build-deps
build-deps/platform-tools/adb version
