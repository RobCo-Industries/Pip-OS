"""The syscall table as a whole: every number of abi.h that the kernel
does not serve returns -PIP_ENOSYS.

The list of numbers comes from sdk/include/pipos/abi.h, and the served
ones from the kernel's ev=syscalls marker. A group that starts to serve
its calls drops out of the expectation by itself.
"""

from pipos_helpers import (abi_syscalls, assert_rom_checks, booted, make_card,
                           no_exception, one, rom_markers, rom_one, served_syscalls,
                           with_served_map)


def test_served_numbers_are_in_the_abi(board, kernel_image):
    with booted(kernel_image, board) as vm:
        served = served_syscalls(vm)
        no_exception(vm)
    table = abi_syscalls()
    assert served <= set(table), served - set(table)
    # The system group, served since 0.7.1.
    assert {0x42, 0x43, 0x44, 0x45, 0x46, 0x48, 0x49} <= served, served


def test_unserved_numbers_return_enosys(board, kernel_image, test_rom, tmp_path, need_mtools):
    with booted(kernel_image, board) as vm:
        served = served_syscalls(vm)
    table = abi_syscalls()
    rom = with_served_map(test_rom("enosys"), served)
    sd = make_card(tmp_path / "sd.img", {"PIPBOY/ROM1.ROM": rom})
    with booted(kernel_image, board, sd=sd) as vm:
        assert served_syscalls(vm) == served
        assert rom_one(vm, "table")["count"] == str(len(table)), vm.serial
        assert {int(m["nr"], 16) for m in rom_markers(vm, "served")} == served & set(table)
        unserved = [table[n] for n in sorted(table) if n not in served]
        assert_rom_checks(vm, unserved)
        assert one(vm, "rom_exit")["status"] == "0"
        no_exception(vm)
