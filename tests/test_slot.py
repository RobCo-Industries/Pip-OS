"""The holotape slot of ABI 1.4 in QEMU, with the test backend.

QEMU has no SD card on SPI0. The test build (-DPIPOS_HOLO_TEST) replaces
the real slot backend with kernel/slot_test.c: tape card images go into
guest RAM through QEMU's -device loader, and the holo script command
inserts, pulls and breaks them. The ROM is tests/roms/holo_slot.c and the
tape is tests/roms/holo_tapes/tape.c.

What this cannot check: the real driver, its pins, SPI0 timing and a real
card. Nothing here ran on hardware.
"""

import os
import re
import time

import pytest

from hosttiming import TICK_MS, CannotJudge, gap_too_long, judged

from pipos_helpers import (TIMEOUT, booted, config, corrupt, make_card, no_exception, one,
                           pipos_markers, rom_markers)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROM = "PIPBOY/ROM1.ROM"
CFG = "PIPBOY/PIPOS.CFG"
HOLO = "PIPBOY/HOLO/"
# kernel/slot_test.c
CARD_BASE = 0x02000000
CARD_STRIDE = 0x00800000
CARD_MIB = 8
CARD_BLOCKS = CARD_MIB * 2048
# kernel/holo.h
HOLO_BASE = 0x00800000
HOLO_SIZE = 0x00400000

INS, REM, BAD = 1, 2, 4
# RobCo-Core's include/robco/holo.h: the debounce before an attach.
SETTLE_MS = 250
ENODEV, ENOEXEC, EFAULT, EIO, ENOENT = -41, -40, -14, -5, -2
HW_TAPE = 8


@pytest.fixture(scope="session")
def holo_build(request, robco_build, kernel_image):
    """The test build of the kernel, next to --build-dir."""
    build = os.path.join(ROOT, request.config.getoption("--build-dir"))
    return robco_build(ROOT, target="kernel",
                       flags=(f"BUILD={build}/holotest", "EXTRA_CFLAGS=-DPIPOS_HOLO_TEST"),
                       image=os.path.join(build, "holotest", "pipos.img"))


def slot_cfg(slot="on"):
    return config(beep="no", extra=f"[holotape]\nslot = {slot}\n")


def boot_card(tmp_path, test_rom, tapes=None, cfg=None):
    files = {ROM: test_rom("holo_slot"), CFG: cfg if cfg is not None else slot_cfg()}
    for name, data in (tapes or {}).items():
        files[HOLO + name] = data
    return make_card(tmp_path / "sd.img", files)


def tape_card(tmp_path, n, files):
    """Card image n for the slot: files in its root folder, or None for no FAT volume."""
    path = tmp_path / f"tape{n}.img"
    if files is None:
        with open(path, "wb") as f:
            f.truncate(CARD_MIB << 20)
        return path
    return make_card(path, files, size_mib=CARD_MIB)


def loader(cards):
    args = []
    for n, path in cards.items():
        args += ["-device", f"loader,file={path},addr={CARD_BASE + n * CARD_STRIDE:#x},force-raw=on"]
    return args


def run(image, board, sd, cards=None):
    return booted(image, board, sd=sd, wait="jump", extra_args=loader(cards or {}))


def holo(vm, *words):
    reply = vm.script("holo " + " ".join(words), timeout=TIMEOUT)
    assert reply.status == "ok", reply
    return reply


def holo_at(vm, *words):
    """holo insert or pull: the tick_ms() when it took effect."""
    reply = holo(vm, *words)
    assert len(reply.lines) == 1 and reply.lines[0].startswith("t_ms="), reply
    return int(reply.lines[0][len("t_ms="):])


def key(vm, name):
    """A button press, or "dial" for one detent."""
    command = "dial 1" if name == "dial" else f"key {name}"
    assert vm.script(command, timeout=TIMEOUT).status == "ok"


def wait_rom(vm, ev, count=1, **match):
    t = time.monotonic()
    while True:
        found = [m for m in rom_markers(vm, ev)
                 if all(m.get(k) == str(v) for k, v in match.items())]
        if len(found) >= count:
            return found
        assert time.monotonic() - t < TIMEOUT, f"no testrom ev={ev} {match}\n{vm.serial}"
        time.sleep(0.02)


def tape_events(vm):
    return [int(m["value"]) for m in rom_markers(vm, "tapeev")]


def mask(vm):
    n = len(rom_markers(vm, "mask"))
    key(vm, "up")
    return int(wait_rom(vm, "mask", n + 1)[-1]["bits"])


def listing(vm):
    n = len(rom_markers(vm, "list"))
    k = len(rom_markers(vm, "entry"))
    key(vm, "select")
    count = int(wait_rom(vm, "list", n + 1)[-1]["n"])
    entries = rom_markers(vm, "entry")[k:]
    assert len(entries) == count, vm.serial
    return [(e["file"], int(e["flags"])) for e in entries]


def launch(vm, key_name):
    n = len(rom_markers(vm, "launch"))
    key(vm, key_name)
    return int(wait_rom(vm, "launch", n + 1)[-1]["rc"])


def insert(vm, n=0, expect=INS):
    """Inserts card n and waits for its PIP_EV_TAPE."""
    count = len(tape_events(vm))
    holo(vm, "insert", str(n), str(CARD_BLOCKS))
    wait_rom(vm, "tapeev", count + 1)
    assert tape_events(vm)[count] == expect, vm.serial


