"""Saves the serial log of one boot with the sound on, for `make boot-sound`.

It runs only with --boot-sound-log PATH. The card uses 20 ms per
character, 200 ms per line, volume 80, the beep and the system sound on, and
carries the sleep test ROM, which makes no sound and waits. So the log holds
the rest ramp, the soft beep, the disk clicks, the note, the quiet second and
the scroll's blips. The test waits until the sounds stop, so the log holds
them all. tools/boot_sound.py
turns the log into a WAV file.
"""

import time

import pytest

from pipos_helpers import TIMEOUT, booted, config_summary, make_card, no_exception

CFG = (b"[display]\nchar_delay_ms = 20\nline_delay_ms = 200\n"
       b"[audio]\nvolume = 80\nbeep = yes\nsystem_sound = yes\n")


def wait_quiet(vm, quiet_s=2.0, limit_s=60.0):
    """Waits until no audio record has come for quiet_s."""
    end = time.monotonic() + limit_s
    seen, last = vm.serial.count("ev=audio "), time.monotonic()
    while time.monotonic() < end:
        time.sleep(0.25)
        now = vm.serial.count("ev=audio ")
        if now != seen:
            seen, last = now, time.monotonic()
        elif time.monotonic() - last >= quiet_s:
            return
    pytest.fail("the sounds did not stop\n" + vm.serial)


def test_save_boot_log(request, board, kernel_image, test_rom, tmp_path, need_mtools):
    path = request.config.getoption("--boot-sound-log")
    sd = make_card(tmp_path / "sd.img", {"PIPBOY/PIPOS.CFG": CFG,
                                         "PIPBOY/ROM1.ROM": test_rom("sleep")})
    with booted(kernel_image, board, sd=sd, wait="jump") as vm:
        vm.wait_for("ev=sleeping", timeout=TIMEOUT, line=True)
        wait_quiet(vm)
        cfg = config_summary(vm)
        no_exception(vm)
        log = vm.serial
    with open(path, "w", encoding="utf-8") as f:
        f.write(log)
    assert (cfg["char_delay_ms"], cfg["key_click"], cfg["volume"]) == ("20", "1", "80"), log
    if "ev=audio " not in log:
        pytest.fail("the boot printed no audio marker\n" + log)
    if " ev=audio op=pcm rate=22050 len=882 " not in log:
        pytest.fail("the boot played no rest ramp\n" + log)
    if " ev=audio op=pcm rate=37500 len=2250 " not in log:
        pytest.fail("the boot played no soft beep\n" + log)
    if " ev=disk at=" not in log or " ev=audio op=pcm rate=37500 len=789 " not in log:
        pytest.fail("the boot played no disk click\n" + log)
    if " ev=scroll " not in log or " ev=audio op=pcm rate=37500 len=600 " not in log:
        pytest.fail("the boot did not scroll with blips\n" + log)
