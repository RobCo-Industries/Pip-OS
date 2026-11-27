"""The end of the boot: the quiet second, the scroll and the text after a ROM.

With typing on, the kernel closes the click gate when the last boot line
starts and prints "ev=quiet dropped=<n> t_ms=<ms>". With a ROM to run, the
text then scrolls away one line every 40 ms, a blip for each line that
leaves and is not blank, and the kernel prints "ev=scroll lines= blips=".
When the ROM ends, the boot text comes back on the screen.

ev=quiet's t_ms is tick_ms(). The audio clock starts from tick_ms() too,
so the two agree within one 10 ms tick.
"""

import os
import subprocess
import sys
import zlib

from hosttiming import Timed, judged, split_checks
from pipos_helpers import (TIMEOUT, booted, dump, make_card, no_exception, one,
                           pipos_markers)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CFG = "PIPBOY/PIPOS.CFG"
ROM = "PIPBOY/ROM1.ROM"
CLICK_LEN = "789"
BLIP_LEN = "600"
RATE = "37500"
QUIET_MS = 1000
# Blips start 40 ms apart, or 80 ms across a blank line. Trace times read
# the audio tick, so allow one tick less than a step.
STEP_MS = 40
MIN_BLIP_GAP_MS = 30
TICK_MS = 10
HEAD = ["*************** PIP-OS(R) V7.1.0.8 ***************",
        "COPYRIGHT 2075 ROBCO IND.",
        "**************************************************",
        "",
        "LOADER V1.1",
        "EXEC VERSION 41.10",
        "64K RAM SYSTEM",
        "38911 BYTES FREE"]
TAIL = ["", "**************************************************",
        "PIP-OS KERNEL INITIALIZED", "PIP-OS READY",
        "**************************************************"]
LINES = 15   # head, holotape line, ROM line, tail
BLIPS = 13   # the two blank lines get none


def cfg(sound="yes", delay=10, line=0):
    return (f"[display]\nchar_delay_ms = {delay}\nline_delay_ms = {line}\n"
            f"[audio]\nvolume = 80\nbeep = yes\nsystem_sound = {sound}\n").encode()


def blip_crc(tmp_path):
    out = tmp_path / "pcm"
    subprocess.run([sys.executable, "-I", os.path.join(ROOT, "tools", "sound_table.py"),
                    "--dump", str(out)], check=True)
    return f"{zlib.crc32((out / 'blip.u8').read_bytes()):08x}"


def boot_text_shown(rows):
    """The screen rows start with the boot text, holotape and ROM lines included."""
    text = [r.rstrip() for r in rows]
    assert text[:len(HEAD)] == HEAD, rows
    assert text[len(HEAD)].endswith("FOUND"), rows
    assert text[len(HEAD) + 1].startswith("LOAD ROM(1): "), rows
    assert text[len(HEAD) + 2:LINES] == TAIL, rows


def is_click(m):
    return m.get("ev") == "audio" and m.get("op") in ("pcm", "pcm_mix") \
        and m.get("len") == CLICK_LEN


def blip_gaps(times):
    assert all(b - a >= MIN_BLIP_GAP_MS for a, b in zip(times, times[1:])), times


def stretched(times):
    """A late step shows the host stalled QEMU: a gap over its plan by more than a tick."""
    for a, b in zip(times, times[1:]):
        plan = STEP_MS if b - a < 2 * STEP_MS - TICK_MS else 2 * STEP_MS
        if b - a > plan + TICK_MS:
            return f"a blip came {b - a} ms after the one before, planned {plan}"
    return None