def pull(vm, expect=REM):
    count = len(tape_events(vm))
    holo(vm, "pull")
    wait_rom(vm, "tapeev", count + 1)
    assert tape_events(vm)[count] == expect, vm.serial


def hw(vm):
    m = wait_rom(vm, "hw")[0]
    return int(m["flags"]), int(m["abi"], 16)


# ---- The shipped image and the setting --------------------------------------


def test_shipped_image_has_no_test_backend(board, kernel_image, holo_build, tmp_path,
                                           test_rom, need_mtools):
    with open(kernel_image, "rb") as f:
        shipped = f.read()
    with open(holo_build, "rb") as f:
        test = f.read()
    for word in (b"bad holo command", b"ev=holo_attach"):
        assert word not in shipped and word in test, word
    with booted(kernel_image, board, sd=boot_card(tmp_path, test_rom), wait="jump") as vm:
        reply = vm.script("holo pull", timeout=TIMEOUT)
        assert reply.status == "err unknown command", reply
        no_exception(vm)


def test_shipped_slot_on_starts_real_driver(board, kernel_image, tmp_path, test_rom,
                                            need_mtools):
    with booted(kernel_image, board, sd=boot_card(tmp_path, test_rom), wait="jump") as vm:
        m = one(vm, "holo_slot")
        assert (m["backend"], m["rc"]) == ("holo", "0"), m
        flags, abi = hw(vm)
        assert (flags & HW_TAPE, abi) == (HW_TAPE, 0x00010004), vm.serial
        no_exception(vm)


@pytest.mark.parametrize("cfg", ["off", None])
def test_slot_off(board, kernel_image, holo_build, test_rom, test_holo, tmp_path, need_mtools,
                  cfg):
    """With off, the default, no backend starts and every call is ABI 1.3."""
    tape = test_holo("tape")
    sd = boot_card(tmp_path, test_rom, {"BOOT.HTP": tape},
                   cfg=slot_cfg(cfg) if cfg else config(beep="no"))
    card = tape_card(tmp_path, 0, {"SLOT.HTP": tape})
    for image in (kernel_image, holo_build):
        with run(image, board, sd, {0: card}) as vm:
            flags, abi = hw(vm)
            assert (flags & HW_TAPE, abi) == (0, 0x00010004)
            assert not pipos_markers(vm, "holo_slot"), vm.serial
            if image == holo_build:
                holo(vm, "insert", "0", str(CARD_BLOCKS))
            time.sleep(0.6)
            assert mask(vm) == 0
            assert listing(vm) == [("BOOT.HTP", 0)]
            assert launch(vm, "dial") == -2  # SLOT.HTP: -PIP_ENOENT
            assert tape_events(vm) == []
            assert vm.script("state", timeout=TIMEOUT).lines[0].endswith(" slot=off")
            no_exception(vm)


def test_slot_clash_with_panel_cs(board, holo_build, test_rom, test_holo, tmp_path,
                                  need_mtools):
    """A panel on CE1 clashes with the slot: the slot stays off and the boot goes on."""
    cfg = config(beep="no", extra="[holotape]\nslot = on\n[display]\nbackend = both\n"
                                  "tft_cs = 1\n")
    sd = boot_card(tmp_path, test_rom, {"BOOT.HTP": test_holo("tape")}, cfg=cfg)
    card = tape_card(tmp_path, 0, {"SLOT.HTP": test_holo("tape")})
    with run(holo_build, board, sd, {0: card}) as vm:
        flags, _ = hw(vm)
        assert flags & HW_TAPE == 0, vm.serial
        clash = [m for m in pipos_markers(vm, "config") if m.get("rc") == "slot_clash"]
        assert [(m["key"], m["line"]) for m in clash] == [("display.tft_cs", "0")], vm.serial
        assert not pipos_markers(vm, "holo_slot"), vm.serial
        holo(vm, "insert", "0", str(CARD_BLOCKS))
        time.sleep(0.6)
        assert mask(vm) == 0
        assert listing(vm) == [("BOOT.HTP", 0)]
        assert vm.script("state", timeout=TIMEOUT).lines[0].endswith(" slot=off")
        no_exception(vm)


# ---- Insertion, listing, launch -------------------------------------------------


def test_boot_with_no_card(board, holo_build, test_rom, test_holo, tmp_path, need_mtools):
    sd = boot_card(tmp_path, test_rom, {"BOOT.HTP": test_holo("tape")})
    with run(holo_build, board, sd) as vm:
        assert hw(vm)[0] & HW_TAPE == HW_TAPE
        m = one(vm, "holo_slot")
        assert (m["backend"], m["rc"]) == ("ram", "0"), m
        time.sleep(0.6)
        assert mask(vm) == 0
        assert listing(vm) == [("BOOT.HTP", 0)]
        assert tape_events(vm) == []
        no_exception(vm)


def test_boot_with_card_in(board, holo_build, test_rom, test_holo, tmp_path, need_mtools):
    """A card in at boot reports INSERTED once, after boot."""
    sd = boot_card(tmp_path, test_rom)
    card = tape_card(tmp_path, 0, {"SLOT.HTP": test_holo("tape")})
    with run(holo_build, board, sd, {0: card}) as vm:
        insert(vm)
        assert mask(vm) == INS
        assert mask(vm) == 0
        assert tape_events(vm) == [INS]
        no_exception(vm)


