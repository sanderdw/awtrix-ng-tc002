"""The stock-firmware gate: the vendor application passes by its recorded hash or by defining the
launcher's entry points, the other vendor libraries only by hash, and the installer forwards the
operator's override to the update helper. No clock needed."""
import hashlib
import importlib.util
import json
import os
import random
import shutil
import struct
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build-host"
FINGERPRINTS = json.loads((ROOT / "src/tc002/vendor-fingerprints.json").read_text())
LAUNCHER = ["onEasyUIInit", "onEasyUIDeinit", "onStartupApp", "_ZN4base13wifiOnAndWaitEi"]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


install = load("tc002_install", ROOT / "tools/install.py")
vendor_fingerprints = load("tc002_vendor_fingerprints", ROOT / "tools/vendor_fingerprints.py")


def built(path):
    """A host build artifact; CI must have it, a local run without a build skips."""
    if not path.exists():
        if os.environ.get("CI"):
            pytest.fail(f"{path} is missing; build build-host first")
        pytest.skip(f"{path} is missing; build build-host first")
    return path


def fixture(variant):
    return built(BUILD / f"elf-fixtures/vendor-app-{variant}.so")


# --- a minimal shared object: header, .dynstr, .dynsym and three section headers ----------------------

GLOBAL, WEAK, LOCAL = 1, 2, 0
FUNC, OBJECT, NOTYPE = 2, 1, 0


def make_elf(bits, symbols, *, order="<", e_type=3, shentsize=None, dynsym_entsize=None, dynsym_link=2,
             strtab_type=3, terminated=True, bad_name=False):
    """symbols: (name, bind, type, section index); section 0 means undefined."""
    ehdr, shdr, sym = (install.ELF_LAYOUTS[1 if bits == 32 else 2][i].replace("<", order) for i in range(3))
    ehdr_size, shdr_size, sym_size = (struct.calcsize(f) for f in (ehdr, shdr, sym))
    strings, offsets = bytearray(b"\0"), []
    for name, *_ in symbols:
        offsets.append(len(strings))
        strings += name.encode() + b"\0"
    if not terminated:
        strings = strings[:-1]
    table = bytearray(sym_size)  # the null symbol
    for i, (_, bind, kind, section) in enumerate(symbols):
        name = len(strings) + 5 if bad_name and i == len(symbols) - 1 else offsets[i]
        info = bind << 4 | kind
        table += (struct.pack(sym, name, 0, 0, info, 0, section) if bits == 32
                  else struct.pack(sym, name, info, 0, section, 0, 0))
    strings_at = ehdr_size
    table_at = strings_at + len(strings) + (-(strings_at + len(strings)) % 8)
    sections_at = table_at + len(table)
    sections = bytes(shdr_size)
    sections += struct.pack(shdr, 0, 11, 2, 0, table_at, len(table), dynsym_link, 1, 8,
                            sym_size if dynsym_entsize is None else dynsym_entsize)
    sections += struct.pack(shdr, 0, strtab_type, 2, 0, strings_at, len(strings), 0, 0, 1, 0)
    ident = b"\x7fELF" + bytes([1 if bits == 32 else 2, 1 if order == "<" else 2, 1]) + bytes(9)
    header = struct.pack(ehdr, ident, e_type, 40, 1, 0, 0, sections_at, 0, ehdr_size, 0, 0,
                         shdr_size if shentsize is None else shentsize, 3, 0)
    blob = bytearray(header) + strings
    blob += bytes(table_at - len(blob)) + table + sections
    return blob


def launcher_symbols(**deinit):
    kind = {"bind": GLOBAL, "type": FUNC, "section": 1}
    kind.update(deinit)
    return [("onEasyUIInit", GLOBAL, FUNC, 1), ("onEasyUIDeinit", kind["bind"], kind["type"], kind["section"]),
            ("onStartupApp", GLOBAL, FUNC, 1), ("_ZN4base13wifiOnAndWaitEi", GLOBAL, FUNC, 1)]


def verdict(data, wanted=LAUNCHER):
    defined = install.elf_defined_functions(bytes(data))
    return "unreadable" if defined is None else "missing:" + ",".join(n for n in wanted if n not in defined)


DAMAGE = [dict(order=">"), dict(e_type=2), dict(shentsize=12), dict(dynsym_entsize=8), dict(dynsym_link=3),
          dict(strtab_type=1), dict(terminated=False), dict(bad_name=True)]
SYMBOL_KINDS = [dict(bind=WEAK), dict(bind=LOCAL), dict(section=0), dict(type=OBJECT), dict(type=NOTYPE)]


# --- JSON and generated header ------------------------------------------------------------------------

