"""The input calls with the input test ROM (tests/roms/input.c).

The test injects every event on the script channel: `key <name> [1|0]`
for buttons and `dial <n>` for the dial. The ROM prints ev=ph_<name>
when it is ready for the next step.
"""

import time

from pipos_helpers import (TIMEOUT, assert_rom_checks, config, make_card, no_exception, one,
                           pipos_markers, rom_markers)

from harness.boot import boot

CHECKS = [
    # start
    "dial_boot", "buttons_boot", "poll_empty", "poll_empty_untouched", "wait0_empty",
    "wait0_untouched", "wait_short", "wait_short_untouched", "wait_short_not_short",
    "wait_short_not_long", "wait_range_first", "wait_range_last", "wait_range_untouched",
    "wait_range_before_fault",
    # faults
    "poll_kernel", "poll_null", "poll_mmio1", "poll_mmio2", "wait_kernel", "wait_null_forever",
    "wait_mmio_timed", "poll_straddle", "wait_straddle", "straddle_intact",
    # sequence
    "seq_rc", "dial_seq",
    # hold
    "hold_rc", "hold_value", "buttons_held", "hold2_rc", "buttons_two", "release_rc",
    "release_value", "buttons_one", "release2_rc", "buttons_released",
    # dial
    "dial_back_rc", "dial_back_value", "dial_back", "dial_big_rc", "dial_big",
    # timed
    "timed_rc", "timed_early", "timed_event", "max_rc", "max_event",
    # overflow
    "overflow_count", "overflow_first", "overflow_last_dial", "overflow_in_order",
    "overflow_last_type", "overflow_last_code", "overflow_empty",
    # stale
    "stale_select_rc", "stale_press_rc", "stale_held", "stale_release_lost", "stale_released",
    # stamp
    "stamp_rc", "stamp_not_before", "stamp_not_after",
]

# Characters on the console: each one is an event, so RobCo-Core's queue of 32 stays full.
FLOOD = "x" * 4000

# Kept and dropped keys mixed: back, f1 and characters never reach the ROM.
SEQ_LINES = ["key up", "key back", "key down", "key left", "key f1", "key right", "char x",
             "key select", "key dial_push", "text ab", "dial 3", "dial -2"]
SEQ_EVENTS = [(1, b, v) for b in range(6) for v in (1, 0)] + [(2, 0, 3), (2, 0, -2)]


def script_ok(vm, line):
    reply = vm.script(line, timeout=TIMEOUT)
    assert reply.status == "ok", (line, reply)
    return reply


def at_phase(vm, name):
    vm.wait_for(f"@RC stage=testrom ev=ph_{name}\n", timeout=TIMEOUT)


def test_input_calls(board, kernel_image, test_rom, tmp_path, need_mtools):
    # A slow boot screen leaves time to inject events before the ROM starts.
    sd = make_card(tmp_path / "sd.img", {"PIPBOY/ROM1.ROM": test_rom("input"),
                                         "PIPBOY/PIPOS.CFG": config(delay=150, beep="no")})
    with boot(kernel_image, board, sd=sd, serial2=True, timeout=TIMEOUT) as vm:
        vm.wait_for("ev=input ", timeout=TIMEOUT)
        vm.send("dial 2\nkey up\n", port=1)

        at_phase(vm, "seq")
        # The kernel took the early events before the ROM ran, so they were its own.
        early = [(m["type"], m["code"], m["value"]) for m in pipos_markers(vm, "key")]
        assert early == [("dial", "0", "2"), ("key", "up", "1"), ("key", "up", "0")], vm.serial
        vm.wait_for("ok\nok\n", timeout=TIMEOUT, port=1)
        time.sleep(0.2)
        # The ROM blocks in wait_event: the kernel's commands still answer.
        assert script_ok(vm, "state").lines[0].startswith("ready=0 rom=1"), vm.serial
        for line in SEQ_LINES:
            script_ok(vm, line)

        at_phase(vm, "hold")
        script_ok(vm, "key select 1")
        at_phase(vm, "hold2")
        script_ok(vm, "key left 1")
        at_phase(vm, "release")
        script_ok(vm, "key select 0")
        at_phase(vm, "release2")
        script_ok(vm, "key left 0")

        at_phase(vm, "dial")
        script_ok(vm, "dial -10")
        at_phase(vm, "dial2")
        script_ok(vm, "dial 1000000")

        at_phase(vm, "timed")
        time.sleep(0.3)
        script_ok(vm, "dial 1")
        at_phase(vm, "max")
        time.sleep(0.3)
        script_ok(vm, "dial -1")

        at_phase(vm, "overflow")
        for i in range(1, 41):
            script_ok(vm, f"dial {i}")
        script_ok(vm, "key select 1")

        at_phase(vm, "stale_press")
        script_ok(vm, "key select 0")
        script_ok(vm, "key up 1")
        at_phase(vm, "stale")
        # While the ROM spins: a flood on the console, then the release on the
        # script channel. The kernel reads the console first at each poll, so the
        # flood fills the slot each pop frees and RobCo-Core drops the release.
        start = len(vm.lines(1))
        vm.send(FLOOD)
        vm.send("wait 20\nkey up 0\n", port=1)
        deadline = time.monotonic() + TIMEOUT
        while len(vm.lines(1)) < start + 2 and time.monotonic() < deadline:
            time.sleep(0.05)
        assert vm.lines(1)[start:start + 2] == ["ok", "err full"], vm.serial2

        at_phase(vm, "stamp")
        time.sleep(0.2)
        script_ok(vm, "dial 1")

        vm.wait_for("ev=rom_exit", timeout=TIMEOUT, line=True)
        assert_rom_checks(vm, CHECKS)
        assert one(vm, "rom_exit")["status"] == "0", vm.serial

        events = rom_markers(vm, "event")
        assert [(int(m["type"]), int(m["code"]), int(m["value"])) for m in events] \
            == SEQ_EVENTS, vm.serial
        times = [int(m["time"]) for m in events]
        assert times == sorted(times) and times[0] > 0, times
        no_exception(vm)
