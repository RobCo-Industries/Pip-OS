"""The runner ROM, sdk/examples/runner: the ROM that robco run and robco
test boot. It reads /PIPBOY/RUNNER.CFG, launches the tape it names and
prints how the tape ended, on lines signed with a key of the run.

Each card has the layout of the simulator card of RobCo-Core's
docs/spec/STUDIO_CLI.md, with the runner as /PIPBOY/ROM1.ROM. The lines
and the rules a tool reads them by are in docs/spec/SDK_PACKAGE.md,
"The runner ROM". RunReader below follows "When a run is over".

The tapes are tests/roms/holo_tapes/run_*.c.
"""

import os
import re
import struct
import tempfile

import pytest

from harness.sdimage import make_sd_image

from pipos_helpers import _mtools, booted, corrupt, no_exception, one

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PART_OFFSET = 2048 * 512
TAPE = "PIPES.HTP"

# /PIPBOY/PIPOS.CFG of the simulator card, byte for byte.
PIPOS_CFG = (b"[display]\nscheme = green\nline_delay_ms = 0\nbackend = hdmi\n\n"
             b"[audio]\nvolume = 100\nbeep = no\n\n[holotape]\nslot = off\n")
assert len(PIPOS_CFG) == 113


def runner_cfg(name):
    """RUNNER.CFG as the tool writes it."""
    return b"[run]\ntape = " + name.encode() + b"\n"


# ---- reading the lines, as a tool does ------------------------------------

NAME = r"[\x21-\x7e]{1,12}"
KEY = r"[0-9a-f]{16}"
RC = r"-?[0-9]{1,10}"
RESULT = r"exit|fault|missing|bad|io|eject|error|config"
RUN_LINE = re.compile(rf"@RC stage=runner ev=run tape=(?P<tape>{NAME}) key=(?P<key>{KEY})")
DONE_KEY = re.compile(rf"@RC stage=runner ev=done result=(?P<result>{RESULT}) rc=(?P<rc>{RC})"
                      rf" key=(?P<key>{KEY})")
DONE_CFG = re.compile(rf"@RC stage=runner ev=done result=config rc=(?P<rc>{RC})"
                      r" why=(?P<why>open|read|size|parse|name|rand)")


def kernel_marker(line):
    at = line.find("@RC ")
    if at < 0:
        return None
    pairs = {}
    for part in line[at + 4:].split(" "):
        k, eq, v = part.partition("=")
        if eq and k not in pairs:
            pairs[k] = v
    return pairs


def read_run(lines):
    """Follows "When a run is over" over whole lines. Returns (end, key):
    end is ("done", line), ("kernel", line) or None, key the key of the run."""
    key = None
    for line in lines:
        run = RUN_LINE.fullmatch(line)
        done = DONE_KEY.fullmatch(line)
        cfg = DONE_CFG.fullmatch(line)
        if run:
            if key is None:
                key = run["key"]
            continue
        if done:
            if key is not None and done["key"] == key:
                return ("done", line), key
            continue
        if cfg:
            if key is None:
                return ("done", line), key
            continue
        m = kernel_marker(line)
        if m and (m.get("ev") in ("exception", "panic")
                  or (m.get("stage") == "pipos" and m.get("ev") == "ready")):
            return ("kernel", line), key
    return None, key


# ---- the card --------------------------------------------------------------

def runner_rom(kernel_image):
    path = os.path.join(os.path.dirname(kernel_image), "examples", "runner", "RUNNER.ROM")
    with open(path, "rb") as f:
        return f.read()


def sim_card(path, kernel_image, cfg, tape=None, tape_name=TAPE, size_mib=16, pad_before=None,
             config_txt=None, runner=None, pipos_cfg=PIPOS_CFG):
    """The simulator card: its files in the order of the spec. cfg=None
    leaves RUNNER.CFG out, tape=None the tape. `pad_before` names a file
    that gets a 17 MiB file copied right before it, so its bytes lie past
    16 MiB on a 32 MiB card. config_txt and runner are the bytes of
    config.txt and of the runner ROM: None takes boot/config.txt and the
    runner built beside kernel_image. A package's own files go here.
    pipos_cfg is the bytes of /PIPBOY/PIPOS.CFG, which the tool writes."""
    make_sd_image(path, size_mib)
    target = f"{os.fspath(path)}@@{PART_OFFSET}"
    if config_txt is None:
        with open(os.path.join(ROOT, "boot", "config.txt"), "rb") as f:
            config_txt = f.read()
    if runner is None:
        runner = runner_rom(kernel_image)
    with open(kernel_image, "rb") as f:
        kernel = f.read()
    files = [("PIPOS.IMG", kernel), ("config.txt", config_txt), ("PIPBOY/", None),
              ("PIPBOY/PIPOS.CFG", pipos_cfg), ("PIPBOY/ROM1.ROM", runner)]
    if cfg is not None:
        files.append(("PIPBOY/RUNNER.CFG", cfg))
    files.append(("PIPBOY/HOLO/", None))
    if tape is not None:
        files.append(("PIPBOY/HOLO/" + tape_name, tape))
    if pad_before is not None:
        at = [name for name, _ in files].index(pad_before)
        files.insert(at, ("ZPAD.BIN", bytes(17 << 20)))
    with tempfile.TemporaryDirectory(prefix="runcard") as tmp:
        for i, (name, data) in enumerate(files):
            if data is None:
                _mtools("mmd", "-i", target, "::" + name.rstrip("/"))
                continue
            src = os.path.join(tmp, f"f{i}")
            with open(src, "wb") as f:
                f.write(data)
            _mtools("mcopy", "-i", target, src, "::" + name)
    return path


