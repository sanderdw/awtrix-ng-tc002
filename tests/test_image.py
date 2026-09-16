"""Firmware container corruption and boundary checks; no device or vendor payload required."""
import hashlib
import importlib.util
from pathlib import Path
import struct
import zlib
import pytest

spec = importlib.util.spec_from_file_location("tc002_image", Path(__file__).parents[1]/"tools/image.py")
image = importlib.util.module_from_spec(spec)
spec.loader.exec_module(image)


def sample():
    payload = bytearray(4096)
    payload[:4] = b"hsqs"
    struct.pack_into("<Q", payload, 40, 4096)
    header = bytearray(572)
    # Header bytes independently recorded from a stock res-only container.
    header[:24] = bytes.fromhex("5a4b53574556312e302d313830313237300130230310606c")
    struct.pack_into("<II", header, 24, 572, len(payload))
    header[32:48] = payload[:16]
    struct.pack_into("<I", header, 48, 524)
    header[52] = 2
    struct.pack_into("<I", header, 53, 0xAA550606)
    struct.pack_into("<I", header, 568, zlib.crc32(header[:568]))
    return bytes(header)+hashlib.md5(payload).digest()+payload[16:]


def test_lossless_round_trip():
    original = sample()
    assert image.pack(image.unpack(original), original) == original


@pytest.mark.parametrize("offset", [0, 16, 17, 18, 20, 24, 28, 32, 48, 53, 568, 572, 600, 4096])
def test_corruption_rejected(offset):
    blob = bytearray(sample())
    blob[offset] ^= 1
    with pytest.raises(ValueError):
        image.unpack(blob)


@pytest.mark.parametrize("length", [0, 16, 571, 572, 4667, 4669])
def test_exact_container_size(length):
    blob = sample()[:length].ljust(length, b"\x00")
    with pytest.raises(ValueError):
        image.unpack(blob)


def test_wrong_partition_rejected_even_with_valid_crc():
    blob = bytearray(sample())
    blob[20] = 2
    struct.pack_into("<I", blob, 568, zlib.crc32(blob[:568]))
    with pytest.raises(ValueError, match="res-partition"):
        image.unpack(blob)


def test_truncated_and_oversized_filesystem():
    payload = bytearray(image.unpack(sample()))
    struct.pack_into("<Q", payload, 40, 4097)
    with pytest.raises(ValueError, match="truncated"):
        image.pack(payload, sample())
    with pytest.raises(ValueError, match="invalid res"):
        image.pack(bytes(image.MAX_RES+1), sample())

@pytest.mark.parametrize("offset", [None, 0, 16, 17, 18, 20, 24, 28, 32, 48, 53, 568, 572, 600, 4096])
def test_native_updater_validates_same_container(tmp_path, offset):
    import subprocess
    updater = Path(__file__).parents[1]/"build-host/tc002-update"
    blob = bytearray(sample())
    if offset is not None:
        blob[offset] ^= 1
    path = tmp_path/"candidate.img"
    path.write_bytes(blob)
    result = subprocess.run([updater, "--validate", path], capture_output=True)
    assert (result.returncode == 0) == (offset is None), result.stderr


def test_manifest_version_comes_from_cmake_and_status_from_validation():
    import re
    cmake = (Path(__file__).parents[1] / "CMakeLists.txt").read_text()
    assert image.firmware_version() == re.search(r'AWTRIX_NG_VERSION="([^"]+)"', cmake)[1]
    assert image.manifest_status(None) == "release-candidate; not cold-boot validated"
    assert image.manifest_status("2026-09-20").startswith("cold-boot validated on 2026-09-20")