def test_insert_list_launch_exit(board, holo_build, test_rom, test_holo, tmp_path, need_mtools):
    tape = test_holo("tape")
    sd = boot_card(tmp_path, test_rom, {"BOOT.HTP": tape})
    card = tape_card(tmp_path, 0, {"SLOT.HTP": tape, ".HIDDEN.HTP": tape, "README.TXT": b"x"})
    with run(holo_build, board, sd, {0: card}) as vm:
        insert(vm)
        assert mask(vm) == INS
        assert listing(vm) == [("SLOT.HTP", 1), ("BOOT.HTP", 0)]
        assert launch(vm, "dial") == 200
        assert one(vm, "holo_start")["name"] == "SLOT.HTP"
        assert mask(vm) == 0
        no_exception(vm)


def test_shadowing(board, holo_build, test_rom, test_holo, tmp_path, need_mtools):
    """A slot file shadows a boot card tape by name, also a damaged one."""
    tape = test_holo("tape")
    util = test_holo("tape_util")
    sd = boot_card(tmp_path, test_rom, {"SLOT.HTP": util, "OTHER.HTP": tape})
    good = tape_card(tmp_path, 0, {"slot.htp": tape})
    bad = tape_card(tmp_path, 1, {"SLOT.HTP": corrupt(tape, 0x20)})
    with run(holo_build, board, sd, {0: good, 1: bad}) as vm:
        assert listing(vm) == [("OTHER.HTP", 0), ("SLOT.HTP", 0)]
        insert(vm, 0)
        assert listing(vm) == [("SLOT.HTP", 1), ("OTHER.HTP", 0)], vm.serial
        pull(vm)
        assert listing(vm) == [("OTHER.HTP", 0), ("SLOT.HTP", 0)]
        insert(vm, 1)
        assert listing(vm) == [("OTHER.HTP", 0)]
        assert launch(vm, "dial") == ENOEXEC
        pull(vm)
        assert listing(vm) == [("OTHER.HTP", 0), ("SLOT.HTP", 0)]
        no_exception(vm)


def test_slot_folder_named_like_tape(board, holo_build, test_rom, test_holo, tmp_path,
                                     need_mtools):
    """A folder SLOT.HTP on the slot card counts as missing: the boot card tape shows and runs."""
    tape = test_holo("tape")
    sd = boot_card(tmp_path, test_rom, {"SLOT.HTP": tape})
    card = tape_card(tmp_path, 0, {"SLOT.HTP/": b"", "MORE.HTP": tape})
    with run(holo_build, board, sd, {0: card}) as vm:
        insert(vm)
        assert mask(vm) == INS
        assert listing(vm) == [("MORE.HTP", 1), ("SLOT.HTP", 0)]
        assert launch(vm, "dial") == 200
        assert one(vm, "holo_start")["name"] == "SLOT.HTP"
        assert mask(vm) == 0
        no_exception(vm)


def test_32_entries_over_both(board, holo_build, test_rom, test_holo, tmp_path, need_mtools):
    tape = test_holo("tape")
    sd = boot_card(tmp_path, test_rom, {f"B{i:02d}.HTP": tape for i in range(20)})
    card = tape_card(tmp_path, 0, {f"S{i:02d}.HTP": tape for i in range(20)})
    with run(holo_build, board, sd, {0: card}) as vm:
        insert(vm)
        got = listing(vm)
        assert got == [(f"S{i:02d}.HTP", 1) for i in range(20)] + \
            [(f"B{i:02d}.HTP", 0) for i in range(12)], got
        no_exception(vm)


def slot_reads(vm):
    reply = vm.script("holo reads", timeout=TIMEOUT)
    assert reply.status == "ok" and len(reply.lines) == 1, reply
    return int(reply.lines[0].split("=")[1])


def test_full_slot_reads_no_more(board, holo_build, test_rom, test_holo, tmp_path,
                                 need_mtools):
    """With 32 slot tapes, the listing stops: no next entry, no shadow lookup,
    no boot card pass.

    The boot card cannot be made to fail, so the test counts slot reads. A
    full listing reads the slot card as much with boot card tapes as
    without, and a read error armed on the read after them changes nothing.
    """
    tape = test_holo("tape")
    card = tape_card(tmp_path, 0, {f"S{i:02d}.HTP": tape for i in range(32)})
    want = [(f"S{i:02d}.HTP", 1) for i in range(32)]
    need = []
    for tapes in ({}, {"BOOT.HTP": tape, "S00.HTP": tape, "S31.HTP": tape}):
        sd = boot_card(tmp_path, test_rom, tapes)
        with run(holo_build, board, sd, {0: card}) as vm:
            insert(vm)
            assert mask(vm) == INS
            assert listing(vm) == want
            r0 = slot_reads(vm)
            assert listing(vm) == want
            need.append(slot_reads(vm) - r0)
            holo(vm, "readfail", str(need[0]))
            assert listing(vm) == want, vm.serial
            assert mask(vm) == 0
            assert not pipos_markers(vm, "holo_slot_error"), vm.serial
            no_exception(vm)
    assert need[0] == need[1], need


def test_second_card(board, holo_build, test_rom, test_holo, tmp_path, need_mtools):
    tape = test_holo("tape")
    sd = boot_card(tmp_path, test_rom)
    one_card = tape_card(tmp_path, 0, {"FIRST.HTP": tape})
    two_card = tape_card(tmp_path, 2, {"SECOND.HTP": tape, "THIRD.HTP": tape})
    with run(holo_build, board, sd, {0: one_card, 2: two_card}) as vm:
        insert(vm, 0)
        assert listing(vm) == [("FIRST.HTP", 1)]
        pull(vm)
        insert(vm, 2)
        assert mask(vm) == INS | REM
        assert listing(vm) == [("SECOND.HTP", 1), ("THIRD.HTP", 1)]
        no_exception(vm)


