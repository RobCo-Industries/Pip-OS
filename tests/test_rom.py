"""Runs the test ROMs of tests/roms/ in user mode: exit paths, entry state,
system calls, faults, and the kernel's script commands while a ROM runs.

The ROMs print "@RC stage=testrom ..." lines through the log call.
"""

import pytest

from harness.boot import boot
from harness.qemu import QemuError

from pipos_helpers import (TIMEOUT, booted, dump, make_card, no_exception, one,
                           pipos_markers)

ROM = "PIPBOY/ROM1.ROM"

STARTUP = 0x017F0000
STUB = 0x017F0080
STACK_TOP = 0x01800000
HEAP_BASE = 0x01000000
HEAP_SIZE = 0x007F0000
PIPOS_VERSION = (7 << 16) | (1 << 8) | 0


def rom_markers(vm, ev):
    return [m for m in vm.markers() if m.get("stage") == "testrom" and m.get("ev") == ev]


def rom_one(vm, ev):
    found = rom_markers(vm, ev)
    assert len(found) == 1, f"expected one testrom ev={ev}, got {found}\n{vm.serial}"
    return found[0]


def card(tmp_path, data):
    return make_card(tmp_path / "sd.img", {ROM: data})


def state(vm):
    reply = vm.script("state", timeout=TIMEOUT)
    assert reply.status == "ok" and len(reply.lines) == 1, reply
    return dict(field.split("=") for field in reply.lines[0].split())


def test_log_and_exit(board, kernel_image, test_rom, tmp_path, need_mtools):
    with booted(kernel_image, board, sd=card(tmp_path, test_rom("hello"))) as vm:
        assert one(vm, "load")["name"] == "hello", vm.serial
        assert one(vm, "jump")["entry"] == "0x00400000"
        assert one(vm, "rom_exit")["status"] == "7"
        assert one(vm, "ready")["rom"] == "1"
        serial = vm.serial
        order = [serial.index(s) for s in ("ev=load", "ev=jump entry=0x00400000",
                                           "hello from the test rom\n",
                                           "ev=rom_exit status=7", "ev=ready rom=1")]
        assert order == sorted(order), serial
        assert not pipos_markers(vm, "rom_kill")
        st = state(vm)
        assert (st["ready"], st["rom"]) == ("1", "1"), st
        no_exception(vm)


def test_return_from_main(board, kernel_image, test_rom, tmp_path, need_mtools):
    with booted(kernel_image, board, sd=card(tmp_path, test_rom("ret5"))) as vm:
        assert one(vm, "rom_exit")["status"] == "5", vm.serial
        assert one(vm, "ready")["rom"] == "1"


def test_return_through_trampoline(board, kernel_image, test_rom, tmp_path, need_mtools):
    with booted(kernel_image, board, sd=card(tmp_path, test_rom("tramp"))) as vm:
        assert one(vm, "rom_exit")["status"] == "9", vm.serial
        assert "trampoline came back" not in vm.serial


def test_entry_state(board, kernel_image, test_rom, tmp_path, need_mtools):
    with booted(kernel_image, board, sd=card(tmp_path, test_rom("entry"))) as vm:
        regs = {k: int(v, 16) for k, v in rom_one(vm, "entry").items()
                if k not in ("stage", "ev")}
        assert regs == {"r0": STARTUP, "r1": 0, "r2": 0, "r3": 0, "sp": STACK_TOP,
                        "lr": STUB, "main_arg": STARTUP}, vm.serial
        s = rom_one(vm, "startup")
        fields = {k: int(v, 16) for k, v in s.items()
                  if k not in ("stage", "ev", "args_zero")}
        assert fields == {"abi_version": 0x00010000, "size": 128, "heap_base": HEAP_BASE,
                          "heap_size": HEAP_SIZE, "stack_top": STACK_TOP, "kind": 0,
                          "flags": 0}, vm.serial
        assert s["args_zero"] == "1"
        assert one(vm, "rom_exit")["status"] == "0"


@pytest.fixture
def calls(board, kernel_image, test_rom, tmp_path, need_mtools):
    """The boot of the calls ROM, after it ended."""
    with booted(kernel_image, board, sd=card(tmp_path, test_rom("calls"))) as vm:
        rom_one(vm, "done")
        assert one(vm, "rom_exit")["status"] == "0", vm.serial
        no_exception(vm)
        yield vm


def test_ticks_and_sleep(calls):
    t = rom_one(calls, "ticks")
    a, b, c = int(t["a"]), int(t["b"]), int(t["c"])
    assert t["sleep_rc"] == "0"
    assert 0 <= a <= b <= c < 2**31, t
    assert 50 <= b - a < 1000, t


def test_sleep_range(calls):
    m = rom_one(calls, "sleep_range")
    assert (m["big"], m["max"], m["ok"]) == ("-34", "-34", "0"), m
    assert 0 <= int(m["fast_ms"]) < 20, m
    assert 20 <= int(m["slept_ms"]) < 1000, m


def test_sys_info(calls):
    boot_m = one(calls, "boot")
    s = {k: int(v, 16) for k, v in rom_one(calls, "sys_info").items()
         if k not in ("stage", "ev", "rc")}
    assert rom_one(calls, "sys_info")["rc"] == "0"
    assert s == {"size": 32, "abi_version": 0x00010000, "pipos_version": PIPOS_VERSION,
                 "board_rev": int(boot_m["board_rev"], 16),
                 "soc": int(boot_m["soc"].removeprefix("bcm"), 16),
                 "mem_size": int(boot_m["mem"], 16), "reserved0": 0, "reserved1": 0}, s


