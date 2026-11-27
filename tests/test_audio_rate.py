"""The sample_rate call of ABI 1.5 on the target, through the trace backend.

Each play prints "ev=audio op=pcm rate=" with the rate it started with.
A queued buffer prints "op=pcm_next" and plays at the rate of its play.
Durations come from t_ms: strict on the lower side, SLACK_MS on the upper
side for a loaded machine, as in test_audio_sample.py.
"""

import zlib

from pipos_helpers import (assert_rom_checks, booted, config, make_card, no_exception, one,
                           pipos_markers, wait_markers)

ROM = "PIPBOY/ROM1.ROM"
CFG = "PIPBOY/PIPOS.CFG"
TAPE = "PIPBOY/HOLO/TAPE.HTP"
SLACK_MS = 150
SAME_LEN = 3750

CHECKS = [
    "start_8000", "bad_0", "bad_44100", "bad_37501", "bad_keeps",
    "slow", "slow_wait", "to_hi", "fast", "fast_wait",
    "to_lo", "q_first", "q_to_hi", "q_playing", "q_joins", "q_wait", "q_new", "q_new_wait",
    "tape", "back_hi", "after_tape", "after_tape_wait",
]


def crc(seed, n):
    """The CRC-32 of audio_rate.c's fill(seed, n)."""
    return f"{zlib.crc32(bytes((i * 7 + seed * 31) & 0xff for i in range(n))):08x}"


def pcm(rate, seed, n):
    return ("pcm", rate, n, crc(seed, n))


END = ("pcm_end",)

# Every marker of audio_rate.c and the tape's "rate", in order.
TRACE = [
    pcm(8000, 1, SAME_LEN), END,                         # 0: the buffer at 8000 Hz
    pcm(37500, 1, SAME_LEN), END,                        # 2: the same buffer at 37500 Hz
    pcm(8000, 2, 1600), ("pcm_next", 800, crc(3, 800)), END,  # 4: the queue keeps 8000
    pcm(37500, 4, SAME_LEN), END,                        # 7: the next play takes 37500
    pcm(37500, 6, 375), END,                             # 9: the tape, at 37500 it set
    pcm(37500, 5, SAME_LEN), END,                        # 11: the ROM's 37500 is back
]


def key(m):
    op = m["op"]
    if op == "pcm":
        return ("pcm", int(m["rate"]), int(m["len"]), m["crc"])
    if op == "pcm_next":
        return ("pcm_next", int(m["len"]), m["crc"])
    if op == "pcm_end":
        return END
    return (op,)


def lasted(tr, i, j, ms):
    """From marker i to marker j took at least ms and at most ms + SLACK_MS."""
    dt = tr[j][1] - tr[i][1]
    assert ms <= dt <= ms + SLACK_MS, (i, j, dt, ms, tr)


def play_ms(n, rate):
    return n * 1000 // rate


def test_sample_rate(board, kernel_image, test_rom, test_holo, tmp_path, need_mtools):
    sd = make_card(tmp_path / "sd.img", {
        ROM: test_rom("audio_rate"),
        CFG: config(volume=70, beep="no"),
        TAPE: test_holo("tape"),
    })
    with booted(kernel_image, board, sd=sd) as vm:
        assert_rom_checks(vm, CHECKS)
        assert one(vm, "rom_exit")["status"] == "0"
        wait_markers(vm, "audio", count=len(TRACE))
        tr = [(key(m), int(m["t_ms"])) for m in pipos_markers(vm, "audio")]
        no_exception(vm)
    assert [t[0] for t in tr] == TRACE, tr
    assert not pipos_markers(vm, "audio_drop"), vm.serial
    lasted(tr, 0, 1, play_ms(SAME_LEN, 8000))     # 468 ms
    lasted(tr, 2, 3, play_ms(SAME_LEN, 37500))    # 100 ms
    lasted(tr, 4, 6, play_ms(1600 + 800, 8000))   # both buffers at 8000: 300 ms
    lasted(tr, 7, 8, play_ms(SAME_LEN, 37500))
    lasted(tr, 9, 10, play_ms(375, 37500))        # 10 ms
    lasted(tr, 11, 12, play_ms(SAME_LEN, 37500))
