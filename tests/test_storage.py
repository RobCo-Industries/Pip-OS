"""Saves and files (system calls 0x50-0x57) on a real card image.

Each test boots a storage test ROM from tests/roms/ on its own card, then
reads the card back with mtools where the result lives on the card.
"""

import os
import subprocess

from harness.boot import boot

from pipos_helpers import (READY, TIMEOUT, assert_rom_checks, booted, make_card, no_exception, one,
                           rom_markers, rom_one)

ROM = "PIPBOY/ROM1.ROM"
SAVE_DIR = "PIPBOY/SAVE"
PART_OFFSET = 2048 * 512


def pattern(seed, n):
    """The bytes storage_save.c writes: fill(seed, n)."""
    return bytes((i * 31 + seed + (i >> 8)) & 0xFF for i in range(n))


def _mtools(*args):
    env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
    return subprocess.run(list(args), check=True, env=env, capture_output=True).stdout


def card_read(sd, path):
    """The bytes of `path` on the card, or None when it is missing."""
    try:
        return _mtools("mtype", "-i", f"{os.fspath(sd)}@@{PART_OFFSET}", "::" + path)
    except subprocess.CalledProcessError:
        return None


def card_names(sd, folder):
    """The names in `folder`, long names where they exist, upper-cased."""
    out = _mtools("mdir", "-b", "-i", f"{os.fspath(sd)}@@{PART_OFFSET}", "::" + folder)
    return sorted(line.rsplit("/", 1)[-1].upper()
                  for line in out.decode().splitlines() if line.strip())


def state(vm):
    reply = vm.script("state", timeout=TIMEOUT)
    assert reply.status == "ok" and len(reply.lines) == 1, reply
    return dict(field.split("=") for field in reply.lines[0].split())


def run_rom(kernel_image, board, sd, names):
    with booted(kernel_image, board, sd=sd) as vm:
        assert_rom_checks(vm, names)
        assert one(vm, "rom_exit")["status"] == "0", vm.serial
        no_exception(vm)
        return vm, state(vm)


def round_trip(tag):
    return [f"{tag}_{s}" for s in ("write", "info", "read", "same")]


SAVE_CHECKS = (
    ["missing_info", "missing_read", "missing_delete"] + round_trip("first") +
    ["small_read", "small_same", "zero_read"] + round_trip("shorter") + round_trip("longer") +
    ["delete", "gone_info", "gone_read", "gone_delete"] + round_trip("empty") +
    round_trip("long_name") + round_trip("edge_chars") +
    ["max_write", "max_info", "max_read", "max_same", "over_write", "huge_write", "over_kept"] +
    ["name_empty", "name_nine", "name_dot", "name_dash", "name_tilde", "name_hash",
     "name_slash", "name_space", "name_high", "name_nonul", "name_kernel", "name_null",
     "name_cross"] +
    ["ptr_write", "wbuf_kernel", "wbuf_periph", "wbuf_cross", "wbuf_none", "rbuf_kernel",
     "rbuf_periph", "rbuf_cross", "rbuf_intact", "rbuf_size", "rbuf_size_intact",
     "info_kernel", "info_cross", "info_intact"]
)


def test_saves(board, kernel_image, test_rom, tmp_path, need_mtools):
    sd = make_card(tmp_path / "sd.img", {ROM: test_rom("storage_save")})
    assert card_read(sd, SAVE_DIR + "/MAX.SAV") is None
    with booted(kernel_image, board, sd=sd) as vm:
        assert_rom_checks(vm, SAVE_CHECKS)
        assert one(vm, "rom_exit")["status"] == "0", vm.serial
        ms = int(rom_one(vm, "timing")["write_64k_ms"])
        print(f"\n{board}: write_save of 64 KiB took {ms} ms")
        assert 0 <= ms < 30000
        no_exception(vm)
    # The kernel made the folder; only finished saves are left, no work file.
    assert card_names(sd, SAVE_DIR) == ["ABCDEFGH.SAV", "EMPTY.SAV", "MAX.SAV", "PTR.SAV",
                                        "_09AZ.SAV"]
    assert card_read(sd, SAVE_DIR + "/MAX.SAV") == pattern(9, 65536)
    assert card_read(sd, SAVE_DIR + "/ABCDEFGH.SAV") == pattern(5, 777)
    assert card_read(sd, SAVE_DIR + "/EMPTY.SAV") == b""


def test_save_persists_across_boots(board, kernel_image, test_rom, tmp_path, need_mtools):
    sd = make_card(tmp_path / "sd.img", {ROM: test_rom("storage_persist"), SAVE_DIR + "/": b""})
    run_rom(kernel_image, board, sd, ["first_missing", "first_write"])
    assert card_read(sd, SAVE_DIR + "/PERSIST.SAV") == pattern(42, 3000)
    run_rom(kernel_image, board, sd, ["second_read", "second_same"])


OLD = b"OLD content of the save\n"
NEW = b"NEW content, longer than the old one\n"

# Save name -> files in /PIPBOY/SAVE, per row of the storage guide's recovery table.
RECOVERY = {
    "TILDE": {"~TILDE.SAV": NEW},
    "HASH": {"#HASH.SAV": OLD},
    "TGTTILDE": {"TGTTILDE.SAV": OLD, "~TGTTILDE.SAV": NEW},
    "TGTHASH": {"TGTHASH.SAV": NEW, "#TGTHASH.SAV": OLD},
    "BOTH": {"~BOTH.SAV": NEW, "#BOTH.SAV": OLD},
    "ALL": {"ALL.SAV": NEW, "~ALL.SAV": OLD, "#ALL.SAV": OLD},
    "INFOHASH": {"#INFOHASH.SAV": OLD},
    "DELBOTH": {"~DELBOTH.SAV": NEW, "#DELBOTH.SAV": OLD},
}


