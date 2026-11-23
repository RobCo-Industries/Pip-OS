"""Holotapes (system calls 0x60-0x63): enumerate, launch, the sandbox and
the state handover between the ROM and a holotape.

The test holotape is tests/roms/holo_tapes/tape.c; its argument string
picks what it does. The launcher ROMs are tests/roms/holo_*.c.
"""

import os
import struct

from harness.boot import boot

from pipos_helpers import (TIMEOUT, _mtools, assert_rom_checks, booted, corrupt,
                           make_card, make_rom, no_exception, one, pipos_markers, reseal,
                           rom_markers, rom_one, screenshot)
from test_rom import ARM1176_BOARDS, first_privileged_pc

ROM = "PIPBOY/ROM1.ROM"
HOLO = "PIPBOY/HOLO/"
PART_OFFSET = 2048 * 512


def state(vm):
    reply = vm.script("state", timeout=TIMEOUT)
    assert reply.status == "ok" and len(reply.lines) == 1, reply
    return dict(field.split("=") for field in reply.lines[0].split())


def set_file_alias(sd, old, new):
    """Gives the one file entry whose 8.3 name is `old`, and that has a long
    name, the 8.3 name `new`, and fixes the long name's checksum."""
    def field(name):
        base, _, ext = name.partition(".")
        return base.ljust(8).encode() + ext.ljust(3).encode()

    def checksum(sfn):
        s = 0
        for b in sfn:
            s = (((s & 1) << 7) + (s >> 1) + b) & 0xFF
        return s

    img = bytearray(open(sd, "rb").read())
    hits = [at for at in range(PART_OFFSET, len(img), 32)
            if img[at:at + 11] == field(old) and img[at + 11] & 0x18 == 0
            and img[at - 32 + 11] == 0x0F]
    assert len(hits) == 1, hits
    at = hits[0]
    img[at:at + 11] = field(new)
    lfn = at - 32
    while img[lfn + 11] == 0x0F:
        img[lfn + 13] = checksum(field(new))
        lfn -= 32
    open(sd, "wb").write(img)


def with_subtype(tape, subtype):
    data = bytearray(tape)
    struct.pack_into("<I", data, 0x48, subtype)
    return reseal(data)


def with_field(tape, offset, value):
    data = bytearray(tape)
    struct.pack_into("<I", data, offset, value)
    return reseal(data)


def with_meta(tape, title, icon):
    """The tape with a META title and icon, resealed."""
    data = bytearray(tape)
    data[0x60:0x120] = title.encode().ljust(64, b"\0") + icon
    return reseal(data)


# ---- holo_launch and the sandbox -----------------------------------------

LAUNCH_CHECKS = [
    "last_none", "eject", "exit0", "last_exit0", "exit1", "last_exit1", "exit255", "exit256",
    "exit_neg", "ret7", "ret_lower_name", "args_empty", "args_99", "args_100", "last_args_100",
    "args_kernel", "args_null", "name_kernel", "name_null", "args_before_check",
    "missing", "last_missing", "bad_crc", "last_bad_crc", "truncated", "rom_magic", "data_tape",
    "folder", "long_alias", "alias_name", "alias_long_name", "bad_names",
    "exit0_again", "info", "open_file",
    "rd_rom", "wr_rom", "last_kill", "wr_rom_intact", "rd_heap", "wr_heap", "wr_heap_intact",
    "rd_kernel", "wr_kernel", "rd_backbuf", "rd_mmio", "undef", "jmp_kernel", "after_kill",
    "rom_intact", "last_after",
]

INFO_CHECKS = [
    "su_kind", "su_size", "su_abi", "su_heap", "su_stack", "su_flags", "su_at", "sys_info",
    "mem_info", "heap_works", "launch_eperm", "launch_bad_eperm", "power_eperm",
    "power_bad_eperm", "set_time_eperm", "last_status", "eject", "enumerate", "in_rom_slot",
    "out_rom_slot", "enum_rom_slot", "in_rom_heap", "out_rom_heap", "out_holo_heap",
]

# The kills in the order of the sandbox part of holo_launch.c.
KILLS = ["wr_rom", "rd_heap", "wr_heap", "rd_kernel", "wr_kernel", "rd_backbuf", "rd_mmio",
         "undef", "jmp_kernel"]