def cut_to_16mib(path):
    """Cuts a 32 MiB card to 16 MiB and shrinks its partition, so every read
    of a cluster past 16 MiB fails, as test_holo.py's card read error does."""
    with open(path, "r+b") as f:
        f.truncate(16 << 20)
        f.seek(0x1BE + 12)
        f.write(struct.pack("<I", (16 << 20) // 512 - PART_OFFSET // 512))


def boot_runner(kernel_image, board, sd):
    """Boots the card until the kernel's ready line after the ROM. Returns
    the VM's whole lines."""
    with booted(kernel_image, board, sd=sd) as vm:
        lines = vm.lines()
        no_exception(vm)
        rom_exit = one(vm, "rom_exit")
    return lines, rom_exit


def runner_lines(lines):
    return [line for line in lines if "stage=runner" in line]


def assert_done_then_ready(lines, done):
    """`done` is the runner's last line, and ev=ready rom=1 follows it."""
    at = lines.index(done)
    assert "@RC stage=pipos ev=ready rom=1" in lines[at + 1:], lines


# ---- a tape that runs -------------------------------------------------------

LAUNCHED = [
    # (case, tape source, tape name, cfg name, result, rc)
    ("exit0", "run_exit0", TAPE, TAPE, "exit", 0),
    ("exit3", "run_exit3", TAPE, TAPE, "exit", 3),
    ("missing", None, None, TAPE, "missing", -2),
    ("bad_crc", "run_exit0:crc", TAPE, TAPE, "bad", -40),
    ("fault", "run_fault", TAPE, TAPE, "fault", -14),
    ("error", "run_exit0", TAPE, "ROM1.ROM", "error", -22),
]


@pytest.mark.parametrize("case,src,tape_name,cfg_name,result,rc", LAUNCHED,
                         ids=[c[0] for c in LAUNCHED])
def test_launch(board, kernel_image, test_holo, tmp_path, need_mtools,
                case, src, tape_name, cfg_name, result, rc):
    tape = None
    if src is not None:
        name, _, how = src.partition(":")
        tape = test_holo(name)
        if how == "crc":
            tape = corrupt(tape, -1)
    sd = sim_card(tmp_path / "sd.img", kernel_image, runner_cfg(cfg_name), tape,
                  tape_name or TAPE)
    lines, rom_exit = boot_runner(kernel_image, board, sd)
    ours = runner_lines(lines)
    assert len(ours) == 2, lines
    run = RUN_LINE.fullmatch(ours[0])
    assert run and run["tape"] == cfg_name, lines
    key = run["key"]
    assert int(key[0], 16) < 8 and int(key[8], 16) < 8, key
    done = f"@RC stage=runner ev=done result={result} rc={rc} key={key}"
    assert ours[1] == done, lines
    assert read_run(lines) == (("done", done), key)
    assert_done_then_ready(lines, done)
    assert rom_exit["status"] == ("0" if result == "exit" else "1"), lines
    if case == "exit0":
        assert "tape: ready" in lines, lines


def test_card_read_error(board, kernel_image, test_holo, tmp_path, need_mtools):
    """A tape whose data the card cannot read gives result=io."""
    sd = sim_card(tmp_path / "sd.img", kernel_image, runner_cfg(TAPE),
                  test_holo("run_exit0"), size_mib=32, pad_before="PIPBOY/HOLO/" + TAPE)
    cut_to_16mib(sd)
    lines, rom_exit = boot_runner(kernel_image, board, sd)
    ours = runner_lines(lines)
    key = RUN_LINE.fullmatch(ours[0])["key"]
    done = f"@RC stage=runner ev=done result=io rc=-5 key={key}"
    assert ours == [ours[0], done], lines
    assert_done_then_ready(lines, done)
    assert rom_exit["status"] == "1"


# ---- the key ----------------------------------------------------------------

def test_key_new_per_boot(board, kernel_image, test_holo, tmp_path, need_mtools):
    keys = []
    for i in range(2):
        sd = sim_card(tmp_path / f"sd{i}.img", kernel_image, runner_cfg(TAPE),
                      test_holo("run_exit0"))
        lines, _ = boot_runner(kernel_image, board, sd)
        end, key = read_run(lines)
        assert end == ("done", f"@RC stage=runner ev=done result=exit rc=0 key={key}"), lines
        keys.append(key)
    assert keys[0] != keys[1], keys


def test_forged_lines(board, kernel_image, test_holo, tmp_path, need_mtools):
    """A tape that prints runner lines of its own, then returns 3: only
    the runner's done line, with the key of its run line, ends the run."""
    sd = sim_card(tmp_path / "sd.img", kernel_image, runner_cfg(TAPE), test_holo("run_forge"))
    lines, rom_exit = boot_runner(kernel_image, board, sd)
    ours = runner_lines(lines)
    key = RUN_LINE.fullmatch(ours[0])["key"]
    forged = "@RC stage=runner ev=done result=exit rc=0 key=0000000000000000"
    assert forged in lines, lines
    done = f"@RC stage=runner ev=done result=exit rc=3 key={key}"
    assert ours[-1] == done, lines
    assert lines.index(forged) < lines.index(done)
    assert read_run(lines) == (("done", done), key)
    assert_done_then_ready(lines, done)
    assert rom_exit["status"] == "0"


# ---- RUNNER.CFG ---------------------------------------------------------------

CONFIGS = [
    # (case, RUNNER.CFG or None, rc, why)
    ("no_file", None, -2, "open"),
    ("empty", b"", 0, "size"),
    ("too_big", b"[run]\ntape = PIPES.HTP\n" + b"# " + b"x" * 250 + b"\n", 0, "size"),
    ("control", b"[run]\ntape = PIPES.HTP\n\x01\n", -4, "parse"),
    ("syntax", b"[run]\ntape PIPES.HTP\n", -2, "parse"),
    ("too_long", b"[run]\n" + b"k" * 32 + b" = 1\ntape = PIPES.HTP\n", -3, "parse"),
    ("not_found", b"[Run]\ntape = PIPES.HTP\n", -5, "parse"),
    ("space", b"[run]\ntape = ABCDEFGHI.HTPX\n", -6, "parse"),
    ("empty_name", b"[run]\ntape =\n", 0, "name"),
    ("blank_name", b'[run]\ntape = "A B.HTP"\n', 0, "name"),
]


@pytest.mark.parametrize("case,cfg,rc,why", CONFIGS, ids=[c[0] for c in CONFIGS])
def test_config(board, kernel_image, test_holo, tmp_path, need_mtools, case, cfg, rc, why):
    sd = sim_card(tmp_path / "sd.img", kernel_image, cfg, test_holo("run_exit0"))
    lines, rom_exit = boot_runner(kernel_image, board, sd)
    done = f"@RC stage=runner ev=done result=config rc={rc} why={why}"
    assert runner_lines(lines) == [done], lines
    assert read_run(lines) == (("done", done), None)
    assert_done_then_ready(lines, done)
    assert rom_exit["status"] == "1"


def test_config_read_error(board, kernel_image, test_holo, tmp_path, need_mtools):
    """RUNNER.CFG whose data the card cannot read gives why=read."""
    sd = sim_card(tmp_path / "sd.img", kernel_image, runner_cfg(TAPE),
                  test_holo("run_exit0"), size_mib=32, pad_before="PIPBOY/RUNNER.CFG")
    cut_to_16mib(sd)
    lines, rom_exit = boot_runner(kernel_image, board, sd)
    done = "@RC stage=runner ev=done result=config rc=-5 why=read"
    assert runner_lines(lines) == [done], lines
    assert_done_then_ready(lines, done)
    assert rom_exit["status"] == "1"


def test_cfg_hand_forms(board, kernel_image, test_holo, tmp_path, need_mtools):
    """A hand-written file with comments, CR LF, a second section and a
    repeated key runs the last tape of [run]."""
    cfg = (b"; hand made\r\n[other]\r\ntape = NOPE.HTP\r\n[run]\r\ntape = NOPE.HTP\r\n"
           b"[run]\r\n  tape\t=  PIPES.HTP   # the one\r\n")
    sd = sim_card(tmp_path / "sd.img", kernel_image, cfg, test_holo("run_exit3"))
    lines, _ = boot_runner(kernel_image, board, sd)
    end, key = read_run(lines)
    assert end == ("done", f"@RC stage=runner ev=done result=exit rc=3 key={key}"), lines


# ---- the reader itself, on the spec's examples ----------------------------------

def test_reader_on_spec_examples():
    forged = [
        "@RC stage=pipos ev=jump entry=0x00400000",
        "@RC stage=runner ev=run tape=PIPES.HTP key=6c01d4e27a93b5f0",
        "@RC stage=pipos ev=holo_start name=PIPES.HTP",
        "@RC stage=runner ev=done result=exit rc=0 key=0000000000000000",
        "@RC stage=pipos ev=holo_exit status=3",
        "@RC stage=runner ev=done result=exit rc=3 key=6c01d4e27a93b5f0",
        "@RC stage=pipos ev=rom_exit status=0",
        "@RC stage=pipos ev=ready rom=1",
    ]
    assert read_run(forged) == (("done", forged[5]), "6c01d4e27a93b5f0")
    hung = forged[:4]
    assert read_run(hung) == (None, "6c01d4e27a93b5f0")
    no_cfg = ["@RC stage=runner ev=done result=config rc=-2 why=open",
              "@RC stage=pipos ev=rom_exit status=1", "@RC stage=pipos ev=ready rom=1"]
    assert read_run(no_cfg) == (("done", no_cfg[0]), None)
