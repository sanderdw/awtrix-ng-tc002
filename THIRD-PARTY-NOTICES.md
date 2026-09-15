# Attribution and source provenance

This is an unofficial TC002 platform port of **AWTRIX NG 1.1.0**, by Blueforcer
and its contributors, at commit `4ff1de83428ed13cae6e210dfcbf9d186a09bc60`:
https://github.com/Blueforcer/awtrix-ng

The application and this port are distributed under PolyForm Noncommercial 1.0.0;
see LICENSE.md. Preserve upstream's notices and the component licenses in
`upstream/awtrix-ng/THIRD-PARTY-NOTICES.md`, `upstream/awtrix-ng/LICENSES`, and
individual vendor directories. This is not an official Ulanzi or Blueforcer release.

Additional dependencies:

- OpenSSL 3.5.8, Apache-2.0; official source and license documented in
  `vendor/openssl`. Built statically by `tools/build_tls.sh`.
- Mozilla CA certificate collection distributed by curl, `assets/cacert.pem`:
  https://curl.se/docs/caextract.html. Certificate/source information and license
  notices are included in the bundle; Mozilla certificate data is MPL-2.0.
- PubSubClient and Base64, as documented in their `vendor` directories.
- Berry, TJpg_Decoder and other upstream dependencies retain their original
  licenses and copyright notices.

Hardware references:

- Official TC002 documentation and FlyThings SDK headers:
  https://github.com/UlanziTechnology/Ulanzi-U-Clock-TC002
- Installed TC002 1.1.1 binaries were inspected to determine GPIO/SPI, UART and
  audio ABI behavior. The hardware adapters are independently written. Proprietary
  device libraries are dynamically loaded from the clock, not included in this
  source tree.
- ZKSWE format research by qzz0518/ulanzi-tc002-market-clock informed the container
  investigation. The parser, packer, driver and updater here are independent
  implementations; no GPL application code was copied from that project.

`device-private/` and locally generated firmware images contain a copy of the
owner's stock filesystem. They are personal installation/recovery artifacts, not
part of a public source distribution. The package preserves the original vendor
application as `libulanzi-bootstrap.so` for Wi-Fi initialization and fallback.