def launch_card(tmp_path, test_rom, test_holo):
    tape = test_holo("tape")
    sd = make_card(tmp_path / "sd.img", {
        ROM: test_rom("holo_launch"),
        HOLO + "TAPE.HTP": tape,
        HOLO + "BADCRC.HTP": corrupt(tape, -1),
        HOLO + "TRUNC.HTP": tape[:-16],
        HOLO + "ROMFILE.HTP": make_rom(),
        HOLO + "DATA.HTP": with_subtype(tape, 2),
        HOLO + "DIR.HTP/": b"",
        HOLO + "Long tape.htp": tape,
        HOLO + "Game.htp": tape,
        "CARD.TXT": b"ABCDEFGH",
    })
    set_file_alias(sd, "GAME.HTP", "OTHER.HTP")
    return sd


def tchecks(vm):
    return [(m["name"], m["ok"]) for m in rom_markers(vm, "tcheck")]


def test_launch_and_sandbox(board, kernel_image, test_rom, test_holo, tmp_path, need_mtools):
    sd = launch_card(tmp_path, test_rom, test_holo)
    with booted(kernel_image, board, sd=sd) as vm:
        assert_rom_checks(vm, LAUNCH_CHECKS)
        assert one(vm, "rom_exit")["status"] == "0", vm.serial
        assert tchecks(vm) == [(n, "1") for n in INFO_CHECKS], vm.serial
        assert "rom slot string read by the holotape\n" in vm.serial
        assert one(vm, "syscalls")
        st = state(vm)
        no_exception(vm)
    assert st["holo"] == "0" and st["files"] == "0", st
    starts = pipos_markers(vm, "holo_start")
    exits = pipos_markers(vm, "holo_exit")
    kills = pipos_markers(vm, "holo_kill")
    errors = pipos_markers(vm, "holo_error")
    # Every launch that got past the file checks started once and ended once.
    assert len(starts) == len(exits) + len(kills), vm.serial
    assert {s["name"] for s in starts} == {"TAPE.HTP", "LONGTA~1.HTP", "OTHER.HTP"}, starts
    assert [e["code"] for e in errors] == ["crc", "size", "magic", "bounds"], errors
    assert [e["status"] for e in exits][:8] == ["0", "1", "255", "0", "255", "7", "9", "200"]
    # The sandbox kills: reason, pc and fault address.
    faults = rom_markers(vm, "tfault")
    assert len(kills) == len(KILLS), kills
    k = dict(zip(KILLS, kills))
    for case in ("wr_rom", "rd_heap", "wr_heap", "rd_kernel", "wr_kernel", "rd_backbuf",
                 "rd_mmio"):
        assert k[case]["reason"] == "data_abort", (case, k[case])
        assert 0x00800000 <= int(k[case]["pc"], 16) < 0x00C00000, (case, k[case])
    assert k["wr_kernel"]["dfar"] == "0x00008000", k["wr_kernel"]
    assert k["rd_backbuf"]["dfar"] == "0x00c00000", k["rd_backbuf"]
    assert (k["undef"]["reason"], k["undef"]["pc"]) == ("undef", faults[0]["pc"]), vm.serial
    jmp = k["jmp_kernel"]
    if board in ARM1176_BOARDS:
        # QEMU 10.1 does not check execute permission on ARMv6: see test_rom.py.
        assert jmp["reason"] != "prefetch_abort", \
            f"QEMU now refuses the USR fetch on {board}: move it to the " \
            f"prefetch_abort expectation\n{vm.serial}"
        pc = first_privileged_pc(os.path.join(os.path.dirname(kernel_image), "pipos.elf"))
        assert (jmp["reason"], jmp["pc"]) == ("undef", pc), vm.serial
    else:
        assert (jmp["reason"], jmp["pc"]) == ("prefetch_abort", "0x00008000"), vm.serial
    # A holotape fault never reaches the screen and never ends the ROM.
    assert not pipos_markers(vm, "rom_kill"), vm.serial


# ---- holo_enumerate --------------------------------------------------------

