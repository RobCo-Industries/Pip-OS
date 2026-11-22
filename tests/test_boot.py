"""Boots without a card: boot text, markers, beep, golden screen, panic, RTC.

Set PIPOS_UPDATE_GOLDEN=1 to rewrite tests/golden/boot_nocard.png from the
current run instead of comparing against it.
"""

import os

from PIL import Image

from pipos_helpers import (CMD_PANIC, SCHEME_BG, TIMEOUT, booted, config_summary, dump,
                           no_exception, one, pipos_markers, screenshot, wait_markers)

GOLDEN = os.path.join(os.path.dirname(os.path.abspath(__file__)), "golden",
                      "boot_nocard.png")
RTC_DEVICE = ["-device", "ds1338,bus=i2c-bus.1,address=0x68"]

BOOT_TEXT = [
    "*************** PIP-OS(R) V7.1.0.8 ***************",
    "COPYRIGHT 2075 ROBCO IND.",
    "**************************************************",
    "",
    "LOADER V1.1",
    "EXEC VERSION 41.10",
    "64K RAM SYSTEM",
    "38911 BYTES FREE",
    "NO HOLOTAPE FOUND",
    "ROM(1) NOT LOADED: NO SD CARD",
    "",
    "**************************************************",
    "PIP-OS KERNEL INITIALIZED",
    "PIP-OS READY",
    "**************************************************",
]


def test_no_card(board, kernel_image):
    with booted(kernel_image, board) as vm:
        boot = one(vm, "boot")
        assert boot["soc"].startswith("bcm") and boot["midr"].startswith("0x"), vm.serial
        assert one(vm, "mmu")["on"] == "1"
        assert one(vm, "tick")["hz"] == "100"
        assert int(one(vm, "heap")["size"]) > 0
        assert one(vm, "card")["rc"] == "nocard"
        cfg = config_summary(vm)
        assert (cfg["rc"], cfg["scheme"], cfg["line_delay_ms"]) == ("nocard", "green", "0")
        rom = one(vm, "rom")
        assert (rom["rc"], rom["name"], rom["version"]) == ("io", "-", "-"), vm.serial
        assert one(vm, "holo")["count"] == "0"
        assert one(vm, "ready")["rom"] == "0"

        rows = dump(vm)
        assert rows[:len(BOOT_TEXT)] == BOOT_TEXT, rows
        assert not any(rows[len(BOOT_TEXT):]), rows
        screen = [m for m in pipos_markers(vm, "screen")][-1]
        assert screen["scheme"] == "green"
        # The same lines reach the UART.
        for line in BOOT_TEXT:
            assert line + "\n" in vm.serial
        no_exception(vm)


def test_input_backends_start(board, kernel_image):
    with booted(kernel_image, board) as vm:
        inp = one(vm, "input")
        # QEMU has no mini UART pins to mux, so every backend starts.
        assert (inp["gpio"], inp["serial"], inp["mini"], inp["script"]) == ("0", "0", "0", "0")


def test_power_on_beep(board, kernel_image):
    with booted(kernel_image, board) as vm:
        init = one(vm, "audio_init")
        assert (init["rc"], init["backend"]) == ("0", "trace"), vm.serial
        tone = wait_markers(vm, "audio", op="tone")[0]
        assert (tone["hz"], tone["vol"]) == ("1000", "100")
        wait_markers(vm, "audio", op="stop")
        no_exception(vm)


def test_golden_screen_no_card(board, kernel_image, tmp_path):
    with booted(kernel_image, board) as vm:
        dump(vm)
        img = screenshot(vm, tmp_path)
    assert img.getpixel((639, 479)) == SCHEME_BG["green"]
    if os.environ.get("PIPOS_UPDATE_GOLDEN"):
        os.makedirs(os.path.dirname(GOLDEN), exist_ok=True)
        img.save(GOLDEN)
    with Image.open(GOLDEN) as golden:
        assert golden.convert("RGB").tobytes() == img.tobytes(), f"screen differs from {GOLDEN}"


def test_panic_path(board, kernel_image):
    with booted(kernel_image, board) as vm:
        vm.send(CMD_PANIC)
        vm.wait_for("ev=panic", timeout=TIMEOUT, line=True)
        panic = one(vm, "panic")
        assert panic["msg"] == "panic_requested_on_the_console", vm.serial


def test_rtc_seeds_clock(board, kernel_image):
    with booted(kernel_image, board, extra_args=RTC_DEVICE) as vm:
        clock = one(vm, "clock")
        assert clock["rtc"] == "present", vm.serial
        assert clock["time"].startswith("20"), vm.serial


def test_no_rtc(board, kernel_image):
    with booted(kernel_image, board) as vm:
        clock = one(vm, "clock")
        assert (clock["rtc"], clock["time"]) == ("absent", "unset"), vm.serial
