"""The disk click under a ROM.

With [audio] system_sound on, the kernel keeps clicking while a ROM runs,
in the ROM pace: at most 8 clicks owed, none in the first 700 ms after the
jump, none while a ROM sound plays or in the 300 ms after it ends. The
debt comes from the card and slot calls the ROM makes. A ROM call that
starts or stops a sound stops a click that plays, so the ROM's sound always
wins. audio_status and volume cut nothing, audio_status and sample_queue
act as if the click were silence, and a refused call holds nothing back.

The jump marker has no t_ms. The test ROMs print their own "t0" mark with
the kernel's time first thing, after the jump, so a click 700 ms after t0
is 700 ms after the jump too. t0 is on the kernel's tick clock and a click
on the audio clock: see AUDIO_CLOCK_LAG_MS.
"""

import time

from pipos_helpers import (AUDIO_CLOCK_LAG_MS, assert_rom_checks, booted, make_card, no_exception,
                           one, rom_one, wait_markers)

import test_audio_queue as queue

ROM = "PIPBOY/ROM1.ROM"
CFG = "PIPBOY/PIPOS.CFG"
CLICK_LEN = "789"


def cfg(delay=0, beep="no", volume=70):
    """Typing at delay ms per character, 0 for off, and the system sound on."""
    return (f"[display]\nchar_delay_ms = {delay}\nline_delay_ms = 0\n"
            f"[audio]\nvolume = {volume}\nbeep = {beep}\nsystem_sound = yes\n").encode()


def is_click(m):
    return m["op"] == "pcm" and m.get("len") == CLICK_LEN and m.get("rate") == "37500"


def after_jump(vm):
    """The pipos audio records that come after ev=jump in the serial log."""
    out, seen = [], False
    for m in vm.markers():
        if m.get("stage") != "pipos":
            continue
        if m.get("ev") == "jump":
            seen = True
        elif seen and m.get("ev") == "audio":
            out.append(m)
    assert seen, vm.serial
    return out


def check_cuts(tr):
    """A click ends by itself with pcm_end, or one op=stop cuts it and the
    ROM's own start comes next. Returns the number of cut clicks."""
    cuts = 0
    for i, m in enumerate(tr):
        if not is_click(m):
            continue
        assert i + 1 < len(tr), tr[i:]
        nxt = tr[i + 1]
        if nxt["op"] == "pcm_end":
            continue
        assert nxt["op"] == "stop", tr[i:i + 3]
        assert i + 2 < len(tr), tr[i:]
        rom = tr[i + 2]
        assert rom["op"] in ("tone", "pcm", "voice") and not is_click(rom), tr[i:i + 3]
        cuts += 1
    return cuts


def without_clicks(tr):
    """The trace with every click and the record that ends it taken out."""
    out, skip = [], False
    for m in tr:
        if skip:
            skip = False
            assert m["op"] in ("pcm_end", "stop"), m
            continue
        if is_click(m):
            skip = True
            continue
        out.append(m)
    return out


def boot_rom(kernel_image, board, tmp_path, rom, delay=0, **kw):
    sd = make_card(tmp_path / "sd.img", {ROM: rom, CFG: cfg(delay=delay, **kw)})
    return booted(kernel_image, board, sd=sd)


def test_a_reading_rom_gets_at_most_8_clicks(board, kernel_image, test_rom, tmp_path,
                                             need_mtools):
    with boot_rom(kernel_image, board, tmp_path, test_rom("click_read")) as vm:
        assert one(vm, "rom_exit")["status"] == "0"
        no_exception(vm)
    t0 = int(rom_one(vm, "t0")["t_ms"])
    tr = after_jump(vm)
    clicks = [m for m in tr if is_click(m)]
    assert 1 <= len(clicks) <= 8, tr
    assert int(clicks[0]["t_ms"]) - t0 >= 700 - AUDIO_CLOCK_LAG_MS, (t0, clicks[0])
    # Nothing else plays: every click ends by itself.
    assert check_cuts(tr) == 0, tr
    assert not [m for m in vm.markers() if m.get("ev") == "audio_drop"], vm.serial


