"""Build TC002 ZKSWE res-only images from a clock's own partition. Never writes to a device.

Library use (the installer) and CLI use (a developer) share build_images(). The container
header is synthesized from the recorded platform constants; only this port's own updater ever
reads it. Layout reference: qzz0518/ulanzi-tc002-market-clock's ZKSWE format research; all
parsing, bounds checks and packaging here are implemented independently.
"""
import argparse
import hashlib
import json
import re
import shutil
import struct
import subprocess
import tempfile
import zlib
from pathlib import Path

MAX_RES = 0x800000
HEADER_SIZE = 572
ROOT = Path(__file__).resolve().parents[1]
# The vendor application, as it is named in stock and as this port keeps it after installing.
VENDOR_GUI = "lib/libzkgui.so"
VENDOR_KEPT_AS = "lib/libulanzi-bootstrap.so"
PORT_FILES = ("bin/awtrix-tc002", "bin/tc002-update", "ui/awtrix.html", "etc/cacert.pem")


def firmware_version():
    """The one version string, from CMakeLists.txt; the manifest must never drift from the binary."""
    return re.search(r'AWTRIX_NG_VERSION="([^"]+)"', (ROOT / "CMakeLists.txt").read_text())[1]


def manifest_status(validated):
    """A release only stops being a candidate once the cold-boot protocol in docs/VALIDATION.md ran."""
    if validated:
        return f"cold-boot validated on {validated} per docs/VALIDATION.md"
    return "release-candidate; not cold-boot validated"


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


def synthesized_header():
    """A container header carrying the TC002 platform constants recorded from a stock image."""
    header = bytearray(HEADER_SIZE)
    header[:24] = bytes.fromhex("5a4b53574556312e302d313830313237300130230310606c")
    struct.pack_into("<I", header, 48, 524)
    header[52] = 2
    struct.pack_into("<I", header, 53, 0xAA550606)
    return bytes(header)


def pack(payload, stock=None):
    """Wrap a squashfs in a container. `stock` may supply a real header as the template."""
    if stock is not None:
        unpack(stock)  # Reject every unknown model/container layout before touching its header.
    if len(payload) > MAX_RES or len(payload) < 96 or payload[:4] != b"hsqs":
        raise ValueError("invalid res filesystem")
    used = struct.unpack_from("<Q", payload, 40)[0]
    if not 96 <= used <= len(payload):
        raise ValueError("truncated res filesystem")
    size = (used + 4095) // 4096 * 4096
    payload = payload[:used].ljust(size, b"\x00")
    header = bytearray(stock[:HEADER_SIZE] if stock is not None else synthesized_header())
    struct.pack_into("<II", header, 24, HEADER_SIZE, len(payload))
    header[32:48] = payload[:16]
    struct.pack_into("<I", header, 568, zlib.crc32(header[:568]))
    result = bytes(header) + hashlib.md5(payload).digest() + payload[16:]
    assert unpack(result) == payload
    return result


def squashfs_params(fs):
    """Compressor name and block size of a squashfs, so a rebuilt image matches the original."""
    compression = {1: "gzip", 4: "xz", 5: "lz4", 6: "zstd"}.get(struct.unpack_from("<H", fs, 20)[0])
    if not compression:
        raise ValueError("unsupported stock squashfs compression")
    return compression, struct.unpack_from("<I", fs, 12)[0]


def has_port_installed(tree):
    return (tree / VENDOR_KEPT_AS).exists()


def restore_stock_tree(tree):
    """Turn a res tree that already carries this port back into the vendor's layout, in place."""
    if not has_port_installed(tree):
        return False
    (tree / VENDOR_KEPT_AS).replace(tree / VENDOR_GUI)
    for relative in PORT_FILES:
        path = tree / relative
        if path.exists():
            path.unlink()
    config_path = tree / "etc/EasyUI.cfg"
    config = json.loads(config_path.read_text())
    config["startupLibPath"] = "/res/lib/libzkgui.so"
    config_path.write_text(json.dumps(config, indent=2))
    return True