def test_mem_info(calls):
    m = rom_one(calls, "mem_info")
    assert m["rc"] == "0"
    assert {k: int(m[k], 16) for k in ("heap_base", "heap_size", "stack_top", "stack_size")} == \
        {"heap_base": HEAP_BASE, "heap_size": HEAP_SIZE, "stack_top": STACK_TOP,
         "stack_size": 0x10000}, m


def test_rand(calls):
    values = [int(v) for k, v in rom_one(calls, "rand").items() if k.startswith("v")]
    assert len(values) == 8
    assert all(0 <= v < 2**31 for v in values), values
    assert len(set(values)) > 1, values


def test_log_checks(calls):
    m = rom_one(calls, "log")
    assert (m["bad"], m["cross"], m["nonul"], m["max"], m["ctrl"]) == \
        ("-14", "-14", "-22", "0", "0"), m
    assert "A" * 255 + "\n" in calls.serial
    assert "A" * 256 not in calls.serial
    assert "ctl[a?b?c?d\te?f]\n" in calls.serial


def test_unknown_numbers(calls):
    m = rom_one(calls, "enosys")
    assert {k: v for k, v in m.items() if k.startswith("n")} == \
        {"n00": "-38", "n7f": "-38", "nffffffff": "-38", "n01": "-38", "n47": "-38",
         "n4a": "-38"}, m


def test_bad_out_pointers(calls):
    m = rom_one(calls, "outptr")
    assert {k: v for k, v in m.items() if k not in ("stage", "ev")} == \
        {"sys_kernel": "-14", "sys_periph1": "-14", "sys_periph2": "-14",
         "mem_kernel": "-14", "mem_null": "-14", "sys_slot_end": "-14"}, m
    s = rom_one(calls, "straddle")
    assert (s["rc"], s["intact"]) == ("-14", "1"), s


def test_registers_survive(calls):
    assert rom_one(calls, "regs")["changed"] == "0x00000000", calls.serial


FAULTS = {
    "wrkern": ("data_abort", "DATA ABORT"),
    "rdperiph": ("data_abort", "DATA ABORT"),
    "jmpkern": ("prefetch_abort", "PREFETCH ABORT"),
    "undef": ("undef", "UNDEFINED INSTRUCTION"),
    "priv": ("undef", "UNDEFINED INSTRUCTION"),
    "null": ("data_abort", "DATA ABORT"),
}


# On the ARM1176 boards QEMU lets USR mode fetch from the kernel's
# privileged-only sections: the ROM runs kernel code until the first
# privileged instruction, an undef at 0x00008078. Data accesses do fault.
# Strict, so the test fails once the jump faults as it should.
ARM1176_BOARDS = ("raspi0", "raspi1ap")


@pytest.mark.parametrize("case", list(FAULTS))
def test_fault(board, kernel_image, test_rom, tmp_path, need_mtools, case, request):
    reason, text = FAULTS[case]
    if case == "jmpkern" and board in ARM1176_BOARDS:
        request.applymarker(pytest.mark.xfail(
            strict=True, reason="USR fetch from a privileged-only section does not fault "
                                "on ARM1176 in QEMU"))
    with booted(kernel_image, board, sd=card(tmp_path, test_rom(f"fault_{case}"))) as vm:
        f = rom_one(vm, "fault")
        kill = one(vm, "rom_kill")
        assert (kill["reason"], kill["pc"]) == (reason, f["pc"]), vm.serial
        if reason == "data_abort":
            assert kill["dfar"] == f["addr"], vm.serial
        if case == "jmpkern":
            assert kill["pc"] == "0x00008000"
        assert one(vm, "ready")["rom"] == "1"
        assert not pipos_markers(vm, "rom_exit")
        assert "the fault did not happen" not in vm.serial
        line = f"ROM(1) {text} AT 0x{int(f['pc'], 16):08X}"
        assert line in dump(vm), vm.serial
        assert state(vm)["ready"] == "1"
        no_exception(vm)


def test_scripts_work_while_rom_sleeps(board, kernel_image, test_rom, tmp_path, need_mtools):
    sd = card(tmp_path, test_rom("sleep"))
    with boot(kernel_image, board, sd=sd, serial2=True, timeout=TIMEOUT) as vm:
        vm.wait_for("ev=sleeping", timeout=TIMEOUT, line=True)
        assert state(vm)["ready"] == "0", vm.serial
        assert "LOAD ROM(1): SLEEP 1.0.0" in dump(vm)
        assert not pipos_markers(vm, "ready")


def test_scripts_stop_while_rom_spins(board, kernel_image, test_rom, tmp_path, need_mtools):
    """The documented limit: without a blocking call the kernel does no main-loop work."""
    sd = card(tmp_path, test_rom("spin"))
    with boot(kernel_image, board, sd=sd, serial2=True, timeout=TIMEOUT) as vm:
        vm.wait_for("ev=spinning", timeout=TIMEOUT, line=True)
        with pytest.raises(QemuError):
            vm.script("state", timeout=3)
        assert not pipos_markers(vm, "ready")