def test_reads_in_the_first_700_ms_are_owed_with_typing_on(board, kernel_image, test_rom,
                                                          tmp_path, need_mtools):
    """Typing at 1 ms per character and no line delay: the smallest delays
    that still close the gate. The note does not fit in that text, so it
    does not play, but the quiet second and the scroll run. The ROM reads
    its files at once after the jump, then sleeps. Those reads are owed:
    the clicks come once the 700 ms have passed."""
    with boot_rom(kernel_image, board, tmp_path, test_rom("click_read"), delay=1) as vm:
        assert one(vm, "rom_exit")["status"] == "0"
        no_exception(vm)
    assert one(vm, "quiet")
    assert one(vm, "scroll")
    t0 = int(rom_one(vm, "t0")["t_ms"])
    tr = after_jump(vm)
    clicks = [m for m in tr if is_click(m)]
    print(f"clicks={len(clicks)}")
    assert 1 <= len(clicks) <= 8, tr
    assert int(clicks[0]["t_ms"]) - t0 >= 700 - AUDIO_CLOCK_LAG_MS, (t0, clicks[0])
    assert check_cuts(tr) == 0, tr


def test_sample_queue_is_never_busy_over_a_click(board, kernel_image, test_rom, tmp_path,
                                                 need_mtools):
    """Two queued samples end, a click plays, and sample_queue comes in: it
    cuts the click and starts the data. It never answers busy."""
    with boot_rom(kernel_image, board, tmp_path, test_rom("click_queue")) as vm:
        assert_rom_checks(vm, ["queue_starts"] * 39)
        assert one(vm, "rom_exit")["status"] == "0"
        no_exception(vm)
    # The case ran: a click cut by one op=stop, then the ROM's sample.
    tr = after_jump(vm)
    cuts = sum(1 for a, b, c in zip(tr, tr[1:], tr[2:])
               if is_click(a) and b["op"] == "stop" and c["op"] == "pcm" and c.get("len") == "80")
    print(f"cut={cuts}")
    assert cuts >= 1, tr


def test_a_rom_that_reads_nothing_gets_no_click(board, kernel_image, test_rom, tmp_path,
                                                need_mtools):
    """The sleep ROM makes no card call. Typing is on, so the quiet second
    dropped the boot's debt and the ROM load is not owed. pip_sleep_ms polls
    the trace, so a click would show."""
    sd = make_card(tmp_path / "sd.img", {ROM: test_rom("sleep"), CFG: cfg(delay=1)})
    with booted(kernel_image, board, sd=sd, wait="jump") as vm:
        time.sleep(3.0)
        no_exception(vm)
    assert not [m for m in after_jump(vm) if is_click(m)], vm.serial


def test_a_rom_sound_always_wins(board, kernel_image, test_rom, tmp_path, need_mtools):
    with boot_rom(kernel_image, board, tmp_path, test_rom("click_tone")) as vm:
        assert_rom_checks(vm, ["long_tone", "status_quiet"])
        assert one(vm, "rom_exit")["status"] == "0"
        no_exception(vm)
    tr = after_jump(vm)
    assert not [m for m in vm.markers() if m.get("ev") == "audio_drop"], vm.serial
    # No click starts while the long tone sounds.
    i = next(i for i, m in enumerate(tr) if m["op"] == "tone" and m["hz"] == "500")
    start = int(tr[i]["t_ms"])
    end = next(int(m["t_ms"]) for m in tr[i + 1:] if m["op"] in ("stop", "tone", "pcm"))
    assert end - start >= 2000 - AUDIO_CLOCK_LAG_MS, tr[i:i + 2]
    assert not [m for m in tr if is_click(m) and start <= int(m["t_ms"]) <= end], tr
    # The reads under the tone owed clicks, and they came after it.
    clicks = [m for m in tr if is_click(m)]
    assert clicks, tr
    # One op=stop cuts a click that plays when the ROM starts a sound.
    cuts = check_cuts(tr)
    print(f"clicks={len(clicks)} cut={cuts}")
    assert cuts >= 1, tr
    # Without the clicks the ROM's own trace is whole: 41 tones, each with its end.
    rest = without_clicks(tr)
    assert [m["op"] for m in rest] == ["tone", "stop"] * 41, rest


