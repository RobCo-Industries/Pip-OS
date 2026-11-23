"""The input calls with the input test ROM (tests/roms/input.c).

The test injects events on the script channel (`key <name> [1|0]`,
`dial <n>`) and on the console. The ROM prints ev=ph_<name> when it is
ready for the next step. Every injected line waits for its "ok" or "err"
reply, and every phase waits for its marker, so no event can reach the
wrong phase.

The kernel reads the UARTs only when it polls. Two steps put bytes in the
UART FIFOs while nothing polls, so that one poll reads them all: before
the ROM starts (the boot screen) and the "flood" phase (the ROM spins).
The mini UART FIFO holds 8 bytes and the console FIFO 16, so each of
these steps sends at most that much. Bytes beyond a FIFO wait in QEMU and
come in at some later poll.
"""

from pipos_helpers import (TIMEOUT, assert_rom_checks, config, make_card, no_exception, one,
                           rom_markers)

from harness.boot import boot, parse_markers

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
    "timed_rc", "timed_waited", "timed_early", "timed_event", "max_rc", "max_waited",
    "max_event",
    # overflow
    "overflow_count", "overflow_first", "overflow_last_dial", "overflow_in_order",
    "overflow_last_type", "overflow_last_code", "overflow_empty",
    # flood
    "flood_select_rc", "flood_select_value", "flood_count", "flood_in_order",
    "flood_last_type", "flood_last_value", "flood_released",
    # stamp
    "stamp_rc", "stamp_not_before", "stamp_not_after",
    # repeat
    "repeat_press_rc", "repeat_press_value", "repeat_codes", "repeat_count",
    "repeat_not_early", "repeat_not_late", "repeat_release_value", "repeat_quiet",
    "repeat_released",
]

# Before the ROM starts: Enter on the console (select, pressed and
# released) and one 7-byte script line. Each fits its FIFO.
EARLY_CONSOLE = "\r"
EARLY_LINE = "dial 2"
EARLY = [("key", "select", "1"), ("key", "select", "0"), ("dial", "0", "2")]

# The flood phase: 16 CRs (select, pressed and released) are 32 events in
# 16 bytes. Then "dial 1" (7 bytes) is the 33rd: RobCo-Core's queue drops
# its oldest event, the first press, and keeps the newest 32.
FLOOD_CONSOLE = "\r" * 16
FLOOD_LINE = "dial 1"

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


def key_markers_before_jump(vm):
    """(type, code, value) of each ev=key marker the kernel printed before ev=jump."""
    text = vm.serial
    jump = text.index("@RC stage=pipos ev=jump ")
    return [(m["type"], m["code"], m["value"]) for m in parse_markers(text[:jump])
            if m.get("stage") == "pipos" and m.get("ev") == "key"]


def test_input_calls(board, kernel_image, test_rom, tmp_path, need_mtools):
    # A slow boot screen: the early bytes reach the UART FIFOs long before
    # the kernel drains them at image start.
    sd = make_card(tmp_path / "sd.img", {"PIPBOY/ROM1.ROM": test_rom("input"),
                                         "PIPBOY/PIPOS.CFG": config(delay=150, beep="no")})
    with boot(kernel_image, board, sd=sd, serial2=True, timeout=TIMEOUT) as vm:
        vm.wait_for("ev=input ", timeout=TIMEOUT)
        vm.send(EARLY_CONSOLE)
        # The reply comes from the kernel's drain at image start.
        script_ok(vm, EARLY_LINE)
        vm.wait_for("@RC stage=pipos ev=jump ", timeout=TIMEOUT)
        early = key_markers_before_jump(vm)
        assert early == EARLY, (
            "the early input did not reach the UART FIFOs before the ROM started: "
            f"the boot screen was too short on this host. Taken before ev=jump: {early}\n"
            f"{vm.serial}")

        at_phase(vm, "seq")
        # Only the ROM's wait_event polls now: the kernel's commands still answer.
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

        # The kernel reads "wait" inside the ROM's wait_event and holds the
        # next line back 300 ms: the ROM checks that its wait blocked that long.
        at_phase(vm, "timed")
        script_ok(vm, "wait 300")
        script_ok(vm, "dial 1")
        at_phase(vm, "max")
        script_ok(vm, "wait 300")
        script_ok(vm, "dial -1")

        at_phase(vm, "overflow")
        for i in range(1, 41):
            script_ok(vm, f"dial {i}")
        script_ok(vm, "key select 1")

        at_phase(vm, "flood_press")
        script_ok(vm, "key select 0")
        at_phase(vm, "flood")
        # The ROM spins without polling. Its next poll reads both FIFOs in one
        # pass, the console first: the dial step drops the oldest event.
        vm.send(FLOOD_CONSOLE)
        reply = vm.script(FLOOD_LINE, timeout=TIMEOUT)
        assert reply.status == "ok dropped", (
            "the flood bytes did not all reach the UART FIFOs while the ROM spun: "
            f"{reply}\n{vm.serial}")

        at_phase(vm, "stamp")
        script_ok(vm, "dial 1")

        # Held until the ROM saw three repeats.
        at_phase(vm, "repeat")
        script_ok(vm, "key down 1")
        at_phase(vm, "repeat_release")
        script_ok(vm, "key down 0")

        vm.wait_for("ev=rom_exit", timeout=TIMEOUT, line=True)
        assert_rom_checks(vm, CHECKS)
        assert one(vm, "rom_exit")["status"] == "0", vm.serial

        events = rom_markers(vm, "event")
        assert [(int(m["type"]), int(m["code"]), int(m["value"])) for m in events] \
            == SEQ_EVENTS, vm.serial
        times = [int(m["time"]) for m in events]
        assert times == sorted(times) and times[0] > 0, times
        no_exception(vm)
