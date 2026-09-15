#!/usr/bin/env bash
set -euo pipefail
: "${TC002_TOOLCHAIN:?Set TC002_TOOLCHAIN to the Arm GNU 9.2-2019.12 toolchain directory}"
TC002_TOOLCHAIN=$(realpath "$TC002_TOOLCHAIN")
tls_dir=$(realpath -m "${1:-build-deps/openssl}")
mkdir -p "$tls_dir"
archive="$tls_dir/openssl-3.5.8.tar.gz"
if [[ ! -f "$archive" ]]; then
  curl -fL --retry 3 https://github.com/openssl/openssl/releases/download/openssl-3.5.8/openssl-3.5.8.tar.gz -o "$archive"
fi
printf '%s  %s\n' a8f84a39918ec6415ce765d9b429d313ba97b8143169c172e734b9514464f5b2 "$archive" | sha256sum -c -
if [[ ! -f "$tls_dir/Configure" ]]; then
  tar -xzf "$archive" -C "$tls_dir" --strip-components=1
fi
cd "$tls_dir"
perl ./Configure linux-armv4 --cross-compile-prefix="$TC002_TOOLCHAIN/bin/arm-none-linux-gnueabihf-" \
  no-shared no-tests no-apps no-docs no-module no-legacy no-engine no-quic no-comp no-dtls \
  no-ssl3 no-tls1 no-tls1_1 -Os -ffunction-sections -fdata-sections \
  -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard \
  --prefix="$tls_dir/install" --openssldir=/res/etc
make -j"${TC002_JOBS:-4}" build_libs
