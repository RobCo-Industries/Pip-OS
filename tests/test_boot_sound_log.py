"""Saves the serial log of one boot with the sound on, for `make boot-sound`.

It runs only with --boot-sound-log PATH. The card uses 20 ms per
character, 200 ms per line, volume 80, the beep and the system sound on, so
the log holds the rest ramp and the soft beep. tools/boot_sound.py turns the
log into a WAV file.
"""

import pytest

from pipos_helpers import booted, config_summary, make_card, no_exception

CFG = (b"[display]\nchar_delay_ms = 20\nline_delay_ms = 200\n"
       b"[audio]\nvolume = 80\nbeep = yes\nsystem_sound = yes\n")


def test_save_boot_log(request, board, kernel_image, tmp_path, need_mtools):
    path = request.config.getoption("--boot-sound-log")
    sd = make_card(tmp_path / "sd.img", {"PIPBOY/PIPOS.CFG": CFG})
    with booted(kernel_image, board, sd=sd) as vm:
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
