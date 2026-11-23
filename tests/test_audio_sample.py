"""The sample call on the target, through RobCo-Core's trace backend.

A sample prints "ev=audio op=pcm rate= len= vol= crc= t_ms=" at its start
and "op=pcm_end n= last= t_ms=" at its natural end. A stop or a
replacement prints "op=stop" instead. The CRC is the CRC-32 of the bytes
the kernel plays: the ROM overwrites its buffer right after each call, so
a CRC of the original bytes shows the kernel copied them.

Durations come from t_ms: strict on the lower side, SLACK_MS on the upper
side for a loaded machine. The tests do not count PCM IRQs: QEMU delivers
far fewer timer IRQs than samples on some hosts.
"""

import zlib

from pipos_helpers import (AUDIO_CLOCK_LAG_MS, assert_rom_checks, booted, config, dump, make_card,
                           no_exception, one, pipos_markers, rom_one, wait_markers)

ROM = "PIPBOY/ROM1.ROM"
CFG = "PIPBOY/PIPOS.CFG"
RATE = 8000
SLACK_MS = 150

CHECKS = [
    "len0", "len65537", "len_max_u32", "kernel", "mmio", "slot_end", "null", "idle_status",
    "tone_2100", "err_len0", "err_kernel", "err_busy", "err_stop",
    "s800", "s800_busy", "s800_wait", "s800_done",
    "s1", "s1_wait",
    "s65536", "s65536_busy", "s65536_stop", "s65536_quiet",
    "tone_2200", "over_tone", "over_tone_wait",
    "s4000", "tone_2300", "tone_over_wait",
    "s8000", "over_sample", "over_sample_wait",
    "s8000b", "seq_over", "seq_over_wait",
    "vol_40", "s80", "s80_wait", "vol_back",
]


def crc(seed, n):
    """The CRC-32 of audio_sample.c's fill(seed, n)."""
    return f"{zlib.crc32(bytes((i * 7 + seed * 31) & 0xff for i in range(n))):08x}"


def pcm(seed, n, vol=70):
    return ("pcm", n, vol, crc(seed, n))


# Every marker of audio_sample.c, in order. Nothing from a refused call.
TRACE = [
    ("tone", 2100, 70), ("stop", 0, 70),          # errors leave the tone alone
    pcm(1, 800), ("pcm_end",),
    pcm(2, 1), ("pcm_end",),
    pcm(3, 65536), ("stop", 0, 70),               # audio_stop
    ("tone", 2200, 70), pcm(4, 400), ("pcm_end",),  # sample over tone
    pcm(5, 4000), ("stop", 0, 70), ("tone", 2300, 70), ("stop", 0, 70),  # tone over sample
    pcm(6, 8000), ("stop", 0, 70), pcm(7, 800), ("pcm_end",),  # sample over sample
    pcm(8, 8000), ("stop", 0, 70), ("tone", 2400, 70), ("stop", 0, 70),  # play_seq over sample
    pcm(9, 80, 40), ("pcm_end",),                  # volume 40
]


def key(m):
    op = m["op"]
    if op == "pcm":
        assert m["rate"] == str(RATE), m
        return ("pcm", int(m["len"]), int(m["vol"]), m["crc"])
    if op == "pcm_end":
        return ("pcm_end",)
    return (op, int(m["hz"]), int(m["vol"]))


def trace(vm):
    return [(key(m), int(m["t_ms"])) for m in pipos_markers(vm, "audio")]


def lasted(tr, i, ms, slack=SLACK_MS):
    """Marker i lasted at least ms and at most ms + slack, up to marker i + 1."""
    dt = tr[i + 1][1] - tr[i][1]
    assert ms <= dt <= ms + slack, (i, dt, ms, tr)


def cut(tr, i, ms, slack=1000):
    """Marker i is a sound the ROM cut with a call after pip_sleep_ms(ms).

    The tick that starts the sound runs up to one period after the call,
    plus its interrupt latency, which a loaded host makes unbounded. So
    marker i to the cut can be shorter than ms - 10. The ROM made the
    call after marker i - 1, the end of the sound it waited for, and the cut
    comes at least ms after the call. So the cut is at least ms after
    marker i - 1.
    """
    assert tr[i + 1][-1] - tr[i - 1][-1] >= ms, (i, ms, tr)
    assert tr[i + 1][-1] - tr[i][-1] <= ms + slack, (i, ms, tr)


