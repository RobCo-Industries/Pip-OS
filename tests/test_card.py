"""Boots with an SD card: ROM file checks, configuration, holotapes."""

import os
import pathlib
import shutil
import subprocess
import time

import pytest
from PIL import Image

from harness.boot import boot

from pipos_helpers import (READY, SCHEME_BG, TIMEOUT, booted, config, config_issues,
                           config_summary, corrupt, dump, make_big_rom, make_card,
                           make_rom_without_meta, make_multi_card, make_rom,
                           make_superfloppy_card, no_exception,
                           one, pipos_markers, rom_crc, screenshot)

FAT16_LBA = 0x0E
ROM = "PIPBOY/ROM1.ROM"
CFG = "PIPBOY/PIPOS.CFG"
GOLDEN_DIR = pathlib.Path(__file__).resolve().parent / "golden"


def test_valid_rom(board, kernel_image, tmp_path, need_mtools):
    rom = make_rom(name="DEITRIX", version="3.0.3")
    sd = make_card(tmp_path / "sd.img", {ROM: rom, CFG: config()})
    with booted(kernel_image, board, sd=sd) as vm:
        assert one(vm, "card")["rc"] == "ok"
        assert config_summary(vm)["rc"] == "ok"
        m = one(vm, "rom")
        assert (m["rc"], m["name"], m["version"]) == ("ok", "DEITRIX", "3.0.3"), vm.serial
        assert int(m["size"]) == len(rom)
        assert int(m["crc"], 16) == rom_crc(rom)
        load = one(vm, "load")
        assert (load["path"], load["addr"]) == ("/PIPBOY/ROM1.ROM", "0x00400000")
        assert one(vm, "jump")["entry"] == "0x00400000"
        assert one(vm, "rom_exit")["status"] == "0"
        assert one(vm, "ready")["rom"] == "1"
        rows = dump(vm)
        assert "LOAD ROM(1): DEITRIX 3.0.3" in rows, rows
        assert "NO HOLOTAPE FOUND" in rows, rows
        no_exception(vm)


def test_mount_reports_partition(board, kernel_image, tmp_path, need_mtools):
    sd = make_card(tmp_path / "sd.img", {CFG: config()})
    with booted(kernel_image, board, sd=sd) as vm:
        m = one(vm, "mount")
        assert (m["part"], m["lba"]) == ("0", "2048"), vm.serial


def test_second_partition_is_fat(board, kernel_image, tmp_path, need_mtools):
    rom = make_rom(name="DEITRIX", version="3.0.3")
    sd = make_multi_card(tmp_path / "sd.img", [(0x83, 4, None),
                                               (FAT16_LBA, 8, {ROM: rom, CFG: config("amber")})])
    with booted(kernel_image, board, sd=sd) as vm:
        assert one(vm, "card")["rc"] == "ok", vm.serial
        m = one(vm, "mount")
        assert (m["part"], m["type"], m["lba"]) == ("1", "0x0e", str(2048 + 4 * 2048)), vm.serial
        cfg = config_summary(vm)
        assert (cfg["rc"], cfg["scheme"]) == ("ok", "amber"), vm.serial
        assert one(vm, "rom")["rc"] == "ok"
        assert one(vm, "ready")["rom"] == "1"
        no_exception(vm)


def test_fat_volume_with_wrong_type(board, kernel_image, tmp_path, need_mtools):
    sd = make_multi_card(tmp_path / "sd.img", [(0x83, 4, None), (0x83, 8, {CFG: config()})])
    with booted(kernel_image, board, sd=sd) as vm:
        assert one(vm, "mount")["part"] == "1", vm.serial
        assert config_summary(vm)["rc"] == "ok"


def test_no_fat_partition(board, kernel_image, tmp_path, need_mtools):
    sd = make_multi_card(tmp_path / "sd.img", [(0x83, 4, None), (FAT16_LBA, 4, None)])
    with booted(kernel_image, board, sd=sd) as vm:
        assert one(vm, "card")["rc"] == "nomount", vm.serial
        assert not pipos_markers(vm, "mount")
        assert config_summary(vm)["rc"] == "nocard"
        assert one(vm, "rom")["rc"] == "io"
        assert one(vm, "ready")["rom"] == "0"
        no_exception(vm)


def test_card_without_partition_table(board, kernel_image, tmp_path, need_mtools):
    sd = make_superfloppy_card(tmp_path / "sd.img", {CFG: config("blue")})
    with booted(kernel_image, board, sd=sd) as vm:
        m = one(vm, "mount")
        assert (m["part"], m["lba"]) == ("whole", "0"), vm.serial
        assert config_summary(vm)["scheme"] == "blue"


def test_rom_name_from_header(board, kernel_image, tmp_path, need_mtools):
    rom = make_rom(name="testrom", version="1.2.10")
    sd = make_card(tmp_path / "sd.img", {ROM: rom})
    with booted(kernel_image, board, sd=sd) as vm:
        m = one(vm, "rom")
        assert (m["rc"], m["name"], m["version"]) == ("ok", "testrom", "1.2.10"), vm.serial
        assert "LOAD ROM(1): TESTROM 1.2.10" in dump(vm)