ENUM_CHECKS = ["max0", "max33", "out_kernel", "out_null", "out_straddle", "straddle_intact",
               "max1", "max1_first", "max1_untouched", "max32"]


def listing(vm):
    return [(m["file"], m["sub"], int(m["size"]), m["ver"], m["name"], m["title"], m["icon"])
            for m in rom_markers(vm, "tape")]


def run_enum(kernel_image, board, sd):
    with booted(kernel_image, board, sd=sd) as vm:
        assert_rom_checks(vm, ENUM_CHECKS)
        assert one(vm, "rom_exit")["status"] == "0", vm.serial
        no_exception(vm)
        return vm, int(rom_one(vm, "count")["n"]), listing(vm)


def test_enumerate_no_folder(board, kernel_image, test_rom, tmp_path, need_mtools):
    sd = make_card(tmp_path / "sd.img", {ROM: test_rom("holo_enum")})
    _, n, tapes = run_enum(kernel_image, board, sd)
    assert (n, tapes) == (0, [])


def test_enumerate_empty_folder(board, kernel_image, test_rom, tmp_path, need_mtools):
    sd = make_card(tmp_path / "sd.img", {ROM: test_rom("holo_enum"), HOLO: b""})
    _, n, tapes = run_enum(kernel_image, board, sd)
    assert (n, tapes) == (0, [])


def test_enumerate_tapes(board, kernel_image, test_rom, test_holo, tmp_path, need_mtools):
    tape = test_holo("tape")
    icon = bytes((i * 13 + 1) & 0xFF for i in range(128))
    meta = with_meta(tape, "Fancy_Title", icon)
    sd = make_card(tmp_path / "sd.img", {
        ROM: test_rom("holo_enum"),
        HOLO + "A_PLAIN.HTP": tape,
        HOLO + "B_META.HTP": meta,
        HOLO + "C_BADCRC.HTP": corrupt(tape, -1),
        HOLO + "D_ROM.HTP": make_rom(),
        HOLO + "E_VER.HTP": reseal(bytearray(tape[:0x28]) + b"\x02" + tape[0x29:]),
        HOLO + "F_DATA.HTP": with_subtype(tape, 2),
        HOLO + "G_TRUNC.HTP": tape[:-4],
        HOLO + "H_SHORT.HTP": tape[:0x100],
        HOLO + "I_DIR.HTP/": b"",
        HOLO + "J_TEXT.TXT": tape,
        HOLO + "K_UTIL.HTP": with_subtype(with_field(tape, 0x44, 0x00010203), 1),
        HOLO + "Long name.htp": tape,
    })
    _, n, tapes = run_enum(kernel_image, board, sd)
    size = len(tape)
    plain = ("0", size, "0x00010000", "tape", "Holotape_tape", "0x00000000")
    assert tapes == [
        ("A_PLAIN.HTP",) + plain,
        ("B_META.HTP", "0", size, "0x00010000", "tape", "Fancy_Title",
         f"0x{sum(icon):08x}"),
        ("C_BADCRC.HTP",) + plain,
        ("F_DATA.HTP", "2") + plain[1:],
        ("K_UTIL.HTP", "1", size, "0x00010203") + plain[3:],
        ("LONGNA~1.HTP",) + plain,
    ], tapes
    assert n == 6


def test_enumerate_many(board, kernel_image, test_rom, test_holo, tmp_path, need_mtools):
    tape = test_holo("tape")
    files = {ROM: test_rom("holo_enum")}
    for i in range(40):
        files[HOLO + f"T{i:02d}.HTP"] = tape
    sd = make_card(tmp_path / "sd.img", files)
    _, n, tapes = run_enum(kernel_image, board, sd)
    assert n == 32
    assert [t[0] for t in tapes] == [f"T{i:02d}.HTP" for i in range(32)], tapes


# ---- the state handover ----------------------------------------------------

STATE_CHECKS = ["open", "read_before", "tone", "busy", "keys", "launch_killed", "last_killed",
                "no_events", "no_sound", "read_after", "event_after", "event_key", "close"]
TAPE_STATE_CHECKS = ["st_kind", "st_no_events", "st_no_sound", "st_rom_read", "st_rom_close",
                     "st_open", "st_read", "st_tone"]
