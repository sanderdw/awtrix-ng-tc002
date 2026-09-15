"""Validate and pack TC002 ZKSWE res-only images. Never writes to a device.

The platform metadata comes from a validated stock container from YOUR clock.
Layout reference: qzz0518/ulanzi-tc002-market-clock's ZKSWE format research.
All parsing, bounds checks and packaging here are implemented independently.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import zlib

MAX_RES = 0x800000
HEADER_SIZE = 572


def unpack(blob):
    if len(blob) < HEADER_SIZE + 4096:
        raise ValueError("truncated firmware image")
    if blob[:16] != b"ZKSWEV1.0-180127":
        raise ValueError("not a ZKSWE firmware image")
    if tuple(blob[16:19]) != (48, 1, 48) or blob[20] != 3:
        raise ValueError("only a single res-partition image is accepted")
    if struct.unpack_from("<I", blob, 48)[0] != 524 or struct.unpack_from("<I", blob, 53)[0] != 0xAA550606:
        raise ValueError("wrong TC002 platform identifier")
    if zlib.crc32(blob[:568]) != struct.unpack_from("<I", blob, 568)[0]:
        raise ValueError("firmware header CRC mismatch")
    offset, size = struct.unpack_from("<II", blob, 24)
    if offset != HEADER_SIZE or not 4096 <= size <= MAX_RES or size % 4096:
        raise ValueError("invalid res image bounds")
    if len(blob) != offset + size:
        raise ValueError("image length mismatch")
    payload = blob[32:48] + blob[offset+16:]
    if payload[:4] != b"hsqs" or hashlib.md5(payload).digest() != blob[offset:offset+16]:
        raise ValueError("res payload MD5 or squashfs signature mismatch")
    used = struct.unpack_from("<Q", payload, 40)[0]
    if not 96 <= used <= size:
        raise ValueError("invalid squashfs length")
    return payload


def pack(payload, stock):
    unpack(stock)  # Reject every unknown model/container layout before touching its header.
    if len(payload) > MAX_RES or len(payload) < 96 or payload[:4] != b"hsqs":
        raise ValueError("invalid res filesystem")
    used = struct.unpack_from("<Q", payload, 40)[0]
    if not 96 <= used <= len(payload):
        raise ValueError("truncated res filesystem")
    size = (used + 4095) // 4096 * 4096
    payload = payload[:used].ljust(size, b"\x00")
    header = bytearray(stock[:HEADER_SIZE])
    struct.pack_into("<II", header, 24, HEADER_SIZE, len(payload))
    header[32:48] = payload[:16]
    struct.pack_into("<I", header, 568, zlib.crc32(header[:568]))
    result = bytes(header) + hashlib.md5(payload).digest() + payload[16:]
    assert unpack(result) == payload
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--stock", type=Path, required=True)
    p.add_argument("--live-res", type=Path, required=True)
    p.add_argument("--build", type=Path, default=Path("build-tc002"))
    p.add_argument("--output", type=Path, default=Path("dist"))
    a = p.parse_args()
    stock, live = a.stock.read_bytes(), a.live_res.read_bytes()
    unpack(stock)
    a.output.mkdir(parents=True, exist_ok=True)
    # Restore the CURRENT live partition, not the potentially older stock update.
    recovery = pack(live, stock)
    (a.output/"restore-stock.img").write_bytes(recovery)
    with tempfile.TemporaryDirectory(prefix="tc002-image-") as tmp:
        tree = Path(tmp)/"res"
        subprocess.run(["unsquashfs", "-no-progress", "-d", str(tree), str(a.live_res)], check=True,
                       stdout=subprocess.DEVNULL)
        for folder in (tree, tree/"lib", tree/"bin", tree/"ui", tree/"etc"):
            folder.chmod(0o750)
        original = tree/"lib/libzkgui.so"
        original.rename(tree/"lib/libulanzi-bootstrap.so")
        shutil.copyfile(a.build/"libzkgui.so", original)
        shutil.copyfile(a.build/"awtrix-tc002", tree/"bin/awtrix-tc002")
        shutil.copyfile(a.build/"tc002-update", tree/"bin/tc002-update")
        shutil.copyfile("upstream/awtrix-ng/webui/index.html", tree/"ui/awtrix.html")
        shutil.copyfile("assets/cacert.pem", tree/"etc/cacert.pem")
        for path in (original, tree/"bin/awtrix-tc002", tree/"bin/tc002-update"):
            path.chmod(0o750)
        config_path = tree/"etc/EasyUI.cfg"
        config = json.loads(config_path.read_text())
        config["startupLibPath"] = "/res/lib/libzkgui.so"
        config_path.chmod(0o640)
        config_path.write_text(json.dumps(config, indent=2))
        fs = Path(tmp)/"res.squashfs"
        # Match the current partition's compressor and block size; preserve its UID/GID.
        compression = {1: "gzip", 4: "xz", 5: "lz4", 6: "zstd"}.get(struct.unpack_from("<H", live, 20)[0])
        if not compression:
            raise ValueError("unsupported stock squashfs compression")
        block = struct.unpack_from("<I", live, 12)[0]
        subprocess.run(["mksquashfs", str(tree), str(fs), "-noappend", "-comp", compression,
            "-b", str(block), "-no-progress", "-processors", "2", "-all-root"], check=True,
            stdout=subprocess.DEVNULL)
        image = pack(fs.read_bytes(), stock)
        (a.output/"update.img").write_bytes(image)
    manifest = {}
    for name in ("update.img", "restore-stock.img"):
        data = (a.output/name).read_bytes()
        manifest[name] = {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(), "partition": "res"}
    manifest["upstreamCommit"] = "4ff1de83428ed13cae6e210dfcbf9d186a09bc60"
    manifest["matrix"] = {"width": 52, "height": 16}
    manifest["version"] = "1.1.0-tc002.5"
    manifest["tls"] = "OpenSSL 3.5.8"
    manifest["status"] = "release-candidate; not cold-boot validated"
    for name in ("awtrix-tc002", "libzkgui.so", "tc002-update"):
        data = (a.build/name).read_bytes()
        manifest["bin/"+name] = {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
    (a.output/"manifest.json").write_text(json.dumps(manifest, indent=2)+"\n")
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
