"""The sound levels of [audio] in PIPOS.CFG, through the trace backend.

The gain of a play is (master * level * level * trim + 5000) / 10000. A
start line prints vol= as the gain in whole percent, rounded down, and
adds gain=<0..10000> at its end when the gain is not a whole percent.
The kernel's own sounds and a ROM's are sfx, a holotape's are games.
"""

from pipos_helpers import (assert_rom_checks, booted, config, config_summary, make_card,
                           no_exception, one, pipos_markers, wait_markers)

ROM = "PIPBOY/ROM1.ROM"
CFG = "PIPBOY/PIPOS.CFG"
TAPE = "PIPBOY/HOLO/TAPE.HTP"
STARTS = ("tone", "pcm", "voice")
# The rest ramp plays at full scale whatever the levels.
RAMP_LEN = "882"


def gain(master, level, trim=100):
    return (master * level * level * trim + 5000) // 10000


def shown(g):
    """The vol= and gain= fields of a start line at gain g."""
    return (str(g // 100), None if g % 100 == 0 else str(g))


def starts(vm):
    return [m for m in pipos_markers(vm, "audio") if m["op"] in STARTS]


def test_sfx_level_scales_the_boot_beep(board, kernel_image, tmp_path, need_mtools):
    sd = make_card(tmp_path / "sd.img", {CFG: config(volume=70, extra="sfx = 50\n")})
    with booted(kernel_image, board, sd=sd) as vm:
        wait_markers(vm, "audio", count=2)
        cfg = config_summary(vm)
        beep = pipos_markers(vm, "audio")[0]
        no_exception(vm)
    assert (cfg["volume"], cfg["radio"], cfg["sfx"], cfg["recordings"], cfg["games"]) == \
        ("70", "100", "50", "100", "100"), vm.serial
    assert beep["op"] == "tone", beep
    assert (beep["vol"], beep.get("gain")) == shown(70 * 25) == ("17", "1750"), beep


def boot_lines(kernel_image, board, path, extra):
    """The audio lines of a boot with the system sound on, without their times."""
    text = (b"[display]\nchar_delay_ms = 2\nline_delay_ms = 0\n"
            b"[audio]\nvolume = 70\nbeep = yes\nsystem_sound = yes\n" + extra)
    with booted(kernel_image, board, sd=make_card(path, {CFG: text})) as vm:
        wait_markers(vm, "ready")
        lines = [m for m in pipos_markers(vm, "audio")]
        cfg = config_summary(vm)
        no_exception(vm)
    return lines, cfg


def test_full_levels_change_no_line(board, kernel_image, tmp_path, need_mtools):
    """No level key, or every level at 100: each line is the line of volume 70 alone."""
    for name, extra in (("none", b""),
                        ("full", b"radio = 100\nsfx = 100\nrecordings = 100\ngames = 100\n")):
        lines, cfg = boot_lines(kernel_image, board, tmp_path / f"{name}.img", extra)
        assert [cfg[k] for k in ("radio", "sfx", "recordings", "games")] == ["100"] * 4, cfg
        assert lines, name
        for m in lines:
            assert m["op"] != "gain" and "gain" not in m, (name, m)
            if m["op"] in STARTS + ("pcm_mix",) and m.get("len") != RAMP_LEN:
                assert m["vol"] == "70", (name, m)


AUDIO_RATE_CHECKS = [
    "start_8000", "bad_0", "bad_44100", "bad_37501", "bad_keeps",
    "slow", "slow_wait", "to_hi", "fast", "fast_wait",
    "to_lo", "q_first", "q_to_hi", "q_playing", "q_joins", "q_wait", "q_new", "q_new_wait",
    "tape", "back_hi", "after_tape", "after_tape_wait",
]


def test_holotape_takes_the_games_level(board, kernel_image, test_rom, test_holo, tmp_path,
                                        need_mtools):
    """audio_rate.c plays four samples, one with a queued buffer, launches the
    tape that plays one sample, then plays one more. Only the tape's is games."""
    sd = make_card(tmp_path / "sd.img", {
        ROM: test_rom("audio_rate"),
        CFG: config(volume=80, beep="no", extra="sfx = 50\ngames = 30\nradio = 0\n"),
        TAPE: test_holo("tape"),
    })
    with booted(kernel_image, board, sd=sd) as vm:
        assert_rom_checks(vm, AUDIO_RATE_CHECKS)
        assert one(vm, "rom_exit")["status"] == "0"
        wait_markers(vm, "audio", count=13)
        lines = pipos_markers(vm, "audio")
        no_exception(vm)
    sfx, games = shown(gain(80, 50)), shown(gain(80, 30))
    assert sfx == ("20", None) and games == ("7", "720"), (sfx, games)
    got = [(m["op"], m["vol"], m.get("gain")) for m in lines if m["op"] in STARTS]
    want = [("pcm",) + sfx] * 4 + [("pcm",) + games, ("pcm",) + sfx]
    assert got == want, lines
    # The queued buffer joined its play: it starts nothing and moves no gain.
    assert [m["op"] for m in lines].count("pcm_next") == 1, lines
    assert not [m for m in lines if m["op"] == "gain"], lines
