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

from hosttiming import TICK_MS, CannotJudge, judged

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


def audio_trace(vm):
    """(op, t_ms) of each audio trace line, on the audio clock."""
    return [(a["op"], int(a["t_ms"])) for a in pipos_markers(vm, "audio")]


# The attach marker's t0_ms and t1_ms are tick_ms(). The audio trace's t_ms
# is the audio clock, seeded from tick_ms() and within one tick of it.
CLOCKS_MS = TICK_MS
SAMPLE_MS = 7500                # the ROM's dial_push sample: 60000 bytes at 8 kHz


def test_slow_attach_blocks_main_loop(board, holo_build, test_rom, test_holo, tmp_path,
                                      need_mtools):
    """holo_attach() may block 1.2 s inside a work pass: no pipos_poll()
    pass runs meanwhile. The PCM interrupt plays from the kernel's copy of
    the sample, so the sound goes on. Recorded in docs/ARCHITECTURE.md."""
    sd = boot_card(tmp_path, test_rom)
    card = tape_card(tmp_path, 0, {"SLOT.HTP": test_holo("tape")})

    def attempt():
        with run(holo_build, board, sd, {0: card}) as vm:
            holo(vm, "delay", "1200")
            wait_rom(vm, "wait")
            key(vm, "dial_push")
            assert wait_rom(vm, "sample")[0]["rc"] == "0"
            count = len(tape_events(vm))
            t_in = holo_at(vm, "insert", "0", str(CARD_BLOCKS))
            wait_rom(vm, "tapeev", count + 1)
            assert tape_events(vm) == [INS], vm.serial
            m, a0, a1 = attach_span(vm)
            ms, samples, polls = int(m["ms"]), int(m["samples"]), int(m["polls"])
            tr = audio_trace(vm)
            print(f"\nattach: ms={ms} samples={samples} polls={polls} t0={a0} t1={a1} "
                  f"insert={t_in} audio={tr}")
            assert ms >= 1200, m
            # The witness: the main loop's work did not run during the attach.
            assert polls == 0, m
            # Never more samples than due, on any host.
            assert samples <= ms * 8 + 8, m
            no_exception(vm)
            # The sample was playing for the whole attach: it started before
            # t0 and nothing ended it before t1.
            assert tr and tr[0][0] == "pcm" and tr[0][1] <= a0 + CLOCKS_MS, (m, tr)
            end = tr[1] if len(tr) > 1 else None
            if end is not None and end[1] < a1 - CLOCKS_MS:
                assert end[0] == "pcm_end" and end[1] - tr[0][1] >= SAMPLE_MS - CLOCKS_MS, \
                    f"the sound stopped during the attach: {m} {tr}"
                # The whole sample played out before the attach ended. Only
                # a late insert explains it: an attach right after the
                # debounce would still end after the sample.
                assert t_in + SETTLE_MS + 1200 + CLOCKS_MS >= end[1], \
                    f"the insert at {t_in} ms left time to attach before the sample ended: " \
                    f"{m} {tr}"
                raise CannotJudge(f"the insert came at {t_in} ms, the sample ended at {end[1]} ms, "
                                  f"before the attach ended at {a1} ms")
            # The PCM interrupt ran during it. How many samples it output
            # depends on how often QEMU delivers the timer interrupt on this
            # host: a late interrupt skips to the sample due.
            assert samples > 0, m

    judged(attempt)


# The kernel holds two sample_queue buffers: one plays, one waits.
STREAM_CHUNK_MS = 100
STREAM_AHEAD_MS = 2 * STREAM_CHUNK_MS
# tests/roms/holo_slot.c: the stream stops here when no PIP_EV_TAPE comes.
STREAM_MAX_MS = 10000