def install_port_tree(tree, build, webui, cacert):
    """Put this port into a stock res tree, in place, keeping the vendor application beside it."""
    if has_port_installed(tree):
        raise ValueError("tree already carries the port; restore it to stock first")
    for folder in (tree, tree / "lib", tree / "bin", tree / "ui", tree / "etc"):
        folder.mkdir(exist_ok=True)
        folder.chmod(0o750)
    original = tree / VENDOR_GUI
    original.rename(tree / VENDOR_KEPT_AS)
    shutil.copyfile(build / "libzkgui.so", original)
    shutil.copyfile(build / "awtrix-tc002", tree / "bin/awtrix-tc002")
    shutil.copyfile(build / "tc002-update", tree / "bin/tc002-update")
    shutil.copyfile(webui, tree / "ui/awtrix.html")
    shutil.copyfile(cacert, tree / "etc/cacert.pem")
    for path in (original, tree / "bin/awtrix-tc002", tree / "bin/tc002-update"):
        path.chmod(0o750)
    config_path = tree / "etc/EasyUI.cfg"
    config = json.loads(config_path.read_text())
    config["startupLibPath"] = "/res/lib/libzkgui.so"
    config_path.chmod(0o640)
    config_path.write_text(json.dumps(config, indent=2))


def mksquashfs(tree, out, compression, block):
    subprocess.run(["mksquashfs", str(tree), str(out), "-noappend", "-comp", compression,
                    "-b", str(block), "-no-progress", "-processors", "2", "-all-root"], check=True,
                   stdout=subprocess.DEVNULL)
    return out.read_bytes()


def build_images(live, build, webui, cacert, output, version, stock=None, validated=None):
    """From the clock's live res partition: restore-stock.img, update.img and manifest.json.

    `live` may be a pristine stock partition or one that already carries this port; either way
    restore-stock.img is the vendor layout and update.img is this build on top of it.
    """
    output.mkdir(parents=True, exist_ok=True)
    compression, block = squashfs_params(live)
    with tempfile.TemporaryDirectory(prefix="tc002-image-") as tmp:
        tree = Path(tmp) / "res"
        source = Path(tmp) / "live.squashfs"
        source.write_bytes(live)
        subprocess.run(["unsquashfs", "-no-progress", "-d", str(tree), str(source)], check=True,
                       stdout=subprocess.DEVNULL)
        if restore_stock_tree(tree):
            # The clock already ran this port: rebuild the vendor layout from what it kept.
            stock_fs = mksquashfs(tree, Path(tmp) / "stock.squashfs", compression, block)
        else:
            # Restore the CURRENT live partition byte for byte, not a potentially older stock update.
            stock_fs = live
        (output / "restore-stock.img").write_bytes(pack(stock_fs, stock))
        install_port_tree(tree, build, webui, cacert)
        image = pack(mksquashfs(tree, Path(tmp) / "res.squashfs", compression, block), stock)
        (output / "update.img").write_bytes(image)
    manifest = {}
    for name in ("update.img", "restore-stock.img"):
        data = (output / name).read_bytes()
        manifest[name] = {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(), "partition": "res"}
    manifest["matrix"] = {"width": 52, "height": 16}
    manifest["version"] = version
    manifest["tls"] = "OpenSSL 3.5.8"
    manifest["status"] = manifest_status(validated)
    for name in ("awtrix-tc002", "libzkgui.so", "tc002-update"):
        data = (build / name).read_bytes()
        manifest["bin/" + name] = {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return manifest


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--live-res", type=Path, required=True, help="raw dump of the clock's res partition")
    p.add_argument("--stock", type=Path, help="optional stock ZKSWE image whose header is used as the template")
    p.add_argument("--build", type=Path, default=Path("build-tc002"))
    p.add_argument("--webui", type=Path, default=ROOT / "build-webui/index.html")
    p.add_argument("--cacert", type=Path, default=ROOT / "assets/cacert.pem")
    p.add_argument("--output", type=Path, default=Path("dist"))
    p.add_argument("--validated", metavar="YYYY-MM-DD",
                   help="date the cold-boot validation protocol was completed on this exact build")
    a = p.parse_args()
    stock = a.stock.read_bytes() if a.stock else None
    manifest = build_images(a.live_res.read_bytes(), a.build, a.webui, a.cacert, a.output,
                            firmware_version(), stock, a.validated)
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
