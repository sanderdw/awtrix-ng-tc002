#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
TC002_TOOLCHAIN=$(realpath -m "${TC002_TOOLCHAIN:-build-deps/toolchain}")
export TC002_TOOLCHAIN
if [[ ! -x "$TC002_TOOLCHAIN/bin/arm-none-linux-gnueabihf-g++" ]]; then
  mkdir -p "$TC002_TOOLCHAIN"
  archive="$TC002_TOOLCHAIN/gcc9.tar.xz"
  curl -fL --retry 3 https://developer.arm.com/-/media/Files/downloads/gnu-a/9.2-2019.12/binrel/gcc-arm-9.2-2019.12-x86_64-arm-none-linux-gnueabihf.tar.xz -o "$archive"
  printf '%s  %s\n' 51bbaf22a4d3e7a393264c4ef1e45566701c516274dde19c4892c911caa85617 "$archive" | sha256sum -c -
  tar -xJf "$archive" -C "$TC002_TOOLCHAIN" --strip-components=1
fi
TC002_TLS=$(realpath -m "${TC002_TLS:-build-deps/openssl}")
export TC002_TLS
if [[ ! -f "$TC002_TLS/libssl.a" || ! -f "$TC002_TLS/libcrypto.a" ]]; then
  bash tools/build_tls.sh "$TC002_TLS"
fi
uv sync --locked
uv run cmake -S . -B build-tc002 -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/tc002-arm.cmake \
  -DCMAKE_BUILD_TYPE=MinSizeRel -DBUILD_TESTING=OFF -DTC002_TLS="$TC002_TLS"
uv run cmake --build build-tc002 -j"${TC002_JOBS:-4}"
mkdir -p dist/bin
for binary in awtrix-tc002 tc002-update libzkgui.so; do
  cp "build-tc002/$binary" "dist/bin/$binary"
  "$TC002_TOOLCHAIN/bin/arm-none-linux-gnueabihf-strip" --strip-unneeded "dist/bin/$binary"
done
