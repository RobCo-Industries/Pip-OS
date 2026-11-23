"""The audio calls on the target. QEMU has no PWM model, so the kernel
uses RobCo-Core's trace backend: one "ev=audio op=tone|stop hz= vol= t_ms="
marker per change, with t_ms on the driver's own millisecond clock.

Durations come from those t_ms values. The lower bound is strict; the
upper bound leaves SLACK_MS for a loaded machine. A sound the ROM cuts
after a pip_sleep_ms() can end up to CUT_MS early: the driver applies a
request at its next tick, so the start and the cut each land on a tick.
"""

from pipos_helpers import (assert_rom_checks, booted, config, dump, make_card, no_exception,
                           one, pipos_markers, rom_one, wait_markers)

ROM = "PIPBOY/ROM1.ROM"
CFG = "PIPBOY/PIPOS.CFG"
SLACK_MS = 150
CUT_MS = 10

CALL_CHECKS = [
    "tone_hz19", "tone_hz20001", "tone_ms0", "tone_ms60001", "volume_101",
    "seq_count0", "seq_bad_hz19", "seq_bad_hz20001", "seq_bad_ms0", "seq_bad_ms60001",
    "seq_bad_rest0", "seq_kernel", "seq_mmio", "seq_slot_end",
    "idle_status", "idle_stop",
    "vol_start", "tone_1050", "tone_busy", "tone_wait", "tone_done",
    "vol_90", "tone_1100", "tone_1200", "replace_wait",
    "tone_1300", "stop_rc", "stop_status", "stop_again",
    "seq_rc", "seq_busy", "seq_wait",
    "seq_257", "seq_257_quiet", "seq_256", "seq_256_busy", "seq_256_stop", "vol_back",
]

# (op, hz, vol) of every marker, in order: the boot beep at the configured
# volume, then each sound of audio_calls.c. A rest is a stop.
CALL_TRACE = [
    ("tone", 1000, 70), ("stop", 0, 70),
    ("tone", 1050, 40), ("stop", 0, 40),
    ("tone", 1100, 90), ("tone", 1200, 90), ("stop", 0, 90),
    ("tone", 1300, 90), ("stop", 0, 90),
    ("tone", 1400, 90), ("stop", 0, 90), ("tone", 1500, 90), ("tone", 1600, 90),
    ("stop", 0, 90),
    ("tone", 1700, 90), ("stop", 0, 90),
]


def trace(vm):
    return [(m["op"], int(m["hz"]), int(m["vol"]), int(m["t_ms"]))
            for m in pipos_markers(vm, "audio")]


def lasted(tr, i, ms, slack=SLACK_MS):
    """Marker i lasted at least ms and at most ms + slack, up to marker i + 1."""
    dt = tr[i + 1][3] - tr[i][3]
    assert ms <= dt <= ms + slack, (i, dt, ms, tr)


def card(tmp_path, rom, cfg):
    return make_card(tmp_path / "sd.img", {ROM: rom, CFG: cfg})


def test_calls(board, kernel_image, test_rom, tmp_path, need_mtools):
    sd = card(tmp_path, test_rom("audio_calls"), config(volume=70))
    with booted(kernel_image, board, sd=sd) as vm:
        assert_rom_checks(vm, CALL_CHECKS)
        assert one(vm, "rom_exit")["status"] == "0"
        wait_markers(vm, "audio", count=len(CALL_TRACE))
        tr = trace(vm)
        no_exception(vm)
    assert [t[:3] for t in tr] == CALL_TRACE, tr
    assert not pipos_markers(vm, "audio_drop"), vm.serial
    lasted(tr, 0, 80)       # boot beep
    lasted(tr, 2, 100)      # tone(1050, 100)
    # The 1100 Hz tone was replaced after about 50 ms, not after 5 s.
    lasted(tr, 4, 50 - CUT_MS, 1000)
    lasted(tr, 5, 100)      # tone(1200, 100)
    lasted(tr, 7, 50 - CUT_MS, 1000)  # stopped after about 50 ms
    for i, ms in zip(range(9, 13), (60, 40, 60, 50)):
        lasted(tr, i, ms)
    lasted(tr, 14, 50 - CUT_MS, 1000)  # 256 notes, stopped after about 50 ms


def test_exit_stops_sound(board, kernel_image, test_rom, tmp_path, need_mtools):
    """volume = 0 and beep = no: no boot beep, silent tones, and image end stops the tone."""
    sd = card(tmp_path, test_rom("audio_exit"), config(volume=0, beep="no"))
    with booted(kernel_image, board, sd=sd) as vm:
        start = rom_one(vm, "audio_start")
        assert (start["vol"], start["rc"], start["status"]) == ("0", "0", "1"), vm.serial
        assert one(vm, "rom_exit")["status"] == "0"
        wait_markers(vm, "audio", count=2)
        assert dump(vm), vm.serial
        tr = trace(vm)
        no_exception(vm)
    assert [t[:3] for t in tr] == [("tone", 1900, 0), ("stop", 0, 0)], tr
    # The ROM sleeps 100 ms, then exits; the tone was 30 s long.
    lasted(tr, 0, 100 - CUT_MS, 2000)
