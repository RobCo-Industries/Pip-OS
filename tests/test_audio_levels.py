"""The sound calls of ABI 1.6 on the target, through the trace backend.

sound_kind sets the kind of the plays an image starts, sound_level reads
and sets the master and the four levels, and sound_store writes them to
/PIPBOY/PIPOS.SET. A holotape's volume sets its own trim. The gain of a
play is (master * level * level * trim + 5000) / 10000. A start line
shows vol= as the gain in whole percent and gain= when it is not one.
"""

from harness.boot import boot
from pipos_helpers import (READY, TIMEOUT, _mtools, assert_rom_checks, booted, config, make_card,
                           no_exception, one, pipos_markers, wait_markers)

from test_click_rom import after_jump, is_click
from test_levels import gain, shown
from test_storage import PART_OFFSET, card_read

ROM = "PIPBOY/ROM1.ROM"
CFG = "PIPBOY/PIPOS.CFG"
SET = "PIPBOY/PIPOS.SET"
TAPE = "PIPBOY/HOLO/TAPE.HTP"
STARTS = ("tone", "pcm", "voice")
# Every card here: master 80, radio 40, sfx 100, recordings 50, games 30.
LEVELS = "radio = 40\nsfx = 100\nrecordings = 50\ngames = 30\n"
RADIO, SFX, REC, GAMES = gain(80, 40), gain(80, 100), gain(80, 50), gain(80, 30)

CHECKS = [
    "kind_start_sfx", "seq_radio", "kind_back", "seq_wait", "pcm_sfx", "pcm_wait",
    "kind_bad_4", "kind_bad_max", "kind_bad_keeps",
    "q_first", "q_to_radio", "q_joins", "q_wait",
    "lv_tone", "lv_radio_20", "lv_games_20", "lv_stop", "lv_idle_50", "lv_next", "lv_wait",
    "lv_radio_back", "lv_games_back", "lv_which_bad", "lv_level_bad", "lv_bad_keeps",
    "master_ask", "master_ask_keeps", "master_60", "master_volume",
    "over_kind_sfx", "over_sfx", "over_kind_radio", "over_radio", "over_wait",
    "tape_levels", "tape_kind_back", "tape_master", "tape_trim_again",
]

TAPE_CHECKS = [
    "lv_kind_games", "lv_sample", "lv_trim_100", "lv_trim_50", "lv_trim_bad", "lv_stop",
    "lv_tone_trim", "lv_recordings", "lv_tone_rec", "lv_radio_perm", "lv_sfx_perm",
    "lv_kind_bad", "lv_kind_kept", "lv_set_perm", "lv_master_perm", "lv_which_bad",
    "lv_store_perm", "lv_read_master", "lv_read_radio", "lv_read_sfx", "lv_read_recordings",
    "lv_read_games",
]


def card(path, rom, *, tape=None, extra=""):
    files = {ROM: rom, CFG: config(volume=80, beep="no", extra=LEVELS + extra)}
    if tape is not None:
        files[TAPE] = tape
    return make_card(path, files)


def start(m):
    """A start line as (op, hz or len, vol, gain or None)."""
    what = m["hz"] if m["op"] == "tone" else m.get("len")
    return (m["op"], what, m["vol"], m.get("gain"))


def tone(hz, g):
    return ("tone", str(hz), *shown(g))


def between(lines, first, second):
    """The lines after the start `first` and before the start `second`."""
    keys = [start(m) if m["op"] in STARTS else None for m in lines]
    i = keys.index(first)
    j = keys.index(second, i + 1)
    return lines[i + 1:j]


def tape_checks(vm):
    return [(m["name"], m["ok"]) for m in vm.markers()
            if m.get("stage") == "testrom" and m.get("ev") == "tcheck"]