# ---- Removal ----------------------------------------------------------------------


def test_pull_while_idle(board, holo_build, test_rom, test_holo, tmp_path, need_mtools):
    sd = boot_card(tmp_path, test_rom)
    card = tape_card(tmp_path, 0, {"SLOT.HTP": test_holo("tape")})
    with run(holo_build, board, sd, {0: card}) as vm:
        insert(vm)
        assert mask(vm) == INS
        pull(vm)
        assert mask(vm) == REM
        assert listing(vm) == []
        no_exception(vm)


# test_pull_while_slot_tape_runs: the pull lands this long after it is armed.
PULL_IN_MS = 1500


@pytest.mark.parametrize("how", ["right", "left"])
def test_pull_while_slot_tape_runs(board, holo_build, test_rom, test_holo, tmp_path,
                                   need_mtools, how):
    """right: the tape sleeps; left: it calls only ticks_ms. Both end at their next call."""
    sd = boot_card(tmp_path, test_rom)
    card = tape_card(tmp_path, 0, {"SLOT.HTP": test_holo("tape")})

    def attempt():
        with run(holo_build, board, sd, {0: card}) as vm:
            check(vm)

    def check(vm):
        insert(vm)
        n = len(rom_markers(vm, "launch"))
        # A tape that only calls ticks_ms keeps the script channel from
        # running, so the pull is armed before the launch.
        t_arm = holo_at(vm, "pullin", str(PULL_IN_MS))
        key(vm, how)
        done = wait_rom(vm, "launch", n + 1)[-1]
        rc, t_done = int(done["rc"]), int(done["t_ms"])
        # Checks that hold whatever the timing. The launch returned after
        # the pull was due: a launch that fails by itself returns at once.
        assert t_done + TICK_MS >= t_arm + PULL_IN_MS, \
            f"the launch returned rc={rc} at {t_done} ms, the pull was due at " \
            f"{t_arm + PULL_IN_MS} ms: {vm.serial}"
        assert mask(vm) == INS | REM
        assert vm.script("state", timeout=TIMEOUT).lines[0].split()[4] == "holo=0"
        no_exception(vm)
        # When the call entry sees the pull first, the ROM's next work pass
        # reports it; when sleep_ms does, the tape runs and gets no event.
        assert tape_events(vm)[0] == INS and REM not in tape_events(vm)[1:] or \
            tape_events(vm) == [INS, REM]
        started = pipos_markers(vm, "holo_start")
        kills = pipos_markers(vm, "holo_kill")
        if not started:
            # The pull came before the load: only a missing tape or a
            # failed read of it. Anything else fails.
            assert not kills and rc in (ENOENT, EIO), (rc, vm.serial)
            raise CannotJudge(f"the pull came before the tape loaded: rc={rc} at {t_done} ms, "
                              f"the pull due at {t_arm + PULL_IN_MS} ms, no holo_start")
        assert rc == ENODEV, vm.serial
        kill = one(vm, "holo_kill")
        assert kill["reason"] == "eject", kill
        # The fault format, and an address inside the tape: kernel/holo.h.
        assert re.fullmatch(r"0x[0-9a-f]{8}", kill["pc"]), kill
        assert HOLO_BASE <= int(kill["pc"], 16) < HOLO_BASE + HOLO_SIZE, kill
        if not rom_markers(vm, "tloop"):
            # The guest clock follows the host: a stall of 1.5 s let the
            # pull land between the start and the tape's first marker.
            raise CannotJudge("the pull came after holo_start, before the tape's ev=tloop")

    judged(attempt)


def test_pull_while_boot_tape_runs(board, holo_build, test_rom, test_holo, tmp_path,
                                   need_mtools):
    tape = test_holo("tape")
    sd = boot_card(tmp_path, test_rom, {"BOOT.HTP": tape})
    card = tape_card(tmp_path, 0, {"SLOT.HTP": tape})
    with run(holo_build, board, sd, {0: card}) as vm:
        insert(vm)
        n = len(rom_markers(vm, "launch"))
        key(vm, "back")
        wait_rom(vm, "tloop")
        holo(vm, "pull")
        time.sleep(1.0)
        assert len(rom_markers(vm, "launch")) == n, vm.serial
        assert not pipos_markers(vm, "holo_kill")
        assert vm.script("kill", timeout=TIMEOUT).status == "ok"
        assert int(wait_rom(vm, "launch", n + 1)[-1]["rc"]) == EFAULT
        assert mask(vm) == INS | REM
        no_exception(vm)


def test_pull_in_debounce(board, holo_build, test_rom, test_holo, tmp_path, need_mtools):
    sd = boot_card(tmp_path, test_rom)
    card = tape_card(tmp_path, 0, {"SLOT.HTP": test_holo("tape")})

    def attempt():
        with run(holo_build, board, sd, {0: card}) as vm:
            wait_rom(vm, "wait")
            t_in = holo_at(vm, "insert", "0", str(CARD_BLOCKS))
            t_out = holo_at(vm, "pull")
            time.sleep(0.6)
            got = mask(vm)
            no_exception(vm)
            if got == 0 and tape_events(vm) == []:
                return
            # Only a pull after the debounce may attach. The card was in
            # t_out - t_in ms by the tick clock, one tick either way.
            held = t_out - t_in
            assert pipos_markers(vm, "holo_attach") and held + TICK_MS >= SETTLE_MS, \
                (held, got, vm.serial)
            raise CannotJudge(f"the pull came {held} ms after the insert, past the "
                              f"{SETTLE_MS} ms debounce, and an attach ran")

    judged(attempt)