def test_save_recovery(board, kernel_image, test_rom, tmp_path, need_mtools):
    files = {ROM: test_rom("storage_recover")}
    for entries in RECOVERY.values():
        files.update({f"{SAVE_DIR}/{name}": data for name, data in entries.items()})
    sd = make_card(tmp_path / "sd.img", files)
    assert "~TILDE.SAV" in card_names(sd, SAVE_DIR)
    run_rom(kernel_image, board, sd, [
        "tilde_read", "tilde_info",
        "hash_read", "hash_same",
        "tgttilde_read", "tgttilde_same",
        "tgthash_read", "tgthash_same",
        "both_read", "both_same",
        "all_read", "all_same",
        "infohash_info",
        "delboth_delete", "delboth_gone",
    ])
    assert card_names(sd, SAVE_DIR) == ["ALL.SAV", "BOTH.SAV", "HASH.SAV", "INFOHASH.SAV",
                                        "TGTHASH.SAV", "TGTTILDE.SAV"]
    assert card_read(sd, SAVE_DIR + "/HASH.SAV") == OLD
    assert card_read(sd, SAVE_DIR + "/BOTH.SAV") == NEW
    assert card_read(sd, SAVE_DIR + "/TGTTILDE.SAV") == OLD


def file_pattern(n):
    """The bytes of /DATA/BIG.BIN, as storage_files.c computes them."""
    return bytes((i * 7 + (i >> 9) + 3) & 0xFF for i in range(n))


MANY = 70


def files_card(tmp_path, rom):
    files = {ROM: rom, "DATA/BIG.BIN": file_pattern(10000), "DATA/SMALL.TXT": b"hello pip-os\n",
             "DATA/SUB/": b"", SAVE_DIR + "/A.SAV": b"secret", "MIXED/X.TXT": b"x"}
    for i in range(MANY):
        files[f"MANY/F{i:02d}.TXT"] = b"m" * i
    files["MANY/ZDIR/"] = b""
    return make_card(tmp_path / "sd.img", files)


FILE_CHECKS = [
    "big_open", "big_c1", "big_c2", "big_c3", "big_c4", "big_end", "big_end_again",
    "big_same", "big_zero", "big_close",
    "whole_open", "whole_read", "whole_same", "whole_close",
    "small_open", "small_read", "small_same", "small_close",
    "missing", "missing_dir", "open_dir", "open_root",
    "ptr_open", "rd_kernel", "rd_periph", "rd_cross", "rd_intact", "rd_pos", "ptr_close",
    "path_kernel", "path_null", "path_cross", "path_nonul", "path_127", "path_128",
    "handles_8", "handle_9th", "close_3", "reuse_3", "close_all", "close_again",
    "read_closed", "bad_m1", "bad_8", "bad_big",
    "ls_max0", "ls_max65", "ls_file", "ls_missing", "ls_kernel", "ls_cross", "ls_intact",
    "ls_root", "ls_data", "ls_many_p0", "ls_many_p1", "ls_many_end", "ls_many_far",
    "ls_many_small",
    "hostile", "left_open",
]


def dirents(vm, tag):
    return [(m["name"], int(m["flags"]), int(m["size"]))
            for m in rom_markers(vm, "dirent") if m["tag"] == tag]


def test_files(board, kernel_image, test_rom, tmp_path, need_mtools):
    sd = files_card(tmp_path, test_rom("storage_files"))
    with booted(kernel_image, board, sd=sd) as vm:
        assert_rom_checks(vm, FILE_CHECKS)
        assert one(vm, "rom_exit")["status"] == "0", vm.serial
        no_exception(vm)
        bad = [m for m in rom_markers(vm, "hostile") if m["got"] != m["expected"]]
        assert not bad, bad
        assert len(rom_markers(vm, "hostile")) > 50
        # The ROM left three files open; the kernel closed them at exit.
        assert state(vm)["files"] == "0"
        assert dirents(vm, "root") == [("DATA", 1, 0), ("MANY", 1, 0), ("MIXED", 1, 0),
                                       ("PIPBOY", 1, 0)]
        assert dirents(vm, "data") == [("BIG.BIN", 0, 10000), ("SMALL.TXT", 0, 13),
                                       ("SUB", 1, 0)]
        many = [(f"F{i:02d}.TXT", 0, i) for i in range(MANY)] + [("ZDIR", 1, 0)]
        assert dirents(vm, "many0") == many[:64]
        assert dirents(vm, "many1") == many[64:]
        assert dirents(vm, "small") == many[10:13]


def test_card_gone(board, kernel_image, test_rom, tmp_path, need_mtools):
    """A ROM cannot load without a card, so the test ejects it while the ROM runs."""
    sd = make_card(tmp_path / "sd.img", {ROM: test_rom("storage_eio"),
                                         "PIPBOY/PIPOS.CFG": b"[display]\n"})
    # booted() would wait for ev=ready, which comes after the ROM ends.
    with boot(kernel_image, board, sd=sd, serial2=True, timeout=TIMEOUT) as vm:
        vm.wait_for(r"ev=waiting", timeout=TIMEOUT)
        assert "not inserted" in vm.qmp.hmp("eject -f sd0") + vm.qmp.hmp("info block")
        vm.wait_for(READY, timeout=TIMEOUT)
        assert_rom_checks(vm, ["before", "open_before", "probe", "write", "read", "info",
                               "delete", "open", "read_open", "close", "list"])
        assert one(vm, "rom_exit")["status"] == "0", vm.serial
        assert state(vm)["files"] == "0"
        no_exception(vm)
