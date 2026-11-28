"""The stored levels of /PIPBOY/PIPOS.SET, through the trace backend.

At boot the kernel reads PIPOS.CFG, then PIPOS.SET. A valid PIPOS.SET
replaces the levels it names and prints nothing. A file that is not valid
changes nothing and prints one ev=levels line with the reason. A missing
file prints nothing.
"""

import pytest

from pipos_helpers import (booted, config, config_summary, make_card, no_exception,
                           pipos_markers, wait_markers)

CFG = "PIPBOY/PIPOS.CFG"
SET = "PIPBOY/PIPOS.SET"
STARTS = ("tone", "pcm", "voice")
# The rest ramp plays at full scale whatever the levels.
RAMP_LEN = "882"
# PIPOS.CFG of every card here: master 70, sfx 50, so the beep's gain is 1750.
CFG_TEXT = config(volume=70, extra="sfx = 50\n")


def boot_beep(kernel_image, board, path, files):
    """The first audio line, the power-on beep, and the ev=levels lines."""
    with booted(kernel_image, board, sd=make_card(path, files)) as vm:
        wait_markers(vm, "audio", count=2)
        cfg = config_summary(vm)
        beep = pipos_markers(vm, "audio")[0]
        levels = pipos_markers(vm, "levels")
        no_exception(vm)
    return cfg, beep, levels, vm


def test_a_valid_file_sets_the_beep_gain(board, kernel_image, tmp_path, need_mtools):
    text = b"[audio]\nvolume = 80\nradio = 45\nsfx = 40\nrecordings = 60\ngames = 55\n"
    cfg, beep, levels, vm = boot_beep(kernel_image, board, tmp_path / "sd.img",
                                      {CFG: CFG_TEXT, SET: text})
    # The config line shows PIPOS.CFG alone.
    assert (cfg["volume"], cfg["sfx"]) == ("70", "50"), vm.serial
    assert levels == [], vm.serial
    # 80 * 40 * 40 / 100 = 1280: vol=12 gain=1280.
    assert beep["op"] == "tone", beep
    assert (beep["vol"], beep.get("gain")) == ("12", "1280"), beep


def test_absent_keys_keep_the_config_values(board, kernel_image, tmp_path, need_mtools):
    """Only volume here: sfx stays at 50 from PIPOS.CFG. 90 * 25 = 2250."""
    cfg, beep, levels, vm = boot_beep(kernel_image, board, tmp_path / "sd.img",
                                      {CFG: CFG_TEXT, SET: b"# by hand\n[audio]\nvolume = 090\n"})
    assert levels == [], vm.serial
    assert (beep["vol"], beep.get("gain")) == ("22", "2250"), beep


BROKEN = [
    ("size", b"[audio]\nsfx = 40\n" + b"\n" * 600),
    ("syntax", b"[audio]\nsfx = 40\nloud\n"),
    ("key", b"[audio]\nsfx = 40\nsfx = 30\n"),
    ("key", b"[display]\nscheme = amber\n"),
    ("value", b"[audio]\nvolume = 80\nsfx = 101\n"),
    ("syntax", b"[audio]\nsfx = 999\nfoo = 1\n[bad\n"),
]


@pytest.mark.parametrize("reason,text", BROKEN, ids=[f"{r}{i}" for i, (r, _) in
                                                      enumerate(BROKEN)])
def test_a_broken_file_changes_nothing(board, kernel_image, tmp_path, need_mtools, reason,
                                       text):
    cfg, beep, levels, vm = boot_beep(kernel_image, board, tmp_path / "sd.img",
                                      {CFG: CFG_TEXT, SET: text})
    assert len(levels) == 1, vm.serial
    assert (levels[0]["result"], levels[0]["reason"]) == ("bad", reason), levels
    assert vm.serial.count("ev=levels") == 1, vm.serial
    # The PIPOS.CFG values: 70 * 25 = 1750.
    assert (beep["vol"], beep.get("gain")) == ("17", "1750"), beep


def test_no_file_prints_nothing_new(board, kernel_image, tmp_path, need_mtools):
    """The lines of a boot with the system sound on, as with no level at all."""
    text = (b"[display]\nchar_delay_ms = 2\nline_delay_ms = 0\n"
            b"[audio]\nvolume = 70\nbeep = yes\nsystem_sound = yes\n")
    with booted(kernel_image, board, sd=make_card(tmp_path / "sd.img", {CFG: text})) as vm:
        wait_markers(vm, "ready")
        lines = pipos_markers(vm, "audio")
        no_exception(vm)
    assert "ev=levels" not in vm.serial, vm.serial
    assert lines
    for m in lines:
        assert m["op"] != "gain" and "gain" not in m, m
        if m["op"] in STARTS + ("pcm_mix",) and m.get("len") != RAMP_LEN:
            assert m["vol"] == "70", m


def test_no_card_prints_nothing_new(board, kernel_image):
    with booted(kernel_image, board) as vm:
        wait_markers(vm, "ready")
        no_exception(vm)
    assert config_summary(vm)["rc"] == "nocard", vm.serial
    assert "ev=levels" not in vm.serial, vm.serial