def test_pull_in_attach(board, holo_build, test_rom, test_holo, tmp_path, need_mtools):
    sd = boot_card(tmp_path, test_rom)
    card = tape_card(tmp_path, 0, {"SLOT.HTP": test_holo("tape")})
    with run(holo_build, board, sd, {0: card}) as vm:
        holo(vm, "attachpull")
        holo(vm, "insert", "0", str(CARD_BLOCKS))
        m = wait_markers_ev(vm, "holo_attach")
        assert m["rc"] == "-4", m  # BLK_ERR_NOMEDIA
        time.sleep(0.4)
        assert mask(vm) == 0 and tape_events(vm) == []
        assert vm.script("state", timeout=TIMEOUT).lines[0].endswith(" slot=empty")
        no_exception(vm)


def wait_markers_ev(vm, ev):
    t = time.monotonic()
    while not pipos_markers(vm, ev):
        assert time.monotonic() - t < TIMEOUT, vm.serial
        time.sleep(0.02)
    return pipos_markers(vm, ev)[0]


def test_attach_again_then_refused(board, holo_build, test_rom, test_holo, tmp_path,
                                   need_mtools):
    """SD_ERR_AGAIN keeps the card settling with no bit; another error gives BAD."""
    sd = boot_card(tmp_path, test_rom)
    card = tape_card(tmp_path, 0, {"SLOT.HTP": test_holo("tape")})
    with run(holo_build, board, sd, {0: card}) as vm:
        holo(vm, "again", "3")
        holo(vm, "refuse")
        insert(vm, 0, expect=BAD)
        rcs = [m["rc"] for m in pipos_markers(vm, "holo_attach")]
        assert rcs == ["-19", "-19", "-19", "-16"], rcs
        assert mask(vm) == BAD
        assert listing(vm) == []
        pull(vm)
        assert mask(vm) == REM
        no_exception(vm)


# ---- Bad cards and read errors ------------------------------------------------


def test_card_without_fat(board, holo_build, test_rom, tmp_path, need_mtools):
    sd = boot_card(tmp_path, test_rom)
    card = tape_card(tmp_path, 1, None)
    with run(holo_build, board, sd, {1: card}) as vm:
        insert(vm, 1, expect=BAD)
        assert one(vm, "holo_mount")["rc"] != "ok"
        assert mask(vm) == BAD
        assert listing(vm) == []
        pull(vm)
        assert mask(vm) == REM
        no_exception(vm)


def test_read_error_card_in(board, holo_build, test_rom, test_holo, tmp_path, need_mtools):
    """BAD, the list without slot tapes, and the shadowed boot card tape back."""
    tape = test_holo("tape")
    sd = boot_card(tmp_path, test_rom, {"SLOT.HTP": tape})
    card = tape_card(tmp_path, 0, {"SLOT.HTP": tape, "MORE.HTP": tape})
    with run(holo_build, board, sd, {0: card}) as vm:
        insert(vm)
        assert mask(vm) == INS
        holo(vm, "readfail")
        assert listing(vm) == [("SLOT.HTP", 0)]
        assert mask(vm) == BAD
        assert listing(vm) == [("SLOT.HTP", 0)]
        assert vm.script("state", timeout=TIMEOUT).lines[0].endswith(" slot=bad")
        pull(vm)
        assert mask(vm) == REM
        no_exception(vm)


def test_read_error_in_launch(board, holo_build, test_rom, test_holo, tmp_path, need_mtools):
    tape = test_holo("tape")
    sd = boot_card(tmp_path, test_rom, {"SLOT.HTP": tape})
    card = tape_card(tmp_path, 0, {"SLOT.HTP": tape})
    with run(holo_build, board, sd, {0: card}) as vm:
        insert(vm)
        assert mask(vm) == INS
        holo(vm, "readfail")
        assert launch(vm, "dial") == -5  # -PIP_EIO, no boot card try
        assert mask(vm) == BAD
        no_exception(vm)


def test_pull_during_listing(board, holo_build, test_rom, test_holo, tmp_path, need_mtools):
    """A pull met by a read gives REMOVED only, never BAD."""
    tape = test_holo("tape")
    sd = boot_card(tmp_path, test_rom, {"SLOT.HTP": tape})
    card = tape_card(tmp_path, 0, {"SLOT.HTP": tape})
    with run(holo_build, board, sd, {0: card}) as vm:
        insert(vm)
        assert mask(vm) == INS
        holo(vm, "readpull")
        assert listing(vm) == [("SLOT.HTP", 0)]
        assert mask(vm) == REM
        no_exception(vm)


# ---- The mask and the event ---------------------------------------------------


def test_eject_event_from_holotape(board, holo_build, test_rom, test_holo, tmp_path,
                                   need_mtools):
    """A holotape gets 0 and the bits wait for the ROM."""
    tape = test_holo("tape")
    sd = boot_card(tmp_path, test_rom)
    card = tape_card(tmp_path, 0, {"SLOT.HTP": tape})
    with run(holo_build, board, sd, {0: card}) as vm:
        insert(vm)
        assert launch(vm, "down") == 0
        assert mask(vm) == INS
        no_exception(vm)


