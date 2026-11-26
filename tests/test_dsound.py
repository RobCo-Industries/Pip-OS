"""The disk click: card work and the typed boot text pay out in clicks.

With [audio] system_sound on, every card read or write call and every boot
line written with typing on adds to a debt of clicks. A tick callback pays
it in bursts while the voice is free. The kernel prints
"ev=disk at=<stage> calls= blocks= owed=" after each boot stage, so a test
reads the causes there and the effects in the audio trace.
"""

import os
import subprocess
import sys
import time
import zlib

from pipos_helpers import (booted, config_summary, make_card, make_rom, no_exception,
                           pipos_markers)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CFG = "PIPBOY/PIPOS.CFG"
ROM = "PIPBOY/ROM1.ROM"
VOLUME = 80
CLICK_LEN = "789"
# A click lasts 21 ms; a gap is 3 ticks or more.
MIN_START_GAP_MS = 30


def cfg(sound="yes", delay=2):
    return (f"[display]\nchar_delay_ms = {delay}\nline_delay_ms = 0\n"
            f"[audio]\nvolume = {VOLUME}\nbeep = yes\nsystem_sound = {sound}\n").encode()


def table_crcs(tmp_path):
    """CRC-32 of each click and of the beep, from the table generator's own dump."""
    out = tmp_path / "pcm"
    subprocess.run([sys.executable, "-I", os.path.join(ROOT, "tools", "sound_table.py"),
                    "--dump", str(out)], check=True)
    clicks = []
    k = 0
    while (out / f"click{k:02d}.u8").exists():
        clicks.append(f"{zlib.crc32((out / f'click{k:02d}.u8').read_bytes()):08x}")
        k += 1
    return clicks, f"{zlib.crc32((out / 'beep.u8').read_bytes()):08x}"


def settle(vm, quiet_s=2.0, limit_s=40.0):
    """Waits until no audio record has come for quiet_s: the debt is paid."""
    end = time.monotonic() + limit_s
    seen = vm.serial.count("ev=audio ")
    last = time.monotonic()
    while time.monotonic() < end:
        time.sleep(0.25)
        now = vm.serial.count("ev=audio ")
        if now != seen:
            seen, last = now, time.monotonic()
        elif time.monotonic() - last >= quiet_s:
            return
    raise AssertionError("the clicks did not stop\n" + vm.serial)


def clicks_of(trace):
    return [m for m in trace if m["op"] == "pcm" and m.get("len") == CLICK_LEN]


def check_clicks(trace, crcs, beep_crc):
    """Every click is a whole table entry in turn, with its end, after the beep."""
    beep_end = None
    for i, m in enumerate(trace):
        if m["op"] == "pcm" and m.get("crc") == beep_crc:
            assert trace[i + 1]["op"] == "pcm_end", trace
            beep_end = int(trace[i + 1]["t_ms"])
    assert beep_end is not None, trace
    starts = []
    k = 0
    for i, m in enumerate(trace):
        if not (m["op"] == "pcm" and m.get("len") == CLICK_LEN):
            continue
        assert (m["rate"], m["vol"], m["crc"]) == ("37500", str(VOLUME), crcs[k % 12]), m
        assert trace[i + 1]["op"] == "pcm_end", trace[i:i + 2]
        t = int(m["t_ms"])
        assert t >= beep_end, (m, beep_end)
        if starts:
            assert t - starts[-1] >= MIN_START_GAP_MS, (starts[-1], m)
        starts.append(t)
        k += 1
    return starts


def owed(vm):
    return sum(int(m["owed"]) for m in pipos_markers(vm, "disk"))


def test_clicks_follow_the_work(board, kernel_image, tmp_path, need_mtools):
    crcs, beep_crc = table_crcs(tmp_path)
    assert len(crcs) == 12
    sd = make_card(tmp_path / "sd.img", {CFG: cfg()})
    with booted(kernel_image, board, sd=sd) as vm:
        settle(vm)
        cfgm = config_summary(vm)
        trace = pipos_markers(vm, "audio")
        disk = pipos_markers(vm, "disk")
        no_exception(vm)
    assert cfgm["key_click"] == "1", vm.serial
    stages = [m["at"] for m in disk]
    assert stages[:2] == ["mount", "config"], stages
    assert [s for s in stages if s != "text"] == ["mount", "config", "holo", "rom"], stages
    assert stages.count("text") >= 13, stages
    # The mount and config lines wait for the setting.
    serial = vm.serial
    assert serial.index("ev=config rc=ok") < serial.index("ev=disk at=mount"), serial
    assert int(disk[0]["calls"]) > 0 and int(disk[0]["owed"]) > 0, disk
    assert not pipos_markers(vm, "audio_drop"), serial
    starts = check_clicks(trace, crcs, beep_crc)
    assert 0 < len(starts) <= owed(vm), (len(starts), disk)


def test_a_big_rom_owes_at_most_the_cap(board, kernel_image, tmp_path, need_mtools):
    crcs, beep_crc = table_crcs(tmp_path)
    rom = make_rom(name="BIG", payload_size=3 * 1024 * 1024)
    sd = make_card(tmp_path / "sd.img", {CFG: cfg(delay=0), ROM: rom})
    with booted(kernel_image, board, sd=sd) as vm:
        settle(vm)
        trace = pipos_markers(vm, "audio")
        no_exception(vm)
    serial = vm.serial
    load = [m for m in pipos_markers(vm, "disk") if m["at"] == "rom"]
    assert len(load) == 1 and int(load[0]["blocks"]) >= 6144, load
    check_clicks(trace, crcs, beep_crc)
    after = serial[serial.index("ev=disk at=rom"):]
    assert after.count(f" len={CLICK_LEN} ") <= 40, after
    # No click starts once the kernel has jumped to the ROM.
    assert f" len={CLICK_LEN} " not in serial[serial.index("ev=jump"):], serial


def test_sound_off_prints_and_plays_nothing(board, kernel_image, tmp_path, need_mtools):
    sd = make_card(tmp_path / "sd.img", {CFG: cfg(sound="no")})
    with booted(kernel_image, board, sd=sd) as vm:
        settle(vm, quiet_s=1.0)
        no_exception(vm)
    assert not pipos_markers(vm, "disk"), vm.serial
    assert f" len={CLICK_LEN} " not in vm.serial, vm.serial


def test_no_card_prints_and_plays_nothing(board, kernel_image, need_mtools):
    with booted(kernel_image, board) as vm:
        settle(vm, quiet_s=1.0)
        no_exception(vm)
    assert not pipos_markers(vm, "disk"), vm.serial
    assert f" len={CLICK_LEN} " not in vm.serial, vm.serial