def cut_from_mark(tr, i, t0, ms, slack):
    """Marker i is a sound started right after the ROM's t0 mark and cut ms later.

    The tick that starts the sound can run late on a loaded host, so marker
    i to the cut can be shorter than ms. The ROM printed t0, the kernel's
    time, before the call, and the cut comes at least ms after the call.
    t0 is on the kernel's tick clock and the cut on the audio clock: see
    AUDIO_CLOCK_LAG_MS.
    """
    assert tr[i + 1][-1] - t0 >= ms - AUDIO_CLOCK_LAG_MS, (i, t0, ms, tr)
    assert tr[i + 1][-1] - tr[i][-1] <= ms + slack, (i, ms, tr)


def card(tmp_path, rom, cfg):
    return make_card(tmp_path / "sd.img", {ROM: rom, CFG: cfg})


def test_sample(board, kernel_image, test_rom, tmp_path, need_mtools):
    sd = card(tmp_path, test_rom("audio_sample"), config(volume=70, beep="no"))
    with booted(kernel_image, board, sd=sd) as vm:
        assert_rom_checks(vm, CHECKS)
        t0 = int(rom_one(vm, "t0_2100")["t_ms"])
        assert one(vm, "rom_exit")["status"] == "0"
        wait_markers(vm, "audio", count=len(TRACE))
        tr = trace(vm)
        no_exception(vm)
    assert [t[0] for t in tr] == TRACE, tr
    assert not pipos_markers(vm, "audio_drop"), vm.serial
    cut_from_mark(tr, 0, t0, 100, 1000)  # the 5 s tone ran until audio_stop, about 100 ms
    lasted(tr, 2, 800 * 1000 // RATE)  # 800 bytes: 100 ms
    lasted(tr, 4, 0)                   # 1 byte
    cut(tr, 6, 50)                     # 65536 bytes, stopped after about 50 ms
    cut(tr, 8, 50)                     # the tone, replaced after about 50 ms
    lasted(tr, 9, 400 * 1000 // RATE)  # 400 bytes: 50 ms
    cut(tr, 11, 50)                    # 4000 bytes, replaced by a tone
    lasted(tr, 13, 100)                # tone(2300, 100) in full
    cut(tr, 15, 50)                    # 8000 bytes, cut by the next sample
    lasted(tr, 17, 800 * 1000 // RATE)  # the replacing sample in full
    cut(tr, 19, 50)                    # 8000 bytes, cut by play_seq
    lasted(tr, 21, 60)                 # the note in full
    lasted(tr, 23, 80 * 1000 // RATE)  # 80 bytes: 10 ms
    # A replacement stops the old sample and starts the new sound on the same tick.
    for i in (12, 16, 20):
        assert tr[i][1] == tr[i + 1][1], (i, tr)


def test_exit_stops_sample(board, kernel_image, test_rom, tmp_path, need_mtools):
    """The ROM starts an 8.192 s sample and exits after 100 ms: image end stops it."""
    sd = card(tmp_path, test_rom("audio_pcm_exit"), config(volume=0, beep="no"))
    with booted(kernel_image, board, sd=sd) as vm:
        t0 = int(rom_one(vm, "t0")["t_ms"])
        start = rom_one(vm, "sample_start")
        assert (start["rc"], start["status"]) == ("0", "1"), vm.serial
        assert one(vm, "rom_exit")["status"] == "0"
        wait_markers(vm, "audio", count=2)
        assert dump(vm), vm.serial
        tr = trace(vm)
        no_exception(vm)
    data_crc = f"{zlib.crc32(bytes(i & 0xff for i in range(65536))):08x}"
    assert [t[0] for t in tr] == [("pcm", 65536, 0, data_crc), ("stop", 0, 0)], tr
    cut_from_mark(tr, 0, t0, 100, 2000)