def test_tape_event_wakes_wait_event(board, holo_build, test_rom, test_holo, tmp_path,
                                     need_mtools):
    """The ROM sits in wait_event forever; only PIP_EV_TAPE wakes it."""
    sd = boot_card(tmp_path, test_rom)
    card = tape_card(tmp_path, 0, {"SLOT.HTP": test_holo("tape")})
    with run(holo_build, board, sd, {0: card}) as vm:
        wait_rom(vm, "wait")
        waits = len(rom_markers(vm, "wait"))
        insert(vm)
        pull(vm)
        assert tape_events(vm) == [INS, REM]
        assert len(rom_markers(vm, "wait")) == waits + 2
        no_exception(vm)


# ---- A slow card --------------------------------------------------------------


def attach_span(vm):
    """The attach marker's numbers, and its start and end on the tick clock."""
    m = pipos_markers(vm, "holo_attach")[0]
    return m, int(m["t0_ms"]), int(m["t1_ms"])


def pcm_state(m):
    """(pcm0, pos0, pcm1, pos1) from the attach marker: the kernel's own PCM state,
    read with IRQs masked at the attach's start and end. pcm is audio_busy(),
    pos the samples the play has output."""
    return int(m["pcm0"]), int(m["pos0"]), int(m["pcm1"]), int(m["pos1"])


def audio_trace(vm):
    """(op, t_ms) of each audio trace line, on the audio clock."""
    return [(a["op"], int(a["t_ms"])) for a in pipos_markers(vm, "audio")]


def trace_after(vm, ev):
    """The audio trace with every record queued before the ROM's ev=<ev>.

    The ROM logs ev=wait after ev=<ev> and sits in wait_event. The trace
    prints from audio_poll() at the start of each pipos_poll() pass, and
    only a pass that begins after that ev=wait answers the holo command sent
    now. So that pass printed every record queued before ev=<ev>."""
    t = time.monotonic()
    while not re.search(rf"stage=testrom ev={ev}\b(?:(?!stage=testrom ev={ev}\b).)*"
                        r"stage=testrom ev=wait\b", vm.serial, re.S):
        assert time.monotonic() - t < TIMEOUT, f"no testrom ev=wait after ev={ev}\n{vm.serial}"
        time.sleep(0.02)
    holo(vm, "reads")
    return audio_trace(vm)


# The attach marker's t0_ms and t1_ms are tick_ms(). The audio trace's t_ms
# is the audio clock, seeded from tick_ms() and within one tick of it.
CLOCKS_MS = TICK_MS
SAMPLE_RATE = 8000
SAMPLE_MS = 7500                # the ROM's dial_push sample: 60000 bytes at 8 kHz


def pending(vm):
    """(attaching, t_ms) from the test backend: is an attach running now."""
    reply = holo(vm, "pending")
    m = re.fullmatch(r"attaching=([01]) t_ms=(\d+)", reply.lines[0])
    assert m, reply
    return m.group(1) == "1", int(m.group(2))


def start_ms(vm):
    """tick_ms() at the attach's start, from ev=holo_attach_start."""
    return int(wait_markers_ev(vm, "holo_attach_start")["t_ms"])


# A slow card: the test backend steps this long before its result.
SLOW_MS = 1200


def check_stepped(m):
    """The stepped attach's own witness, in every run: the loop ran during
    it, one step per pass. polls counts the passes after the start's pass,
    steps the step calls, each in its own pass."""
    assert m["rc"] == "0" and int(m["ms"]) >= SLOW_MS, m
    assert int(m["steps"]) >= 2, m
    assert int(m["polls"]) == int(m["steps"]), m


def test_slow_attach_keeps_main_loop(board, holo_build, test_rom, test_holo, tmp_path,
                                     need_mtools):
    """A slow card attaches in steps, one per pipos_poll() pass: the loop
    keeps running, script commands are served and a sample keeps playing.
    Recorded in docs/ARCHITECTURE.md.

    The sound part reads the kernel's PCM state from the attach marker.
    The trace gives only the sample's start, read after a barrier."""
    sd = boot_card(tmp_path, test_rom)
    card = tape_card(tmp_path, 0, {"SLOT.HTP": test_holo("tape")})

    def attempt():
        with run(holo_build, board, sd, {0: card}) as vm:
            holo(vm, "delay", str(SLOW_MS))
            wait_rom(vm, "wait")
            key(vm, "dial_push")
            assert wait_rom(vm, "sample")[0]["rc"] == "0"
            count = len(tape_events(vm))
            t_in = holo_at(vm, "insert", "0", str(CARD_BLOCKS))
            s0 = start_ms(vm)
            # A script command served while the attach runs: the loop runs.
            live, t_p = pending(vm)
            wait_rom(vm, "tapeev", count + 1)
            assert tape_events(vm) == [INS], vm.serial
            m, a0, a1 = attach_span(vm)
            assert a0 == s0, m
            polls = int(m["polls"])
            pcm0, pos0, pcm1, pos1 = pcm_state(m)
            # ev=tapeev comes after the attach.
            tr = trace_after(vm, "tapeev")
            print(f"\nattach: {m} insert={t_in} pending={live}@{t_p} audio={tr}")
            # The witness: the main loop's work ran during the attach.
            check_stepped(m)
            assert polls > 0, m
            no_exception(vm)
            assert tr and tr[0][0] == "pcm" and tr[0][1] <= t_in + CLOCKS_MS, (m, tr)
            if not live:
                # Only a reply that came after the attach's end on the
                # guest clock excuses it.
                assert t_p + CLOCKS_MS >= a1, (t_p, m)
                raise CannotJudge(f"the pending query ran at {t_p} ms, the attach ran "
                                  f"{a0} to {a1} ms")
            due = tr[0][1] + SAMPLE_MS
            if not (pcm0 and pcm1):
                edge = a1 if pcm0 else a0
                assert due <= edge + CLOCKS_MS, \
                    f"the sound stopped with {due - edge} ms of the sample left: {m} {tr}"
                raise CannotJudge(f"the insert came at {t_in} ms, the sample was due to end at "
                                  f"{due} ms, the attach ran {a0} to {a1} ms")
            # Playing at both ends, and the PCM interrupt output samples
            # in between, never more than 8 kHz makes due in ms.
            assert 0 < pos1 - pos0 <= int(m["ms"]) * SAMPLE_RATE // 1000 + 8, m

    judged(attempt)


