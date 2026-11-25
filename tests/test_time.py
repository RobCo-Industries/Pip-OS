"""The time, sensor, battery and power calls on the target.

With no RTC the clock starts unset. With QEMU's ds1338 model on BSC1
(i2c-bus.1, every board) and a fixed -rtc base, the kernel seeds the
clock from the chip. power(2) and power(3) blank and restore the screen;
power(0) reboots and power(1) halts.
"""

import calendar
import time

from harness.boot import QemuError, boot

from pipos_helpers import (TIMEOUT, assert_rom_checks, booted, make_card, no_exception, one,
                           pipos_markers, rom_markers, rom_one, screenshot)

ROM = "PIPBOY/ROM1.ROM"
RTC_DEVICE = ["-device", "ds1338,bus=i2c-bus.1,address=0x68"]
RTC_BASE = "2077-10-23T09:47:05"
RTC_UNIX = calendar.timegm(time.strptime(RTC_BASE, "%Y-%m-%dT%H:%M:%S"))

CALL_CHECKS = [
    "hw_no_rtc", "unset_get", "unset_intact", "unset_avail0", "unset_read0", "unset_read0_intact",
    "unset_get_kernel", "unset_get_mmio", "unset_get_straddle",
    "set", "get", "get_value", "get2", "advanced", "avail0", "read0", "read0_agrees",
    "get_kernel", "get_mmio", "get_straddle",
    "read0_kernel", "read0_mmio", "read0_straddle",
    "sensors_1_6", "read6_kernel", "read6_mmio", "read6_straddle",
    "read7", "read7_badptr", "avail7", "read_max", "avail_max", "battery",
    "set_back", "get_back", "back_value", "set_max", "get_max", "max_clamped",
    "set_zero", "get_zero", "zero_value",
]
RTC_CHECKS = ["hw_rtc", "avail0", "get", "read0", "read0_agrees", "set", "get_set", "set_value"]
WRITE_CHECKS = ["set_t1", "get_t1", "t1_value", "set_old", "get_old", "old_value"]
BLANK_CHECKS = ["op4", "op_max", "off", "on"]


def card(tmp_path, data):
    return make_card(tmp_path / "sd.img", {ROM: data})


def wait_rom(vm, ev, count=1, timeout=TIMEOUT):
    deadline = time.monotonic() + timeout
    while len(rom_markers(vm, ev)) < count:
        assert time.monotonic() < deadline, f"wanted {count} testrom ev={ev}\n{vm.serial}"
        time.sleep(0.05)


def test_calls_no_rtc(board, kernel_image, test_rom, tmp_path, need_mtools):
    with booted(kernel_image, board, sd=card(tmp_path, test_rom("time_calls"))) as vm:
        assert one(vm, "clock")["rtc"] == "absent", vm.serial
        assert_rom_checks(vm, CALL_CHECKS)
        assert one(vm, "rom_exit")["status"] == "0"
        # set_time moved the clock and wrote no chip.
        assert not pipos_markers(vm, "rtc_set"), vm.serial
        reply = vm.script("rtc", timeout=TIMEOUT)
        assert reply.status == "ok" and reply.lines == ["rc=-11 time=none"], reply
        no_exception(vm)


def test_clock_from_rtc(board, kernel_image, test_rom, tmp_path, need_mtools):
    extra = RTC_DEVICE + ["-rtc", f"base={RTC_BASE}"]
    with booted(kernel_image, board, sd=card(tmp_path, test_rom("time_rtc")),
                extra_args=extra) as vm:
        clock = one(vm, "clock")
        assert clock["rtc"] == "present", vm.serial
        assert clock["time"].startswith(RTC_BASE[:13]), vm.serial
        assert_rom_checks(vm, RTC_CHECKS)
        got = int(rom_one(vm, "rtc")["time"]) & 0xFFFFFFFF
        # QEMU's clock runs from launch; the boot takes some seconds.
        assert RTC_UNIX <= got <= RTC_UNIX + TIMEOUT, (got, RTC_UNIX, vm.serial)
        # set_time(1234567890) wrote the chip too.
        assert [m["rc"] for m in pipos_markers(vm, "rtc_set")] == ["0"], vm.serial
        chip = read_chip(vm)
        assert 1234567890 <= chip <= 1234567890 + TIMEOUT, (chip, vm.serial)
        no_exception(vm)