def whole_clicks(tr):
    """Every click ends by itself: op=pcm, then its own op=pcm_end, no op=stop."""
    clicks = [i for i, m in enumerate(tr) if is_click(m)]
    for i in clicks:
        assert i + 1 < len(tr) and tr[i + 1]["op"] == "pcm_end", tr[i:i + 2]
    assert not [m for m in tr if m["op"] == "stop"], tr
    return len(clicks)


def test_status_every_10_ms_cuts_no_click(board, kernel_image, test_rom, tmp_path,
                                           need_mtools):
    """A UI loop asks audio_status every 10 ms while it reads. The clicks
    under it play whole, and every answer is 0."""
    with boot_rom(kernel_image, board, tmp_path, test_rom("click_status")) as vm:
        assert_rom_checks(vm, ["status_idle"] * 30)
        assert one(vm, "rom_exit")["status"] == "0"
        no_exception(vm)
    tr = after_jump(vm)
    n = whole_clicks(tr)
    print(f"clicks={n}")
    assert n >= 1, tr
    assert all(is_click(m) or m["op"] == "pcm_end" for m in tr), tr


def test_refused_calls_hold_no_click(board, kernel_image, test_rom, tmp_path, need_mtools):
    """A tone refused every 100 ms starts no sound, so it never restarts the
    300 ms of quiet: the clicks still play, whole."""
    with boot_rom(kernel_image, board, tmp_path, test_rom("click_refused")) as vm:
        assert_rom_checks(vm, ["all_refused"])
        assert one(vm, "rom_exit")["status"] == "0"
        no_exception(vm)
    tr = after_jump(vm)
    n = whole_clicks(tr)
    print(f"clicks={n}")
    assert n >= 1, tr
    assert all(is_click(m) or m["op"] == "pcm_end" for m in tr), tr


def test_sample_queue_with_clicks(board, kernel_image, test_rom, tmp_path, need_mtools):
    """test_audio_queue's ROM with the system sound on and typing off: the
    ROM load's debt, cut to 8, pays out while the ROM plays. Without the
    clicks the trace is the strict one, and every check of the ROM passes."""
    def attempt():
        with boot_rom(kernel_image, board, tmp_path, test_rom("audio_queue")) as vm:
            assert_rom_checks(vm, queue.CHECKS)
            assert one(vm, "rom_exit")["status"] == "0"
            gap = int(rom_one(vm, "l_gap")["max_ms"])
            wait_markers(vm, "audio", count=len(queue.TRACE))
            time.sleep(0.5)
            tr = after_jump(vm)
            no_exception(vm)
        clicks = [m for m in tr if is_click(m)]
        check_cuts(tr)
        rest = [(queue.key(m), int(m["t_ms"])) for m in without_clicks(tr)]
        print(f"clicks={len(clicks)}")
        assert [t[0] for t in rest[:queue.LONG]] == queue.TRACE[:queue.LONG], rest
        queue.judge_long_chain(rest[queue.LONG:], gap)

    queue.judged(attempt)


def test_rom_end_drops_the_debt(board, kernel_image, test_rom, tmp_path, need_mtools):
    """The ROM sleeps past the 700 ms, reads and exits at once, with clicks
    owed. A click that plays when it returns ends, and its records print
    before ev=rom_exit. No click starts from the ROM's return on: none
    between ev=rom_exit and ev=ready, none after ev=ready.

    In QEMU the kernel's work after the return is short, so this window
    rarely holds a click even without the fix. tests/host/test_dsound.c
    pins the rule itself."""
    with boot_rom(kernel_image, board, tmp_path, test_rom("click_exit"), delay=1) as vm:
        assert one(vm, "rom_exit")["status"] == "0"
        wait_markers(vm, "ready")
        time.sleep(1.5)
        no_exception(vm)
    seq = [m for m in vm.markers() if m.get("stage") in ("pipos", "testrom")]
    read = next(i for i, m in enumerate(seq) if m.get("stage") == "testrom" and m.get("ev") == "read")
    exit_ = next(i for i, m in enumerate(seq) if m.get("ev") == "rom_exit")
    assert read < exit_, seq
    late = [m for m in seq[read:] if m.get("ev") == "audio"]
    late_clicks = [m for m in seq[exit_:] if m.get("ev") == "audio"]
    print(f"between_read_and_exit={len(late)}")
    assert not late_clicks, seq[exit_:]