# The kernel holds two sample_queue buffers: one plays, one waits.
STREAM_CHUNK_MS = 100
STREAM_AHEAD_MS = 2 * STREAM_CHUNK_MS


def test_slow_attach_keeps_a_stream_live(board, holo_build, test_rom, test_holo, tmp_path,
                                         need_mtools):
    """A ROM streams 100 ms chunks with sample_queue and sleeps in sleep_ms.
    The attach runs in steps for 1200 ms, inside the ROM's calls. At most
    two chunks, 200 ms, are queued ahead, so the stream lives through the
    attach only if the ROM keeps queueing during it. Recorded in
    docs/ARCHITECTURE.md.

    The ROM streams until it gets the card's PIP_EV_TAPE, and the test
    inserts the card after ev=stream. So the stream is live when the attach
    begins and goes on after it ends."""
    sd = boot_card(tmp_path, test_rom)
    card = tape_card(tmp_path, 0, {"SLOT.HTP": test_holo("tape")})

    def attempt():
        with run(holo_build, board, sd, {0: card}) as vm:
            holo(vm, "delay", str(SLOW_MS))
            wait_rom(vm, "wait")
            assert vm.script("dial -1", timeout=TIMEOUT).status == "ok"
            wait_rom(vm, "stream")
            t_in = holo_at(vm, "insert", "0", str(CARD_BLOCKS))
            wait_rom(vm, "tapeev")
            n = int(wait_rom(vm, "streamed")[0]["n"])
            gap = int(wait_rom(vm, "maxgap")[0]["ms"])
            assert tape_events(vm) == [INS], vm.serial
            m, a0, a1 = attach_span(vm)
            pcm0, _, pcm1, _ = pcm_state(m)
            tr = trace_after(vm, "maxgap")
            print(f"\nstream: chunks={n} gap={gap} {m} insert={t_in} audio={tr}")
            check_stepped(m)
            no_exception(vm)
            assert a0 + CLOCKS_MS >= t_in + SETTLE_MS, (t_in, m)
            # The ROM queued during the attach: 1200 ms of chunks at least.
            assert n >= SLOW_MS // STREAM_CHUNK_MS, (n, m)
            dry = [t for op, t in tr if op == "pcm_end" and a0 - CLOCKS_MS <= t <= a1 + CLOCKS_MS]
            if pcm0 and pcm1 and not dry:
                # pos counts one chunk's play, so the trace shows the chain:
                # a chunk switch every 100 ms through the attach.
                nexts = [t for op, t in tr if op == "pcm_next" and a0 <= t <= a1]
                assert len(nexts) >= (a1 - a0 - STREAM_AHEAD_MS) // STREAM_CHUNK_MS, (m, tr)
                return
            # The stream ran dry near or during the attach. Only a ROM that
            # waited a chunk or more between two queue calls explains it.
            assert gap_too_long(gap, STREAM_CHUNK_MS), \
                f"the stream ran dry at {dry} with queue calls {gap} ms apart at most: {m} {tr}"
            raise CannotJudge(f"the ROM queued {gap} ms apart at most, a chunk lasts "
                              f"{STREAM_CHUNK_MS} ms: dry at {dry}, attach {a0} to {a1} ms")

    judged(attempt)


# ---- A pending attach ----------------------------------------------------------

# Long enough that the test's few commands land inside it on any sane host.
PENDING_MS = 3000


def still_pending(vm, s0):
    """Asserts the attach still runs, or raises CannotJudge when the guest
    clock says PENDING_MS ran out under the test's commands. A kernel that
    held the loop for the attach also lets it run out: the attach marker's
    own witness, one step per pass, tells the two apart."""
    live, t = pending(vm)
    if not live:
        check_stepped(wait_markers_ev(vm, "holo_attach"))
        assert t + CLOCKS_MS >= s0 + PENDING_MS, (s0, t, vm.serial)
        raise CannotJudge(f"the attach started at {s0} ms and the check ran at {t} ms, "
                          f"past its {PENDING_MS} ms")