def test_kinds_levels_and_the_holotape(board, kernel_image, test_rom, test_holo, tmp_path,
                                       need_mtools):
    sd = card(tmp_path / "sd.img", test_rom("audio_levels"), tape=test_holo("tape"))
    with booted(kernel_image, board, sd=sd) as vm:
        assert_rom_checks(vm, CHECKS)
        assert one(vm, "rom_exit")["status"] == "0"
        wait_markers(vm, "audio", count=20)
        lines = pipos_markers(vm, "audio")
        no_exception(vm)
    assert tape_checks(vm) == [(n, "1") for n in TAPE_CHECKS], vm.serial
    assert (RADIO, SFX, REC, GAMES) == (1280, 8000, 2000, 720)
    trim50 = gain(80, 30, 50)
    rec50 = gain(80, 50, 50)
    want = [
        tone(300, RADIO), tone(301, RADIO),        # play_seq as radio
        ("pcm", "800", *shown(SFX)),               # a sample as sfx
        ("pcm", "1600", *shown(SFX)),              # the queue's play, sfx
        tone(500, RADIO), tone(501, gain(80, 50)),  # a level set, then the next start
        tone(600, SFX), tone(601, RADIO),          # a start over another gain
        ("pcm", "8000", *shown(GAMES)),            # the holotape, games at trim 100
        tone(700, trim50), tone(701, rec50),       # trim 50, then recordings
    ]
    assert [start(m) for m in lines if m["op"] in STARTS] == want, lines
    assert shown(RADIO) == ("12", "1280") and shown(SFX) == ("80", None), lines
    # Only two calls change the gain of a sound that plays.
    assert [m["gain"] for m in lines if m["op"] == "gain"] == \
        [str(gain(80, 20)), str(trim50)], lines
    # The queued buffer joined its play: it moved no gain.
    q = between(lines, want[3], want[4])
    assert [m["op"] for m in q].count("pcm_next") == 1, q
    # sound_level for radio moved the radio tone; the one for games did not.
    lv = between(lines, want[4], want[5])
    assert [m["op"] for m in lv] == ["gain", "stop"], lv
    # One stop and no gain line between a sound and a start of another gain.
    over = between(lines, want[6], want[7])
    assert [m["op"] for m in over] == ["stop"], over
    # The tape's volume(100) printed nothing, its volume(50) one gain line.
    tp = between(lines, want[8], want[9])
    assert [m["op"] for m in tp] == ["gain", "stop"], tp


def test_a_click_is_sfx_under_a_radio_rom(board, kernel_image, test_rom, tmp_path, need_mtools):
    sd = make_card(tmp_path / "sd.img", {
        ROM: test_rom("levels_click"),
        CFG: (f"[display]\nchar_delay_ms = 0\nline_delay_ms = 0\n"
              f"[audio]\nvolume = 80\nbeep = no\nsystem_sound = yes\n{LEVELS}").encode(),
    })
    with booted(kernel_image, board, sd=sd) as vm:
        assert_rom_checks(vm, ["click_radio"])
        assert one(vm, "rom_exit")["status"] == "0"
        no_exception(vm)
    clicks = [m for m in after_jump(vm) if is_click(m)]
    assert clicks, vm.serial
    for m in clicks:
        assert (m["vol"], m.get("gain")) == shown(SFX), m


STORED = b"[audio]\nvolume = 70\nradio = 45\nsfx = 90\nrecordings = 60\ngames = 55\n"


def test_store_writes_the_file_and_the_next_boot_reads_it(board, kernel_image, test_rom,
                                                         tmp_path, need_mtools):
    sd = card(tmp_path / "sd.img", test_rom("levels_store"))
    with booted(kernel_image, board, sd=sd) as vm:
        assert_rom_checks(vm, ["store_master", "store_radio", "store_sfx", "store_recordings",
                               "store_games", "store_ok", "store_again", "store_keeps"])
        assert one(vm, "rom_exit")["status"] == "0"
        no_exception(vm)
    assert card_read(sd, SET) == STORED
    # The same card, with the beep on and other values in PIPOS.CFG.
    cfg = tmp_path / "PIPOS.CFG"
    cfg.write_bytes(config(volume=30, extra="sfx = 20\n"))
    _mtools("mcopy", "-o", "-i", f"{sd}@@{PART_OFFSET}", str(cfg), "::" + CFG)
    with booted(kernel_image, board, sd=sd) as vm:
        wait_markers(vm, "audio", count=2)
        beep = pipos_markers(vm, "audio")[0]
        no_exception(vm)
    assert beep["op"] == "tone", beep
    assert (beep["vol"], beep.get("gain")) == shown(gain(70, 90)) == ("56", "5670"), beep


def test_store_with_the_card_gone(board, kernel_image, test_rom, tmp_path, need_mtools):
    sd = card(tmp_path / "sd.img", test_rom("levels_store_eio"))
    with boot(kernel_image, board, sd=sd, serial2=True, timeout=TIMEOUT) as vm:
        vm.wait_for(r"ev=waiting", timeout=TIMEOUT)
        assert "not inserted" in vm.qmp.hmp("eject -f sd0") + vm.qmp.hmp("info block")
        vm.wait_for(READY, timeout=TIMEOUT)
        assert_rom_checks(vm, ["eio_radio", "eio_before", "eio_store", "eio_again",
                               "eio_radio_kept", "eio_master_kept"])
        assert one(vm, "rom_exit")["status"] == "0", vm.serial
        no_exception(vm)