def read_chip(vm):
    """The time the clock chip holds, through the "rtc" script command."""
    reply = vm.script("rtc", timeout=TIMEOUT)
    assert reply.status == "ok" and len(reply.lines) == 1, reply
    fields = dict(f.split("=", 1) for f in reply.lines[0].split())
    assert fields["rc"] == "0", reply
    return calendar.timegm(time.strptime(fields["time"], "%Y-%m-%dT%H:%M:%S"))


def test_set_time_writes_rtc(board, kernel_image, test_rom, tmp_path, need_mtools):
    """set_time writes the chip; a time before 2000 moves the clock only.

    QEMU resets its ds1338 model on a guest reset, so a reboot cannot show
    the written time. The test reads the chip back in the same boot.
    """
    extra = RTC_DEVICE + ["-rtc", f"base={RTC_BASE}"]
    sd = card(tmp_path, test_rom("time_rtc_write"))
    with boot(kernel_image, board, sd=sd, serial2=True, timeout=TIMEOUT,
              extra_args=extra) as vm:
        wait_rom(vm, "step1")
        t1 = read_chip(vm)
        assert len(pipos_markers(vm, "rtc_set")) == 1, vm.serial
        assert pipos_markers(vm, "rtc_set")[0]["rc"] == "0", vm.serial
        wait_rom(vm, "step2")
        after = read_chip(vm)
        assert len(pipos_markers(vm, "rtc_set")) == 1, vm.serial
        wait_rom(vm, "checks", timeout=2 * TIMEOUT)
        assert_rom_checks(vm, WRITE_CHECKS)
        no_exception(vm)
    # T1 = 1900000000; the chip counts on from the write.
    assert 1900000000 <= t1 <= 1900000000 + TIMEOUT, (t1, vm.serial)
    assert t1 <= after <= t1 + TIMEOUT, (t1, after, vm.serial)


def test_screen_off_on(board, kernel_image, test_rom, tmp_path, need_mtools):
    sd = card(tmp_path, test_rom("time_blank"))
    with boot(kernel_image, board, sd=sd, serial2=True, timeout=TIMEOUT) as vm:
        wait_rom(vm, "drawn")
        drawn = screenshot(vm, tmp_path)
        wait_rom(vm, "off")
        off = screenshot(vm, tmp_path)
        wait_rom(vm, "on")
        on = screenshot(vm, tmp_path)
        assert_rom_checks(vm, BLANK_CHECKS)
        no_exception(vm)
    assert drawn.getpixel((100, 100)) == (0xFF, 0x80, 0x00)
    assert off.getcolors() == [(640 * 480, (0, 0, 0))]
    # Back on: the last presented frame, with the square drawn while blanked.
    assert on.getpixel((100, 100)) == (0xFF, 0x80, 0x00)
    assert on.getpixel((440, 340)) == (0xFF, 0x80, 0x00)
    assert drawn.getpixel((440, 340)) == (0x20, 0x30, 0x40)
    assert on.getpixel((10, 10)) == drawn.getpixel((10, 10))


def test_power_reboot(board, kernel_image, test_rom, tmp_path, need_mtools):
    """The ROM reboots every time it runs: the test stops after the second boot."""
    sd = card(tmp_path, test_rom("time_reboot"))
    with boot(kernel_image, board, sd=sd, serial2=True, timeout=TIMEOUT) as vm:
        wait_rom(vm, "rebooting", count=2)
        reboots = pipos_markers(vm, "reboot")
        assert len(reboots) >= 1, vm.serial
        assert len(pipos_markers(vm, "boot")) >= 2, vm.serial
        assert len([m for m in pipos_markers(vm, "rom") if m.get("rc") == "ok"]) >= 2, vm.serial
        assert not rom_markers(vm, "returned"), vm.serial
        assert not pipos_markers(vm, "rom_exit"), vm.serial
        no_exception(vm)


def test_power_halt(board, kernel_image, test_rom, tmp_path, need_mtools):
    sd = card(tmp_path, test_rom("time_halt"))
    with boot(kernel_image, board, sd=sd, serial2=True, timeout=TIMEOUT) as vm:
        vm.wait_for("ev=halt", timeout=TIMEOUT, line=True)
        try:
            reply = vm.script("state", timeout=3)
        except QemuError:
            reply = None
        assert reply is None, reply
        time.sleep(1)
        assert len(pipos_markers(vm, "boot")) == 1, vm.serial
        assert len(pipos_markers(vm, "halt")) == 1, vm.serial
        assert not rom_markers(vm, "returned"), vm.serial
        assert not pipos_markers(vm, "rom_exit"), vm.serial
        assert not pipos_markers(vm, "ready"), vm.serial
        no_exception(vm)