@pytest.mark.parametrize("boot_copy", [False, True])
def test_pending_attach_no_tape_yet(board, holo_build, test_rom, test_holo, tmp_path,
                                    need_mtools, boot_copy):
    """While the attach runs, the slot is SETTLING: no slot entry in the
    listing, no slot tape to launch, no bit. A boot card tape of the same
    name is listed and launched as with no card. Never a half-mounted volume."""
    tape = test_holo("tape")
    sd = boot_card(tmp_path, test_rom, tapes={"SLOT.HTP": tape} if boot_copy else None)
    card = tape_card(tmp_path, 0, {"SLOT.HTP": tape})

    def attempt():
        with run(holo_build, board, sd, {0: card}) as vm:
            holo(vm, "delay", str(PENDING_MS))
            wait_rom(vm, "wait")
            holo(vm, "insert", "0", str(CARD_BLOCKS))
            s0 = start_ms(vm)
            got = listing(vm)
            rc = launch(vm, "dial")
            bits = mask(vm)
            state = vm.script("state", timeout=TIMEOUT).lines[0]
            still_pending(vm, s0)
            assert got == ([("SLOT.HTP", 0)] if boot_copy else []), got
            assert rc == (200 if boot_copy else ENOENT), (rc, vm.serial)
            assert bits == 0 and tape_events(vm) == [], vm.serial
            assert state.endswith(" slot=settling"), state
            assert not pipos_markers(vm, "holo_attach"), vm.serial
            # Then the card mounts as usual.
            wait_rom(vm, "tapeev")
            assert tape_events(vm) == [INS], vm.serial
            assert listing(vm) == [("SLOT.HTP", 1)]
            no_exception(vm)

    judged(attempt)


def test_pull_in_pending_attach_then_second_card(board, holo_build, test_rom, test_holo,
                                                 tmp_path, need_mtools):
    """A pull while the attach runs ends it with no bit. A second card right
    after attaches from its own debounce and reports INSERTED alone."""
    tape = test_holo("tape")
    sd = boot_card(tmp_path, test_rom)
    first = tape_card(tmp_path, 0, {"FIRST.HTP": tape})
    second = tape_card(tmp_path, 2, {"SECOND.HTP": tape})

    def attempt():
        with run(holo_build, board, sd, {0: first, 2: second}) as vm:
            holo(vm, "delay", str(PENDING_MS))
            wait_rom(vm, "wait")
            holo(vm, "insert", "0", str(CARD_BLOCKS))
            s0 = start_ms(vm)
            still_pending(vm, s0)
            holo(vm, "pull")
            m = wait_markers_ev(vm, "holo_attach")
            assert m["rc"] == "-4", m  # BLK_ERR_NOMEDIA
            assert int(m["ms"]) < PENDING_MS, m
            holo(vm, "delay", "0")
            insert(vm, 2)
            assert tape_events(vm) == [INS], vm.serial
            assert mask(vm) == INS
            assert listing(vm) == [("SECOND.HTP", 1)]
            rcs = [a["rc"] for a in pipos_markers(vm, "holo_attach")]
            assert rcs == ["-4", "0"], rcs
            no_exception(vm)

    judged(attempt)


@pytest.mark.parametrize("how", ["slow", "refuse"])
def test_attach_fails_in_steps_then_card_works(board, holo_build, test_rom, test_holo,
                                               tmp_path, need_mtools, how):
    """A stepped attach that ends in SD_ERR_SLOW or SD_ERR_CMD: BAD and its
    PIP_EV_TAPE, no slot tape, until the card leaves. A later card works."""
    tape = test_holo("tape")
    sd = boot_card(tmp_path, test_rom)
    card = tape_card(tmp_path, 0, {"SLOT.HTP": tape})
    with run(holo_build, board, sd, {0: card}) as vm:
        holo(vm, "delay", "300")
        holo(vm, how)
        insert(vm, 0, expect=BAD)
        m = pipos_markers(vm, "holo_attach")[0]
        assert m["rc"] == ("-21" if how == "slow" else "-16"), m
        assert int(m["steps"]) >= 2, m
        assert mask(vm) == BAD
        assert listing(vm) == []
        assert launch(vm, "dial") == ENOENT
        assert vm.script("state", timeout=TIMEOUT).lines[0].endswith(" slot=bad")
        time.sleep(0.5)
        assert len(pipos_markers(vm, "holo_attach")) == 1, vm.serial
        pull(vm)
        insert(vm, 0)
        assert mask(vm) == REM | INS
        assert listing(vm) == [("SLOT.HTP", 1)]
        no_exception(vm)


def test_new_attach_waits_for_slot_tape_end(board, holo_build, test_rom, test_holo, tmp_path,
                                            need_mtools):
    """A new attach needs the slot EMPTY first, so the running slot tape's
    card was seen to leave: the tape ends at its next call, which comes
    within its 20 ms sleep, long before a second card's 250 ms debounce. The
    second card attaches after the tape ended, and its tape is the next."""
    tape = test_holo("tape")
    sd = boot_card(tmp_path, test_rom)
    first = tape_card(tmp_path, 0, {"SLOT.HTP": tape})
    second = tape_card(tmp_path, 2, {"SLOT.HTP": tape})
    with run(holo_build, board, sd, {0: first, 2: second}) as vm:
        insert(vm, 0)
        n = len(rom_markers(vm, "launch"))
        key(vm, "right")
        wait_rom(vm, "tloop")
        holo(vm, "pull")
        holo(vm, "insert", "2", str(CARD_BLOCKS))
        assert int(wait_rom(vm, "launch", n + 1)[-1]["rc"]) == ENODEV
        t = time.monotonic()
        while tape_events(vm).count(INS) < 2:
            assert time.monotonic() - t < TIMEOUT, vm.serial
            time.sleep(0.02)
        kill = vm.serial.index("ev=holo_kill")
        starts = [mm.start() for mm in re.finditer(r"ev=holo_attach_start\b", vm.serial)]
        assert len(starts) == 2 and starts[1] > kill, vm.serial
        assert mask(vm) == INS | REM
        assert listing(vm) == [("SLOT.HTP", 1)]
        assert launch(vm, "dial") == 200
        no_exception(vm)