def test_installer_reads_the_source_fingerprints_from_a_checkout():
    assert not (ROOT / "tools/vendor-fingerprints.json").exists()
    assert install.fingerprints() == FINGERPRINTS


def test_only_the_vendor_application_may_pass_by_symbols():
    libraries = FINGERPRINTS["libraries"]
    assert libraries[install.VENDOR_APP]["symbols"] == LAUNCHER
    assert [name for name, entry in libraries.items() if "symbols" in entry] == [install.VENDOR_APP]
    assert FINGERPRINTS["stock"] == {"app": "1.1.1", "mcu": "V1.0.17"}


def test_generated_header_lists_files_and_required_functions(tmp_path):
    header = tmp_path / "VendorFingerprints.h"
    vendor_fingerprints.generate(header)
    text = header.read_text()
    assert '  {"libulanzi-bootstrap.so", "/res/lib/libulanzi-bootstrap.so"},\n  {"libmi_ao.so", "/lib/libmi_ao.so"},' in text
    for name in LAUNCHER:
        assert f'  {{"libulanzi-bootstrap.so", "{name}"}},' in text
    assert text.count('{"libmi_ao.so",') == 2  # one fingerprint, one file; never a symbol


@pytest.mark.parametrize("change", [
    lambda d: d["libraries"]["libmi_ao.so"].__setitem__("symbols", ["onEasyUIInit"]),
    lambda d: d["libraries"]["libulanzi-bootstrap.so"].__setitem__("symbols", ['on"EasyUIInit']),
    lambda d: d["libraries"]["libulanzi-bootstrap.so"].__setitem__("symbols", []),
    lambda d: d["libraries"]["libulanzi-bootstrap.so"].__setitem__("symbols", ["onStartupApp", "onStartupApp"]),
])
def test_generator_refuses_symbols_it_cannot_trust(tmp_path, monkeypatch, change):
    data = json.loads(json.dumps(FINGERPRINTS))
    change(data)
    monkeypatch.setattr(vendor_fingerprints, "load", lambda: data)
    with pytest.raises(SystemExit):
        vendor_fingerprints.generate(tmp_path / "VendorFingerprints.h")


# --- the Python reader --------------------------------------------------------------------------------

@pytest.mark.parametrize("bits", [32, 64])
def test_reader_accepts_defined_global_and_weak_functions(bits):
    assert verdict(make_elf(bits, launcher_symbols())) == "missing:"
    assert verdict(make_elf(bits, launcher_symbols(bind=WEAK))) == "missing:"


@pytest.mark.parametrize("bits", [32, 64])
@pytest.mark.parametrize("deinit", SYMBOL_KINDS[1:])
def test_reader_ignores_local_undefined_and_non_functions(bits, deinit):
    assert verdict(make_elf(bits, launcher_symbols(**deinit))) == "missing:onEasyUIDeinit"


@pytest.mark.parametrize("bits", [32, 64])
@pytest.mark.parametrize("damage", DAMAGE)
def test_reader_rejects_what_it_cannot_read_safely(bits, damage):
    assert verdict(make_elf(bits, launcher_symbols(), **damage)) == "unreadable"


@pytest.mark.parametrize("bits", [32, 64])
def test_reader_rejects_every_truncation_and_foreign_classes(bits):
    elf = make_elf(bits, launcher_symbols())
    assert all(verdict(elf[:length]) == "unreadable" for length in range(len(elf)))
    elf[4] = 3
    assert verdict(elf) == "unreadable"
    assert verdict(b"") == verdict(b"\x7fELF") == verdict(bytes(4096)) == "unreadable"


@pytest.mark.parametrize("variant, expected", [
    ("full", "missing:"), ("weak", "missing:"), ("missing", "missing:onEasyUIDeinit"),
    ("undefined", "missing:onEasyUIDeinit"), ("data", "missing:onEasyUIDeinit"),
])
def test_reader_on_compiled_libraries(variant, expected):
    assert verdict(fixture(variant).read_bytes()) == expected