def test_slow_attach_runs_a_stream_dry(board, holo_build, test_rom, test_holo, tmp_path,
                                       need_mtools):
    """A ROM streams 100 ms chunks with sample_queue and sleeps in sleep_ms.
    The attach runs inside that sleep_ms for 1200 ms. At most two chunks,
    200 ms, were queued ahead, so the play ends during the attach and the
    next chunk starts a new play after it. Recorded in docs/ARCHITECTURE.md.

    The ROM streams until it gets the card's PIP_EV_TAPE, and the test
    inserts the card after ev=stream. So the stream is live when the attach
    begins and goes on after it ends: the kernel's order makes the overlap.
    Only the ROM's 10 s cap after a late insert, or a ROM that queued a chunk
    late before the attach, could end the stream first."""
    sd = boot_card(tmp_path, test_rom)
    card = tape_card(tmp_path, 0, {"SLOT.HTP": test_holo("tape")})

    def attempt():
        with run(holo_build, board, sd, {0: card}) as vm:
            holo(vm, "delay", "1200")
            wait_rom(vm, "wait")
            assert vm.script("dial -1", timeout=TIMEOUT).status == "ok"
            wait_rom(vm, "stream")
            t_in = holo_at(vm, "insert", "0", str(CARD_BLOCKS))
            n = int(wait_rom(vm, "streamed")[0]["n"])
            assert tape_events(vm) == [INS], vm.serial
            m, a0, a1 = attach_span(vm)
            ms = int(m["ms"])
            tr = audio_trace(vm)
            lastq = rom_markers(vm, "lastq")
            print(f"\nstream: chunks={n} attach ms={ms} t0={a0} t1={a1} insert={t_in} "
                  f"lastq={lastq} audio={tr}")
            assert ms >= 1200 and m["polls"] == "0", m
            no_exception(vm)
            # The test inserts after ev=stream, so the first play starts
            # before the insert, and the attach comes a debounce later.
            assert tr and tr[0][0] == "pcm" and tr[0][1] <= t_in + CLOCKS_MS, (t_in, tr)
            assert a0 + CLOCKS_MS >= t_in + SETTLE_MS, (t_in, m)
            before = [k for k, (_, t) in enumerate(tr) if t < a0 - CLOCKS_MS]
            assert before, (m, tr)
            last = before[-1]
            if tr[last][0] in ("pcm_end", "stop") and last == len(tr) - 1 and \
                    tr[last][1] - tr[0][1] >= STREAM_MAX_MS - 2 * CLOCKS_MS:
                # The cap ended the stream first. Only a late insert explains
                # it: an attach a debounce after it would find the stream live.
                assert t_in + SETTLE_MS + CLOCKS_MS >= tr[last][1], \
                    f"the insert at {t_in} ms left time to attach in the stream: {m} {tr}"
                raise CannotJudge(f"the insert came at {t_in} ms, the stream hit its cap at "
                                  f"{tr[last][1]} ms, before the attach at {a0} ms")
            if tr[last][0] == "pcm_end":
                # The stream ran dry before the attach. The ROM's last queue
                # call before the attach was full, or filled it, so at least
                # a chunk was queued then: only a ROM late by a chunk explains it.
                assert len(lastq) == 1, (lastq, vm.serial)
                tq = int(lastq[0]["t_ms"])
                late = tr[last][1] - tq
                assert late + 2 * CLOCKS_MS >= STREAM_CHUNK_MS, \
                    f"the stream ran dry {late} ms after the ROM's last queue call at {tq} ms, " \
                    f"before the attach: {m} {tr}"
                raise CannotJudge(f"the ROM's last queue call before the attach came at {tq} ms, "
                                  f"the stream ran dry at {tr[last][1]} ms, {late} ms later, "
                                  f"before the attach at {a0} ms")
            # 1. Live: a chunk was playing when the attach began.
            assert tr[last][0] in ("pcm", "pcm_next"), (m, tr)
            # 2. The play ends inside the attach. At its start at most the
            # rest of the playing chunk and one waiting chunk were queued,
            # STREAM_AHEAD_MS, so it ends by t0 + 200 ms, one tick between
            # the clocks.
            end = next((k for k in range(last + 1, len(tr)) if tr[k][0] != "pcm_next"), None)
            assert end is not None and tr[end][0] == "pcm_end", (m, tr)
            assert a0 - CLOCKS_MS <= tr[end][1] <= a0 + STREAM_AHEAD_MS + CLOCKS_MS, (m, tr)
            # 3. The next chunk starts a new play after the attach ends.
            assert end + 1 < len(tr) and tr[end + 1][0] == "pcm", (m, tr)
            assert tr[end + 1][1] >= a1 - CLOCKS_MS, (m, tr)
            # 4. The silence lasts the attach less what was queued at its
            # start, less one tick: the new play's t_ms is the audio clock,
            # which moves at each 10 ms tick.
            gap = tr[end + 1][1] - tr[end][1]
            assert gap >= ms - STREAM_AHEAD_MS - TICK_MS, (gap, m, tr)
            no_exception(vm)

    judged(attempt)
