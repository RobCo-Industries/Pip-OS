"""Boot text that writes itself, [display] char_delay_ms, and the system sound,
[audio] system_sound or its old name key_click.

QEMU has no PWM model, so sounds show as the trace backend's "ev=audio"
markers. Typing makes no sound. With the system sound on, the boot plays the
library's rest ramp and then the soft beep, a table sample.
"""

import os
import re
import subprocess
import sys
import zlib

from pipos_helpers import (booted, config_issues, config_summary, dump, make_card,
                           no_exception, pipos_markers)

CFG = "PIPBOY/PIPOS.CFG"
DELAY_MS = 2

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# The library's rest ramp, as its trace prints it at volume 100.
RAMP = {"op": "pcm", "rate": "22050", "len": "882", "vol": "100"}


def beep_crc(tmp_path):
    """CRC-32 of sound_beep, from the table generator's own dump."""
    out = tmp_path / "pcm"
    subprocess.run([sys.executable, "-I", os.path.join(ROOT, "tools", "sound_table.py"),
                    "--dump", str(out)], check=True)
    return f"{zlib.crc32((out / 'beep.u8').read_bytes()):08x}"


def audio(vm):
    return pipos_markers(vm, "audio")


def is_ramp(m):
    return all(m.get(k) == v for k, v in RAMP.items())


def cfg(delay=None, click=None, beep="no", sound=None):
    text = "[display]\nscheme = green\n"
    if delay is not None:
        text += f"char_delay_ms = {delay}\n"
    text += f"[audio]\nvolume = 100\nbeep = {beep}\n"
    if click is not None:
        text += f"key_click = {click}\n"
    if sound is not None:
        text += f"system_sound = {sound}\n"
    return text.encode()


def card(tmp_path, text):
    return make_card(tmp_path / "sd.img", {CFG: text})


def boot_lines(vm):
    """The boot text lines on the UART: what the screen dump shows, in order."""
    rows = [r for r in dump(vm)]
    while rows and not rows[-1]:
        rows.pop()
    return rows


def test_no_click_keeps_the_text(board, kernel_image, tmp_path, need_mtools):
    with booted(kernel_image, board, sd=card(tmp_path, cfg(DELAY_MS, "no"))) as vm:
        lines = boot_lines(vm)
        cfgm = config_summary(vm)
        no_exception(vm)
    assert (cfgm["char_delay_ms"], cfgm["key_click"]) == (str(DELAY_MS), "0")
    assert not pipos_markers(vm, "audio"), vm.serial
    for line in lines:
        assert line + "\n" in vm.serial
    assert "PIP-OS READY" in lines


def test_system_sound_plays_the_ramp_then_the_soft_beep(board, kernel_image, tmp_path,
                                                         need_mtools):
    crc = beep_crc(tmp_path)
    sd = card(tmp_path, cfg(DELAY_MS, beep="yes", sound="yes"))
    with booted(kernel_image, board, sd=sd) as vm:
        cfgm = config_summary(vm)
        lines = boot_lines(vm)
        tr = audio(vm)
        no_exception(vm)
    assert cfgm["key_click"] == "1", vm.serial
    assert "PIP-OS READY" in lines
    assert not pipos_markers(vm, "sound"), vm.serial
    assert is_ramp(tr[0]), tr
    assert tr[1]["op"] == "pcm_end", tr
    assert (tr[2]["op"], tr[2]["rate"], tr[2]["len"], tr[2]["crc"]) == \
        ("pcm", "37500", "2250", crc), tr
    assert tr[3]["op"] == "pcm_end", tr
    # Typing makes no sound: nothing else plays during the boot text.
    assert len(tr) == 4, tr
    assert not [m for m in tr if m["op"] == "tone"], tr


def test_old_name_without_beep_plays_the_ramp_alone(board, kernel_image, tmp_path,
                                                    need_mtools):
    with booted(kernel_image, board, sd=card(tmp_path, cfg(0, "yes"))) as vm:
        cfgm = config_summary(vm)
        tr = audio(vm)
        no_exception(vm)
    assert (cfgm["char_delay_ms"], cfgm["key_click"]) == ("0", "1")
    assert [m["op"] for m in tr] == ["pcm", "pcm_end"], tr
    assert is_ramp(tr[0]), tr


def test_system_sound_wins_over_the_old_name(board, kernel_image, tmp_path, need_mtools):
    with booted(kernel_image, board, sd=card(tmp_path, cfg(DELAY_MS, "yes", sound="no"))) as vm:
        cfgm = config_summary(vm)
        no_exception(vm)
    assert cfgm["key_click"] == "0", vm.serial
    assert not audio(vm), vm.serial


def test_bad_values(board, kernel_image, tmp_path, need_mtools):
    for delay, click in (("101", "loud"), ("-1", "2"), ("20ms", "")):
        sd = make_card(tmp_path / f"sd{delay}.img", {CFG: cfg(delay, click)})
        with booted(kernel_image, board, sd=sd) as vm:
            issues = config_issues(vm)
            cfgm = config_summary(vm)
            no_exception(vm)
        assert ("badvalue", "display.char_delay_ms") in issues, vm.serial
        assert ("badvalue", "audio.key_click") in issues, vm.serial
        assert (cfgm["rc"], cfgm["char_delay_ms"], cfgm["key_click"]) == ("ok", "0", "0")
        assert not pipos_markers(vm, "audio"), vm.serial


SUMMARY = re.compile(r"^@RC stage=pipos ev=config rc=ok path=\S+ line=0 scheme=.*$", re.M)


def test_keys_off_change_nothing_on_the_uart(board, kernel_image, tmp_path, need_mtools):
    """A card without the keys and one with char_delay_ms = 0, system_sound = no."""
    logs = []
    for name, text in (("absent", cfg()), ("zero", cfg(0, "no", sound="no"))):
        sd = make_card(tmp_path / f"{name}.img", {CFG: text})
        with booted(kernel_image, board, sd=sd) as vm:
            logs.append(vm.serial)
            no_exception(vm)
    summaries = [SUMMARY.findall(log) for log in logs]
    assert [len(s) for s in summaries] == [1, 1], logs
    assert summaries[0] == summaries[1]
    assert SUMMARY.sub("", logs[0]) == SUMMARY.sub("", logs[1])