def test_python_and_helper_readers_agree_on_damaged_files(tmp_path):
    """The installer and the update helper must reach the same verdict, or a clock the installer
    approves would be refused on the clock (or the other way round)."""
    query = built(BUILD / "test-elf-symbols")
    rng = random.Random(20260925)
    seeds = [make_elf(32, launcher_symbols()), make_elf(64, launcher_symbols()),
             make_elf(32, launcher_symbols(section=0)), bytearray(fixture("full").read_bytes())]
    # Every targeted case from the tests above, then random damage.
    cases = [make_elf(bits, launcher_symbols(), **damage) for bits in (32, 64) for damage in DAMAGE]
    cases += [make_elf(bits, launcher_symbols(**kind)) for bits in (32, 64) for kind in SYMBOL_KINDS]
    files, expected = [], []
    for i, blob in enumerate(cases):
        path = tmp_path / f"case{i}.so"
        path.write_bytes(blob)
        files.append(str(path))
        expected.append(verdict(blob))
    for i in range(480):
        blob = bytearray(seeds[i % len(seeds)])
        for _ in range(rng.randint(1, 3)):
            choice = rng.random()
            if choice < 0.15:
                del blob[rng.randrange(len(blob)):]
                if not blob:
                    break
            elif choice < 0.55:
                blob[rng.randrange(min(64, len(blob)))] = rng.randrange(256)
            elif choice < 0.8:
                start = max(0, len(blob) - 200)
                blob[rng.randrange(start, len(blob))] = rng.randrange(256)
            else:
                blob[rng.randrange(len(blob))] = rng.randrange(256)
        path = tmp_path / f"{i}.so"
        path.write_bytes(blob)
        files.append(str(path))
        expected.append(verdict(blob))
    result = subprocess.run([query, "--query", ",".join(LAUNCHER), *files], capture_output=True, text=True,
                            check=True)
    assert result.stdout.splitlines() == expected
    # Both outcomes must occur, or the comparison proves nothing.
    assert 50 < expected.count("unreadable") < len(expected) - 50


# --- the installer's decisions ------------------------------------------------------------------------

def vendor_file(tmp_path, blob, name="libzkgui.so"):
    path = tmp_path / name
    path.write_bytes(blob)
    return path


def test_vendor_application_by_hash_or_by_entry_points(tmp_path):
    elf = make_elf(32, launcher_symbols())
    path = vendor_file(tmp_path, elf)
    digest = hashlib.sha256(elf).hexdigest()
    entry = dict(FINGERPRINTS["libraries"][install.VENDOR_APP])
    assert install.check_vendor_file(entry, path)["status"] == "compatible"
    assert install.check_vendor_file(dict(entry, sha256=[digest]), path)["status"] == "verified"
    broken = install.check_vendor_file(entry, vendor_file(tmp_path, make_elf(32, launcher_symbols(section=0))))
    assert broken["status"] == "UNKNOWN BUILD" and broken["missing"] == ["onEasyUIDeinit"]
    absent = install.check_vendor_file(entry, tmp_path / "nothing.so")
    assert absent["status"] == "UNKNOWN BUILD" and not absent["exists"] and absent["missing"] == LAUNCHER


def test_audio_and_network_libraries_never_pass_by_symbols(tmp_path):
    path = vendor_file(tmp_path, make_elf(32, launcher_symbols()))
    for name in ("libmi_ao.so", "libzknet.so"):
        assert install.check_vendor_file(FINGERPRINTS["libraries"][name], path)["status"] == "UNKNOWN BUILD"


def results(app="verified", mi_ao="verified", zknet="verified", missing=()):
    def result(status):
        return {"status": status, "sha256": "ab" * 32, "required": [], "missing": list(missing),
                "readable": True, "exists": True}
    return {install.VENDOR_APP: dict(result(app), required=LAUNCHER), "libmi_ao.so": result(mi_ao),
            "libzknet.so": result(zknet)}


def test_decision_forces_only_when_the_operator_accepts_an_unverified_library(capsys):
    assert install.vendor_decision(results(), False) is False
    assert install.vendor_decision(results(), True) is False
    assert install.vendor_decision(results(app="compatible"), False) is False
    with pytest.raises(SystemExit, match="--allow-unverified") as refused:
        install.vendor_decision(results(mi_ao="UNKNOWN BUILD"), False)
    assert "audio (tones, MP3, radio) stays off" in str(refused.value.code)
    assert install.vendor_decision(results(zknet="UNKNOWN BUILD"), True) is True
    assert "fallback access point" in capsys.readouterr().out


def test_decision_refuses_a_vendor_application_without_the_entry_points_even_when_forced():
    for allow in (False, True):
        with pytest.raises(SystemExit, match="does not change this") as refused:
            install.vendor_decision(results(app="UNKNOWN BUILD", missing=["onEasyUIDeinit"]), allow)
        assert "it does not define onEasyUIDeinit" in str(refused.value.code)


class FakeClock:
    host, serial, adb = "192.0.2.7", "192.0.2.7:5555", "adb"

    def __init__(self, preflight="Preflight passed\nexit=0\n", files=None):
        self.preflight, self.files, self.calls = preflight, files or {}, []

    def run(self, *args, check=True, timeout=600):
        self.calls.append(args)
        if args[0] == "pull":
            Path(args[2]).write_bytes(self.files[args[1]])
        if args[0] == "shell" and "--preflight" in args[1]:
            return self.preflight
        return ""

    def shell(self, command):
        return self.run("shell", command)

    def helper_calls(self):
        return [args[1] for args in self.calls if args[0] == "shell" and install.HELPER + " --" in args[1]]


