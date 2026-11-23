"""Runs the test ROMs of tests/roms/ in user mode: exit paths, entry state,
system calls, faults, and the kernel's script commands while a ROM runs.

The ROMs print "@RC stage=testrom ..." lines through the log call.
"""

import os
import re
import struct
import subprocess

import pytest

from harness.boot import boot
from harness.qemu import QemuError

from pipos_helpers import (TIMEOUT, booted, dump, make_card, no_exception, one,
                           pipos_markers, assert_rom_checks, rom_markers,
                           reseal, rom_one)

ROM = "PIPBOY/ROM1.ROM"

STARTUP = 0x017F0000
STUB = 0x017F0080
STACK_TOP = 0x01800000
HEAP_BASE = 0x01000000
HEAP_SIZE = 0x007F0000
PIPOS_VERSION = (7 << 16) | (1 << 8) | 0


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


@pytest.mark.parametrize("rom, status", [("exit300", "44"), ("retneg", "255")])
def test_exit_status_low_byte(board, kernel_image, test_rom, tmp_path, need_mtools, rom, status):
    with booted(kernel_image, board, sd=card(tmp_path, test_rom(rom))) as vm:
        assert one(vm, "rom_exit")["status"] == status, vm.serial
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
        assert fields == {"abi_version": 0x00010001, "size": 128, "heap_base": HEAP_BASE,
                          "heap_size": HEAP_SIZE, "stack_top": STACK_TOP, "kind": 0,
                          "flags": 0}, vm.serial
        assert s["args_zero"] == "1"
        assert one(vm, "rom_exit")["status"] == "0"



def test_abi_1_0_image_runs(board, kernel_image, test_rom, tmp_path, need_mtools):
    """The loader checks only the major: an image sealed for ABI 1.0 runs
    and reads the version it runs on, 1.1, in its startup block."""
    rom = bytearray(test_rom("entry"))
    assert struct.unpack_from("<I", rom, 0x40)[0] == 0x00010001
    struct.pack_into("<I", rom, 0x40, 0x00010000)
    with booted(kernel_image, board, sd=card(tmp_path, reseal(rom))) as vm:
        assert int(rom_one(vm, "startup")["abi_version"], 16) == 0x00010001, vm.serial
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


TICK_MS = 10  # the kernel's 100 Hz tick


def test_sleep_is_never_short(calls):
    """ROMs only see ticks_ms. A read falls up to one tick after the real
    time, so a sleep of at least ms shows at least ms rounded down to a
    whole tick."""
    got = [(int(m["ms"]), m["rc"], int(m["elapsed"])) for m in rom_markers(calls, "sleep_len")]
    assert [g[0] for g in got] == [1, 7, 10, 13, 25, 50], calls.serial
    for ms, rc, elapsed in got:
        assert rc == "0", got
        assert ms - ms % TICK_MS <= elapsed < ms + 1000, got


def test_sys_info(calls):
    boot_m = one(calls, "boot")
    s = {k: int(v, 16) for k, v in rom_one(calls, "sys_info").items()
         if k not in ("stage", "ev", "rc")}
    assert rom_one(calls, "sys_info")["rc"] == "0"
    assert s == {"size": 32, "abi_version": 0x00010001, "pipos_version": PIPOS_VERSION,
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
        {"n00": "-38", "n7f": "-38", "nffffffff": "-38", "n80": "-38", "n14": "-38"}, m


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


# QEMU 10.1 does not check execute permission on ARMv6 CPUs. On these boards
# the ROM runs kernel code in USR mode until the first privileged instruction.
ARM1176_BOARDS = ("raspi0", "raspi1ap")
PRIVILEGED = ("mrc", "mcr", "cps", "srs", "rfe")


def first_privileged_pc(elf):
    """Follows the branch at 0x8000 and returns the address of the first
    instruction of the entry routine that is undefined in USR mode."""
    out = subprocess.run(["arm-none-eabi-objdump", "-d", elf], check=True,
                         capture_output=True, text=True).stdout
    insns = []
    for line in out.splitlines():
        m = re.match(r"\s*([0-9a-f]+):\s+[0-9a-f]{8}\s+(\S+)\s*(.*)", line)
        if m:
            insns.append((int(m[1], 16), m[2], m[3]))
    addrs = [a for a, _, _ in insns]
    first = insns[addrs.index(0x8000)]
    assert first[1] == "b", f"expected a branch at 0x8000, got {first}"
    i = addrs.index(int(first[2].split()[0], 16))
    for addr, op, args in insns[i:]:
        if op.startswith(PRIVILEGED):
            return f"0x{addr:08x}"
        assert not re.match(r"(b|bl|bx|blx)(eq|ne|cs|cc|mi|pl|vs|vc|hi|ls|ge|lt|gt|le|al)?$", op) \
            and not op.startswith(("ldr", "ldm", "pop")), \
            f"{op} {args} at 0x{addr:x} comes before a privileged instruction"
    raise AssertionError("no privileged instruction after the kernel entry")


@pytest.mark.parametrize("case", list(FAULTS))
def test_fault(board, kernel_image, test_rom, tmp_path, need_mtools, case):
    reason, text = FAULTS[case]
    with booted(kernel_image, board, sd=card(tmp_path, test_rom(f"fault_{case}"))) as vm:
        f = rom_one(vm, "fault")
        kill = one(vm, "rom_kill")
        if case == "jmpkern" and board in ARM1176_BOARDS:
            assert kill["reason"] != "prefetch_abort", \
                f"QEMU now refuses the USR fetch on {board}: move it to the " \
                f"prefetch_abort expectation\n{vm.serial}"
            pc = first_privileged_pc(os.path.join(os.path.dirname(kernel_image), "pipos.elf"))
            assert (kill["reason"], kill["pc"]) == ("undef", pc), vm.serial
            assert one(vm, "ready")["rom"] == "1"
            assert state(vm)["ready"] == "1"
            no_exception(vm)
            return
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


SDK_CHECKS = ["printf_conv", "printf_trunc", "printf_long", "printf_nul", "str_mem", "str_memmove",
              "str_len_cmp", "str_chr", "str_lcpy", "heap_align", "heap_calloc",
              "heap_exhaust", "heap_recover", "heap_free", "ini_get", "ini_get_int",
              "ini_get_bool", "ini_missing", "crc32_known", "crc32_stream"]


def test_sdk_self_check(board, kernel_image, test_rom, tmp_path, need_mtools):
    """tests/roms/sdkcheck, built only through sdk/pipos-app.mk and libpip.a."""
    with booted(kernel_image, board, sd=card(tmp_path, test_rom("sdkcheck"))) as vm:
        assert_rom_checks(vm, SDK_CHECKS)
        long_line = "".join(chr(ord("a") + i % 26) for i in range(600)) + "\n"
        assert long_line in vm.serial
        assert "sdkcheck: nul a?b\n" in vm.serial
        assert one(vm, "rom_exit")["status"] == "0"
        no_exception(vm)