def bad_roms(kernel_image):
    good = make_rom()
    return {
        "crc": (corrupt(good), "ROM(1) NOT LOADED: CHECKSUM MISMATCH"),
        "magic": (pathlib.Path(kernel_image).read_bytes(), "ROM(1) NOT LOADED: NOT A ROM IMAGE"),
        "bounds": (make_big_rom(), "ROM(1) NOT LOADED: DOES NOT FIT THE ROM SLOT"),
        "version": (make_rom_without_meta(), "ROM(1) NOT LOADED: UNKNOWN HEADER VERSION"),
    }


@pytest.mark.parametrize("error", ["crc", "magic", "bounds", "version"])
def test_bad_rom(board, kernel_image, tmp_path, need_mtools, error):
    data, line = bad_roms(kernel_image)[error]
    sd = make_card(tmp_path / "sd.img", {ROM: data})
    with booted(kernel_image, board, sd=sd) as vm:
        m = one(vm, "rom")
        assert (m["rc"], m["name"], m["version"]) == (error, "-", "-"), vm.serial
        assert one(vm, "error")["code"] == error
        assert one(vm, "ready")["rom"] == "0"
        assert not pipos_markers(vm, "jump")
        rows = dump(vm)
        assert line in rows, rows
        assert not any(r.startswith("LOAD ROM(1)") for r in rows), rows
        no_exception(vm)


def test_no_rom_no_config(board, kernel_image, tmp_path, need_mtools):
    sd = make_card(tmp_path / "sd.img", {"PIPBOY/": b""})
    with booted(kernel_image, board, sd=sd) as vm:
        assert one(vm, "card")["rc"] == "ok"
        assert config_summary(vm)["rc"] == "nofile"
        assert one(vm, "rom")["rc"] == "nofile"
        assert one(vm, "ready")["rom"] == "0"
        assert not pipos_markers(vm, "jump")
        assert "ROM(1) NOT LOADED: NO FILE /PIPBOY/ROM1.ROM" in dump(vm)


@pytest.mark.parametrize("scheme", ["amber", "blue"])
def test_scheme_changes_background(board, kernel_image, tmp_path, need_mtools, scheme):
    sd = make_card(tmp_path / "sd.img", {CFG: config(scheme=scheme)})
    with booted(kernel_image, board, sd=sd) as vm:
        cfg = config_summary(vm)
        assert (cfg["rc"], cfg["scheme"]) == ("ok", scheme), vm.serial
        dump(vm)
        img = screenshot(vm, tmp_path)
    assert img.getpixel((639, 479)) == SCHEME_BG[scheme]
    assert img.getpixel((0, 470)) == SCHEME_BG[scheme]


def test_config_values(board, kernel_image, tmp_path, need_mtools):
    sd = make_card(tmp_path / "sd.img", {CFG: config(delay=20, volume=30, beep="no")})
    with booted(kernel_image, board, sd=sd) as vm:
        cfg = config_summary(vm)
        assert (cfg["rc"], cfg["line_delay_ms"], cfg["volume"], cfg["beep"]) == \
            ("ok", "20", "30", "0"), vm.serial
        assert config_issues(vm) == []
        dump(vm)
        # The beep was off: audio_poll() ran before the dump, nothing played.
        assert not pipos_markers(vm, "audio"), vm.serial


def test_config_bad_values_ignored(board, kernel_image, tmp_path, need_mtools):
    text = config(scheme="pink", volume=500, beep="maybe",
                  extra="[display]\nbrightness = 3\n[extra]\nkey = 1\n")
    sd = make_card(tmp_path / "sd.img", {CFG: text})
    with booted(kernel_image, board, sd=sd) as vm:
        issues = config_issues(vm)
        assert ("badvalue", "display.scheme") in issues, vm.serial
        assert ("badvalue", "audio.volume") in issues
        assert ("badvalue", "audio.beep") in issues
        assert ("unknown", "display.brightness") in issues
        assert ("unknown", "extra.key") in issues
        cfg = config_summary(vm)
        assert (cfg["rc"], cfg["scheme"], cfg["volume"], cfg["beep"]) == \
            ("ok", "green", "100", "1"), vm.serial
        assert one(vm, "ready")["rom"] == "0"
        no_exception(vm)


def test_config_syntax_error(board, kernel_image, tmp_path, need_mtools):
    text = b"[display]\nscheme = amber\nthis line is not ini\n"
    sd = make_card(tmp_path / "sd.img", {CFG: text})
    with booted(kernel_image, board, sd=sd) as vm:
        cfg = config_summary(vm)
        assert (cfg["rc"], cfg["line"], cfg["scheme"]) == ("syntax", "3", "green"), vm.serial
        assert one(vm, "ready")["rom"] == "0"


def test_holotape_line(board, kernel_image, tmp_path, need_mtools):
    files = {"PIPBOY/HOLO/GAME.HTP": b"holotape", "PIPBOY/HOLO/NOTES.TXT": b"text",
             "PIPBOY/HOLO/SUB/": b""}
    sd = make_card(tmp_path / "sd.img", files)
    with booted(kernel_image, board, sd=sd) as vm:
        assert one(vm, "holo")["count"] == "1", vm.serial
        rows = dump(vm)
        assert "1 HOLOTAPE FOUND" in rows and "NO HOLOTAPE FOUND" not in rows, rows