def scroll_run(board, kernel_image, test_rom, tmp_path, blip):
    sd = make_card(tmp_path / "sd.img", {CFG: cfg(), ROM: test_rom("sleep")})
    with booted(kernel_image, board, sd=sd, wait="jump") as vm:
        vm.wait_for("ev=sleeping", timeout=TIMEOUT, line=True)
        under = dump(vm)
        marks = pipos_markers(vm)
        serial = vm.serial
        vm.script("kill", timeout=TIMEOUT)
        vm.wait_for("ev=ready", timeout=TIMEOUT)
        after = dump(vm)
        no_exception(vm)

    quiet = one(vm, "quiet")
    # The gate closes as the last line starts: after the line before it,
    # before the last line itself reaches the UART.
    at = serial.index(" ev=quiet ")
    assert serial.index("PIP-OS READY\n") < at < serial.rindex(TAIL[-1] + "\n"), serial
    quiet_ms = int(quiet["t_ms"])

    names = [m.get("ev") for m in marks]
    q, j = names.index("quiet"), names.index("jump")
    assert q < j, names
    # No click starts after the gate closed, in the log or by the clock.
    assert not [m for m in marks[q:j] if is_click(m)], marks[q:j]
    late = [m for m in marks if is_click(m) and int(m["t_ms"]) > quiet_ms + TICK_MS]
    assert not late, (quiet_ms, late)
    # The sleep ROM makes no card call, so it owes no click: the kernel's
    # clicks under a ROM come from the ROM's own reads.
    assert not [m for m in marks[j:] if m.get("ev") == "audio"], marks[j:]

    blips = [m for m in marks[q:j] if m.get("ev") == "audio" and m.get("op") == "pcm"
             and m.get("len") == BLIP_LEN]
    assert len(blips) == BLIPS, blips
    for m in blips:
        assert (m["rate"], m["crc"]) == (RATE, blip), m
    times = [int(m["t_ms"]) for m in blips]
    # ev=quiet reads tick_ms(), the blip the audio trace clock. The trace
    # clock starts from tick_ms() and can lag it by up to one tick.
    assert times[0] - quiet_ms >= QUIET_MS - TICK_MS, (quiet_ms, times[0])

    scroll = one(vm, "scroll")
    assert (scroll["lines"], scroll["blips"]) == (str(LINES), str(BLIPS)), scroll
    assert serial.index(" ev=scroll ") < serial.index(" ev=jump "), serial

    # The screen is empty under the ROM and holds the boot text after it.
    assert all(not r.strip() for r in under), under
    boot_text_shown(after)
    return times


def test_quiet_second_then_scroll(board, kernel_image, test_rom, tmp_path, need_mtools):
    blip = blip_crc(tmp_path)
    judged(lambda: split_checks(
        lambda: scroll_run(board, kernel_image, test_rom, tmp_path, blip),
        Timed("blip gaps", blip_gaps, stretched)))


def test_sound_off_scrolls_in_silence(board, kernel_image, test_rom, tmp_path, need_mtools):
    sd = make_card(tmp_path / "sd.img", {CFG: cfg(sound="no"), ROM: test_rom("hello")})
    with booted(kernel_image, board, sd=sd) as vm:
        rows = dump(vm)
        no_exception(vm)
    scroll = one(vm, "scroll")
    assert (scroll["lines"], scroll["blips"]) == (str(LINES), "0"), scroll
    assert not pipos_markers(vm, "quiet"), vm.serial
    assert " ev=audio op=pcm " not in vm.serial, vm.serial
    assert one(vm, "rom_exit")["status"] == "7"
    boot_text_shown(rows)


def test_fault_line_after_boot_text(board, kernel_image, test_rom, tmp_path, need_mtools):
    sd = make_card(tmp_path / "sd.img", {CFG: cfg(sound="no", delay=1),
                                         ROM: test_rom("fault_undef")})
    with booted(kernel_image, board, sd=sd) as vm:
        rows = dump(vm)
        no_exception(vm)
    assert one(vm, "rom_kill")["reason"] == "undef", vm.serial
    boot_text_shown(rows)
    assert rows[LINES].startswith("ROM(1) "), rows


def test_no_rom_no_scroll(board, kernel_image, tmp_path, need_mtools):
    sd = make_card(tmp_path / "sd.img", {CFG: cfg()})
    with booted(kernel_image, board, sd=sd) as vm:
        rows = dump(vm)
        no_exception(vm)
    # The gate still closes at the last line, then opens again: no scroll.
    one(vm, "quiet")
    assert not pipos_markers(vm, "scroll"), vm.serial
    assert " len=600 " not in vm.serial, vm.serial
    assert [r.rstrip() for r in rows[:len(HEAD)]] == HEAD, rows


def test_typing_off_keeps_the_boot(board, kernel_image, test_rom, tmp_path, need_mtools):
    sd = make_card(tmp_path / "sd.img", {CFG: cfg(delay=0), ROM: test_rom("hello")})
    with booted(kernel_image, board, sd=sd) as vm:
        rows = dump(vm)
        no_exception(vm)
    assert not pipos_markers(vm, "quiet"), vm.serial
    assert not pipos_markers(vm, "scroll"), vm.serial
    assert " len=600 " not in vm.serial, vm.serial
    boot_text_shown(rows)
