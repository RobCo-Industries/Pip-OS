"""Boots with an SD card: ROM file checks, configuration, holotapes."""

import pathlib

import pytest

from pipos_helpers import (SCHEME_BG, booted, config, config_issues, config_summary,
                           corrupt, dump, make_big_rom, make_card, make_rom, no_exception,
                           one, pipos_markers, rom_crc, screenshot)

ROM = "PIPBOY/ROM1.ROM"
CFG = "PIPBOY/PIPOS.CFG"


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
        assert one(vm, "ready")["rom"] == "1"
        assert not pipos_markers(vm, "jump")
        rows = dump(vm)
        assert "LOAD ROM(1): DEITRIX 3.0.3" in rows, rows
        assert "NO HOLOTAPE FOUND" in rows, rows
        no_exception(vm)


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
    }


@pytest.mark.parametrize("error", ["crc", "magic", "bounds"])
def test_bad_rom(board, kernel_image, tmp_path, need_mtools, error):
    data, line = bad_roms(kernel_image)[error]
    sd = make_card(tmp_path / "sd.img", {ROM: data})
    with booted(kernel_image, board, sd=sd) as vm:
        m = one(vm, "rom")
        assert (m["rc"], m["name"], m["version"]) == (error, "-", "-"), vm.serial
        assert one(vm, "error")["code"] == error
        assert one(vm, "ready")["rom"] == "0"
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
