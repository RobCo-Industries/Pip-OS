"""The sample_queue call on the target, through RobCo-Core's trace backend.

A chained chunk prints "ev=audio op=pcm_next len= crc= t_ms=" when it
takes over. The CRC is the CRC-32 of the bytes the kernel plays: the ROM
overwrites its buffer right after each call, so a CRC of the original
bytes shows the kernel copied them.

Durations come from t_ms: strict on the lower side, SLACK_MS on the upper
side for a loaded machine. The tests do not count PCM IRQs.
"""

import time
import zlib

from pipos_helpers import (assert_rom_checks, booted, config, dump, make_card, no_exception,
                           one, pipos_markers, rom_one, wait_markers)

ROM = "PIPBOY/ROM1.ROM"
CFG = "PIPBOY/PIPOS.CFG"
RATE = 8000
SLACK_MS = 150
CUT_MS = 10
LONG_N = 12
LONG_LEN = 2000

CHECKS = [
    "len0", "len65537", "len_max_u32", "kernel", "mmio", "slot_end", "null", "idle_status",
    "b_play", "b_queue", "b_busy", "b_len0", "b_kernel", "b_busy_max", "b_status", "b_wait",
    "c_first", "c_second", "c_status", "c_third", "c_third_waited", "c_status2", "c_wait",
    "c_done",
    "t_tone", "t_queue", "t_wait",
    "d1_play", "d1_queue", "d1_sample", "d1_wait",
    "d2_play", "d2_queue", "d2_tone", "d2_wait",
    "d3_play", "d3_queue", "d3_seq", "d3_wait",
    "d4_play", "d4_queue", "d4_stop", "d4_status", "d4_quiet",
    "u_first", "u_wait", "u_second", "u_wait2",
    "l_all", "l_wait",
]


def crc(seed, n):
    """The CRC-32 of audio_queue.c's fill(seed, n)."""
    return f"{zlib.crc32(bytes((i * 7 + seed * 31) & 0xff for i in range(n))):08x}"


def pcm(seed, n, vol=70):
    return ("pcm", n, vol, crc(seed, n))


def nxt(seed, n):
    return ("pcm_next", n, crc(seed, n))


END = ("pcm_end",)
STOP = ("stop", 0, 70)

# Every marker of audio_queue.c, in order. Nothing from a refused call.
TRACE = [
    pcm(1, 4000), nxt(2, 4000), END,                           # 0: busy
    pcm(10, 2400), nxt(11, 1600), nxt(12, 800), END,           # 3: three chunks
    ("tone", 2200, 70), pcm(13, 400), END,                     # 7: over a tone
    pcm(20, 8000), STOP, pcm(40, 800), END,                    # 10: sample drops it
    pcm(21, 8000), STOP, ("tone", 2300, 70), STOP,             # 14: tone drops it
    pcm(22, 8000), STOP, ("tone", 2400, 70), STOP,             # 18: play_seq drops it
    pcm(23, 8000), STOP,                                       # 22: audio_stop drops it
    pcm(50, 80), END, pcm(51, 80), END,                        # 24: underrun
    pcm(60, LONG_LEN)] + [nxt(60 + i, LONG_LEN) for i in range(1, LONG_N)] + [END]  # 28

LONG = 28


def key(m):
    op = m["op"]
    if op == "pcm":
        assert m["rate"] == str(RATE), m
        return ("pcm", int(m["len"]), int(m["vol"]), m["crc"])
    if op == "pcm_next":
        return ("pcm_next", int(m["len"]), m["crc"])
    if op == "pcm_end":
        return ("pcm_end",)
    return (op, int(m["hz"]), int(m["vol"]))


def trace(vm):
    return [(key(m), int(m["t_ms"])) for m in pipos_markers(vm, "audio")]


def span(tr, i, j, ms, slack=SLACK_MS):
    """From marker i to marker j took at least ms and at most ms + slack."""
    dt = tr[j][1] - tr[i][1]
    assert ms <= dt <= ms + slack, (i, j, dt, ms, tr)


def card(tmp_path, rom, cfg):
    return make_card(tmp_path / "sd.img", {ROM: rom, CFG: cfg})


def play_ms(n):
    return n * 1000 // RATE


def test_sample_queue(board, kernel_image, test_rom, tmp_path, need_mtools):
    sd = card(tmp_path, test_rom("audio_queue"), config(volume=70, beep="no"))
    with booted(kernel_image, board, sd=sd) as vm:
        assert_rom_checks(vm, CHECKS)
        assert one(vm, "rom_exit")["status"] == "0"
        wait_markers(vm, "audio", count=len(TRACE))
        tr = trace(vm)
        no_exception(vm)
    assert [t[0] for t in tr] == TRACE, tr
    assert not pipos_markers(vm, "audio_drop"), vm.serial
    # Each chunk plays in full, the next one right after it.
    span(tr, 0, 1, play_ms(4000))
    span(tr, 1, 2, play_ms(4000))
    span(tr, 3, 4, play_ms(2400))
    span(tr, 4, 5, play_ms(1600))
    span(tr, 5, 6, play_ms(800))
    span(tr, 3, 6, play_ms(4800), 300)
    span(tr, 8, 9, play_ms(400))
    for i in (10, 14, 18, 22):  # cut after about 50 ms, nothing chained
        span(tr, i, i + 1, 50 - CUT_MS, 1000)
    span(tr, 12, 13, play_ms(800))
    # The long chain: no restart, and at least the whole length.
    span(tr, LONG, LONG + LONG_N, play_ms(LONG_N * LONG_LEN), 500)


def test_exit_stops_both(board, kernel_image, test_rom, tmp_path, need_mtools):
    """The ROM exits with one chunk playing and one waiting: both stop."""
    sd = card(tmp_path, test_rom("audio_queue_exit"), config(volume=0, beep="no"))
    with booted(kernel_image, board, sd=sd) as vm:
        start = rom_one(vm, "queue_start")
        assert (start["play"], start["queue"], start["status"]) == ("0", "0", "1"), vm.serial
        assert one(vm, "rom_exit")["status"] == "0"
        wait_markers(vm, "audio", count=2)
        time.sleep(1.5)  # the waiting chunk would take over 500 ms after the start
        assert dump(vm), vm.serial
        tr = trace(vm)
        no_exception(vm)
    a = f"{zlib.crc32(bytes(i & 0xff for i in range(4000))):08x}"
    assert [t[0] for t in tr] == [("pcm", 4000, 0, a), ("stop", 0, 0)], tr
    span(tr, 0, 1, 100 - CUT_MS, 2000)