@pytest.fixture(scope="session")
def default_card(tmp_path_factory):
    """The card of a plain `make sdimage`, built once in a folder of its own."""
    build = tmp_path_factory.mktemp("default_card") / "build"
    root = pathlib.Path(__file__).resolve().parent.parent
    # A plain build: ROM, CI or flags from the caller or an outer make do not apply.
    drop = {"MAKEFLAGS", "MFLAGS", "MAKELEVEL", "CI", "ROM", "TAPE", "CFLAGS_EXTRA", "WINDOW"}
    env = {k: v for k, v in os.environ.items() if k not in drop}
    subprocess.run(["make", "-s", "sdimage", f"BUILD={build}"], cwd=root, check=True,
                   stdout=subprocess.DEVNULL, env=env)
    return build / "sd.img"


def serial_count(vm, text, count, timeout=TIMEOUT):
    """Waits until `text` shows up `count` times on the serial console."""
    deadline = time.monotonic() + timeout
    while vm.serial.count(text) < count:
        assert time.monotonic() < deadline, f"wanted {count}x {text!r}\n{vm.serial}"
        time.sleep(0.05)


def check_golden(img, name):
    """Compares the screen with tests/golden/<name>.png. PIPOS_UPDATE_GOLDEN=1 rewrites it."""
    golden = GOLDEN_DIR / f"{name}.png"
    if os.environ.get("PIPOS_UPDATE_GOLDEN"):
        img.save(golden)
    with Image.open(golden) as g:
        assert g.convert("RGB").tobytes() == img.tobytes(), f"screen differs from {golden}"


MENU = "hello: menu, 1 tapes, tape 1 selected\n"


def test_default_card_runs_hello_and_tape(board, kernel_image, need_mtools, default_card, tmp_path):
    """`make sdimage` puts sdk/examples/hello on the card as ROM1.ROM and
    sdk/examples/tape as HOLO/BEACON.HTP. The ROM lists the tape, runs it
    on SELECT, shows its exit status and ends on LEFT."""
    sd = tmp_path / "sd.img"
    shutil.copyfile(default_card, sd)
    with boot(kernel_image, board, sd=sd, serial2=True, timeout=TIMEOUT) as vm:
        serial_count(vm, MENU, 1)
        m = one(vm, "rom")
        assert (m["rc"], m["name"], m["version"]) == ("ok", "HELLO", "1.0.0"), vm.serial
        assert one(vm, "holo")["count"] == "1", vm.serial
        assert "hello: Pip-OS ROM running, ABI 0x00010000\n" in vm.serial
        assert "hello: heap 0x01000000, 8128 KiB, stack top 0x01800000\n" in vm.serial
        assert "hello: This line came from the heap.\n" in vm.serial
        assert "hello: slept " in vm.serial
        assert "hello: 1 holotapes on the card\n" in vm.serial
        check_golden(screenshot(vm, tmp_path), "card_hello_list")

        assert vm.script("key select", timeout=TIMEOUT).status == "ok"
        serial_count(vm, "beacon: ready\n", 1)
        assert one(vm, "holo_start")["name"] == "BEACON.HTP", vm.serial
        assert 'beacon: started, args "from hello, run 1"\n' in vm.serial
        check_golden(screenshot(vm, tmp_path), "card_beacon")
        # Status 3 by default: up twice, down once gives 4.
        for line in ("key up", "key up", "key down", "key select"):
            assert vm.script(line, timeout=TIMEOUT).status == "ok"
        serial_count(vm, MENU, 2)
        assert one(vm, "holo_exit")["status"] == "4", vm.serial
        assert "beacon: exit status 4\n" in vm.serial
        assert "hello: BEACON.HTP: exit status 4\n" in vm.serial

        # The ROM still reacts: a dial turn redraws, a second run counts, LEFT ends it.
        assert vm.script("dial 1", timeout=TIMEOUT).status == "ok"
        serial_count(vm, MENU, 3)
        assert vm.script("key select", timeout=TIMEOUT).status == "ok"
        serial_count(vm, "beacon: ready\n", 2)
        assert 'beacon: started, args "from hello, run 2"\n' in vm.serial
        assert vm.script("key select", timeout=TIMEOUT).status == "ok"
        serial_count(vm, "hello: BEACON.HTP: exit status 3\n", 1)
        serial_count(vm, MENU, 4)
        assert not pipos_markers(vm, "ready")
        assert vm.script("key left", timeout=TIMEOUT).status == "ok"
        vm.wait_for(READY, timeout=TIMEOUT)
        assert "hello: bye\n" in vm.serial
        assert one(vm, "rom_exit")["status"] == "0", vm.serial
        assert one(vm, "ready")["rom"] == "1"
        assert [m["status"] for m in pipos_markers(vm, "holo_exit")] == ["4", "3"], vm.serial
        no_exception(vm)
