"""macOS side files on the card: what a ROM sees must not change.

A card written from a Mac holds AppleDouble files named "._<name>" next
to real files, and folders such as .Spotlight-V100, .fseventsd and
.Trashes. mtools stores "._CULVERT.HTP" as a long name with the 8.3 alias
"_CULVE~1.HTP", as a Mac does. The test boots a clean card and the same
card with side files, hidden entries and system entries, and compares the
boot's holotape count and what tests/roms/side_files.c lists.
"""

import struct
import subprocess

from harness.env import minimal_env

from pipos_helpers import (_mtools, booted, make_card, no_exception, one, pipos_markers,
                           rom_markers, rom_one)

ROM = "PIPBOY/ROM1.ROM"
HOLO = "PIPBOY/HOLO/"
SAVE = "PIPBOY/SAVE/"
DATA = "PIPBOY/DATA/"
PART_OFFSET = 2048 * 512


def apple_double(size=4096):
    """An AppleDouble header as macOS writes it, padded like a Finder one."""
    head = struct.pack(">II16sH", 0x00051607, 0x00020000, b"Mac OS X        ", 0)
    return head.ljust(size, b"\0")


def clean_files(test_rom, test_holo):
    tape = test_holo("tape")
    return {
        ROM: test_rom("side_files"),
        HOLO + "CULVERT.HTP": tape,
        HOLO + "Long tape.htp": tape,
        SAVE + "GAME.SAV": b"save data",
        DATA + "NOTES.TXT": b"notes",
    }


# name: FAT attribute changes for mattrib, after mcopy.
SIDE = {
    "._CULVERT.HTP": None,
    ".DS_Store": None,
    "._Long tape.htp": "+h",
    ".Spotlight-V100/Store-V2/0.store": None,
    ".fseventsd/fseventsd-uuid": None,
    ".Trashes/501/": "+h",
    "HIDDEN.HTP": "+h",
    "SYSTEM.HTP": "+s",
}


def mac_card(path, test_rom, test_holo):
    tape = test_holo("tape")
    files = clean_files(test_rom, test_holo)
    attrs = []
    for folder in ("", "PIPBOY/", HOLO, SAVE, DATA):
        for name, attr in SIDE.items():
            data = tape if name.endswith(".HTP") and not name.startswith(".") else \
                apple_double()
            files[folder + name] = b"" if name.endswith("/") else data
            if attr:
                attrs.append((attr, folder + name.rstrip("/").split("/")[0]))
    sd = make_card(path, files)
    target = f"{sd}@@{PART_OFFSET}"
    for attr, name in attrs:
        _mtools("mattrib", "-i", target, attr, "::/" + name)
    return sd


def seen(kernel_image, board, sd):
    with booted(kernel_image, board, sd=sd) as vm:
        assert one(vm, "rom_exit")["status"] == "0", vm.serial
        no_exception(vm)
        holo = [m["count"] for m in pipos_markers(vm, "holo")]
        tapes = [(m["file"], m["size"]) for m in rom_markers(vm, "tape")]
        ents = [(m["dir"], m["name"], m["flags"], m["size"]) for m in rom_markers(vm, "ent")]
        dirs = [(m["dir"], m["n"]) for m in rom_markers(vm, "dir")]
        save = rom_one(vm, "save")
        return holo, tapes, ents, dirs, (save["rc"], save["size"])


def test_mac_card_has_side_files(tmp_path, test_rom, test_holo, need_mtools):
    """The card really holds what a Mac writes: long names, aliases, attributes."""
    sd = mac_card(tmp_path / "sd.img", test_rom, test_holo)
    out = _mtools_out("mdir", "-a", "-i", f"{sd}@@{PART_OFFSET}", "::/PIPBOY/HOLO")
    assert "_CULVE~1 HTP" in out and "._CULVERT.HTP" in out, out
    assert "SPOTLI~1" in out and ".Spotlight-V100" in out, out
    attrs = _mtools_out("mattrib", "-i", f"{sd}@@{PART_OFFSET}", "::/PIPBOY/HOLO/*")
    lines = {line.split("/")[-1]: line for line in attrs.splitlines() if "/" in line}
    assert "H" in lines["HIDDEN.HTP"][:10], attrs
    assert "S" in lines["SYSTEM.HTP"][:10], attrs
    assert "H" in lines["._Long tape.htp"][:10], attrs
    assert "H" not in lines["._CULVERT.HTP"][:10], attrs


def _mtools_out(*args):
    env = minimal_env(MTOOLS_SKIP_CHECK="1")
    return subprocess.run(list(args), check=True, env=env, capture_output=True,
                          text=True).stdout


def test_side_files_change_nothing(board, kernel_image, test_rom, test_holo, tmp_path,
                                   need_mtools):
    (tmp_path / "clean").mkdir()
    (tmp_path / "mac").mkdir()
    clean = make_card(tmp_path / "clean" / "sd.img", clean_files(test_rom, test_holo))
    mac = mac_card(tmp_path / "mac" / "sd.img", test_rom, test_holo)
    want = seen(kernel_image, board, clean)
    holo, tapes, ents, dirs, save = want
    assert holo == ["2"], want
    assert [t[0] for t in tapes] == ["CULVERT.HTP", "LONGTA~1.HTP"], want
    assert save == ("0", "9"), want
    got = seen(kernel_image, board, mac)
    assert got == want, "\n".join(map(str, got))