RED = (0xFF, 0, 0)


def pixel(img, x, y):
    """The colour of logical pixel (x, y) on the 640x480 screen."""
    return img.getpixel((2 * x + 1, 2 * y + 1))


def test_state_handover(board, kernel_image, test_rom, test_holo, tmp_path, need_mtools):
    sd = make_card(tmp_path / "sd.img", {
        ROM: test_rom("holo_state"),
        HOLO + "TAPE.HTP": test_holo("tape"),
        "CARD.TXT": b"ABCDEFGH",
    })
    with boot(kernel_image, board, sd=sd, serial2=True, timeout=TIMEOUT) as vm:
        vm.wait_for("ev=want_keys", timeout=TIMEOUT, line=True)
        for line in ("key left 1", "key left 0", "dial 3"):
            assert vm.script(line, timeout=TIMEOUT).status == "ok"
        vm.wait_for("ev=tape_drawn", timeout=TIMEOUT, line=True)
        during = state(vm)
        tape_img = screenshot(vm, tmp_path)
        assert vm.script("kill", timeout=TIMEOUT).status == "ok"
        vm.wait_for("ev=want_more", timeout=TIMEOUT, line=True)
        assert vm.script("key right 1", timeout=TIMEOUT).status == "ok"
        vm.wait_for("ev=parked", timeout=TIMEOUT, line=True)
        after = state(vm)
        rom_img = screenshot(vm, tmp_path)
        no_exception(vm)
        assert not pipos_markers(vm, "ready")
    assert_rom_checks(vm, STATE_CHECKS)
    assert tchecks(vm) == [(n, "1") for n in TAPE_STATE_CHECKS], vm.serial
    assert (during["holo"], during["files"]) == ("1", "2"), during
    assert (after["holo"], after["files"]) == ("0", "1"), after
    kill = one(vm, "holo_kill")
    assert (kill["reason"], kill["code"]) == ("killed", "1"), kill
    # The holotape drew with the default colour and no clip.
    fill = pixel(tape_img, 0, 0)
    assert fill != RED and fill != (0, 0, 0), fill
    assert pixel(tape_img, 120, 120) == fill and pixel(tape_img, 319, 239) == fill
    # The ROM's clip (100,100)-(149,149) and colour came back.
    assert pixel(rom_img, 120, 120) == RED, pixel(rom_img, 120, 120)
    assert pixel(rom_img, 99, 99) == fill and pixel(rom_img, 150, 150) == fill


# ---- card errors -----------------------------------------------------------

def test_card_read_error(board, kernel_image, test_rom, test_holo, tmp_path, need_mtools):
    """A card that fails a read gives -PIP_EIO from enumerate and launch.

    FAR.HTP is copied after a 17 MiB pad, so its clusters lie past 16 MiB.
    The test cuts the 32 MiB image to 16 MiB and shrinks the partition to
    match, so RobCo-Core still mounts it. The FAT volume keeps its old size,
    so a read of FAR.HTP's data goes past the end of the device and the
    block layer fails it, as it fails any card read. NEAR.HTP stays readable.
    """
    tape = test_holo("tape")
    sd = make_card(tmp_path / "sd.img", {
        ROM: test_rom("holo_eio"),
        HOLO + "NEAR.HTP": tape,
        "ZPAD.BIN": bytes(17 << 20),
    }, size_mib=32)
    far = tmp_path / "far.htp"
    far.write_bytes(tape)
    _mtools("mcopy", "-i", f"{sd}@@{PART_OFFSET}", str(far), "::" + HOLO + "FAR.HTP")
    with open(sd, "r+b") as f:
        f.truncate(16 << 20)
        f.seek(0x1BE + 12)
        f.write(struct.pack("<I", (16 << 20) // 512 - PART_OFFSET // 512))
    with booted(kernel_image, board, sd=sd) as vm:
        assert_rom_checks(vm, ["near", "enum", "far", "last_far", "near_again"])
        assert one(vm, "rom_exit")["status"] == "0", vm.serial
        no_exception(vm)
    assert [e["code"] for e in pipos_markers(vm, "holo_error")] == ["io"], vm.serial