@pytest.mark.parametrize("force", [False, True])
def test_flash_passes_force_to_both_helper_runs_only_when_asked(monkeypatch, force):
    monkeypatch.setattr(install, "wait_for", lambda clock, version: True)
    clock = FakeClock()
    assert install.flash(clock, Path("tc002-update"), Path("update.img"), "v", True, "update.img", force=force)
    preflight, write = clock.helper_calls()
    image = install.IMAGE_ON_CLOCK + (" --force" if force else "")
    assert preflight == f'{install.HELPER} --preflight {image} 2>&1; echo "exit=$?"'
    assert write == f"{install.HELPER} --install {image}"


def test_flash_stops_when_the_preflight_refuses(monkeypatch):
    clock = FakeClock(preflight="Refusing to install on an unverified stock firmware\nexit=1\n")
    with pytest.raises(SystemExit, match="preflight refused"):
        install.flash(clock, Path("tc002-update"), Path("update.img"), "v", True, "update.img")
    assert len(clock.helper_calls()) == 1


def test_verify_vendor_reads_the_partition_and_prints_what_is_not_recorded(tmp_path, monkeypatch, capsys):
    # A partition that already carries the port: the vendor application is the preserved copy.
    tree = tmp_path / "tree/lib"
    tree.mkdir(parents=True)
    app = make_elf(32, launcher_symbols())
    (tree / "libulanzi-bootstrap.so").write_bytes(app)
    (tree / "libzkgui.so").write_bytes(b"the AWTRIX launcher")

    def unsquashfs(command, **kwargs):
        assert command[0] == "unsquashfs"
        shutil.copytree(tmp_path / "tree", command[command.index("-d") + 1])
    monkeypatch.setattr(install.subprocess, "run", unsquashfs)
    libraries = FINGERPRINTS["libraries"]
    clock = FakeClock(files={libraries["libmi_ao.so"]["path"]: b"other audio build",
                             libraries["libzknet.so"]["path"]: b"other network build"})
    found = install.verify_vendor(clock, tmp_path / "res-partition.bin", tmp_path)
    assert [found[name]["status"] for name in libraries] == ["compatible", "UNKNOWN BUILD", "UNKNOWN BUILD"]
    out = capsys.readouterr().out
    assert hashlib.sha256(app).hexdigest() in out  # in full, so it can be reported
    assert hashlib.sha256(b"other audio build").hexdigest() in out
    assert "everything the launcher calls" in out and "stays off" in out


def run_main(monkeypatch, tmp_path, *args, vendor=None):
    flashed = []
    monkeypatch.setattr(install, "check_tools", lambda: None)
    monkeypatch.setattr(install, "find_adb", lambda work: "adb")
    monkeypatch.setattr(install.Clock, "connect", lambda self: None)
    monkeypatch.setattr(install, "check_clock", lambda clock: None)
    monkeypatch.setattr(install, "dump_partition", lambda clock, out: b"")
    monkeypatch.setattr(install, "verify_vendor", lambda clock, dump, work: vendor or results())
    monkeypatch.setattr(install.image, "build_images", lambda *a: {
        "update.img": {"bytes": 1}, "restore-stock.img": {"bytes": 1}})
    monkeypatch.setattr(install, "flash", lambda *a, force=False: flashed.append((a[2].name, force)) and False)
    monkeypatch.setattr(sys, "argv", ["install.py", "192.0.2.7", "--work", str(tmp_path), "--yes", *args])
    install.main()
    return flashed


def test_main_restores_with_force(monkeypatch, tmp_path, capsys):
    stock = tmp_path / "192.0.2.7/20260925-120000/restore-stock.img"
    stock.parent.mkdir(parents=True)
    stock.write_bytes(b"stock")
    assert run_main(monkeypatch, tmp_path, "--restore") == [("restore-stock.img", True)]
    assert "this clock's own stock partition" in capsys.readouterr().out


@pytest.mark.parametrize("args, vendor, force", [
    ((), None, False),
    (("--allow-unverified",), None, False),
    ((), results(app="compatible"), False),
    (("--allow-unverified",), results(mi_ao="UNKNOWN BUILD"), True),
])
def test_main_forces_the_install_only_over_an_accepted_unverified_library(monkeypatch, tmp_path, args, vendor, force):
    assert run_main(monkeypatch, tmp_path, *args, vendor=vendor) == [("update.img", force)]
