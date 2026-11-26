"""Boots the kernel that an SDK package ships, with only what is in it.

The QEMU suite boots the kernel of this checkout's compiler. The package's
kernel comes from the compiler `make sdk-package` was given, the pinned
xPack one in CI. This file unpacks a package, builds the simulator card of
RobCo-Core's docs/spec/STUDIO_CLI.md from the package's kernel, config.txt
and runner ROM, the PIPOS.CFG and RUNNER.CFG the spec has the tool write,
and one tape that stands in for the project's build, starts QEMU with the arguments the package's
boot.json gives each board, and waits for the runner's signed done line
with result=exit rc=0.

It runs only when given a package:

    pytest tests/test_sdk_boot.py --sdk-zip build/pipos-1.4.0.zip \
        --sdk-tape build/holotapes/run_exit0/run_exit0.htp --board all

`make sdk-boot-test SDK_ZIP=<zip>` builds the tape and runs it. Nothing
here skips: a board QEMU lacks is a failure.
"""

import contextlib
import json
import os
import shutil
import subprocess
import zipfile

import pytest

from harness.boot import Vm, _connect_unix
from harness.env import minimal_env
from harness.plugin import have_mtools, selected_boards
from harness.qemu import COMMON_ARGS, KEEPALIVE_INTERVAL, Qemu, keepalive_default

from pipos_helpers import READY, TIMEOUT, no_exception, one
from test_runner import PART_OFFSET, RUN_LINE, read_run, runner_lines, sim_card

# The boards boot.json must list, by RobCo-Core's docs/spec/TESTING.md.
BOARDS = ["raspi0", "raspi1ap", "raspi2b", "raspi3b"]
# What the run takes from the package. boot/PIPOS.CFG is not among them:
# the spec has the tool write the card's PIPOS.CFG, below.
NEEDED = ("boot.json", "boot/PIPOS.IMG", "boot/config.txt", "boot/fwstub.bin",
          "boot/RUNNER.ROM")

# The project name of the stand-in tape.
NAME = "PIPES"
TAPE = NAME + ".HTP"
# /PIPBOY/PIPOS.CFG as STUDIO_CLI.md "The simulator card" has the tool write
# it: 11 lines, 113 bytes. Written here, not taken from the package.
CARD_PIPOS_CFG = (b"[display]\n"
                  b"scheme = green\n"
                  b"line_delay_ms = 0\n"
                  b"backend = hdmi\n"
                  b"\n"
                  b"[audio]\n"
                  b"volume = 100\n"
                  b"beep = no\n"
                  b"\n"
                  b"[holotape]\n"
                  b"slot = off\n")
assert len(CARD_PIPOS_CFG) == 113 and CARD_PIPOS_CFG.count(b"\n") == 11
# /PIPBOY/RUNNER.CFG as SDK_PACKAGE.md "What the tool writes" has it.
CARD_RUNNER_CFG = b"[run]\ntape = " + NAME.encode() + b".HTP\n"


@pytest.fixture(scope="module")
def package(request, tmp_path_factory):
    """The package unpacked: a dict of its files' paths, and boot.json."""
    zpath = request.config.getoption("--sdk-zip")
    out = tmp_path_factory.mktemp("sdkpkg")
    with zipfile.ZipFile(zpath) as z:
        names = z.namelist()
        for n in NEEDED:
            assert n in names, f"{zpath} has no {n}"
            dest = out / n
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(z.read(n))
    with open(out / "boot.json", encoding="utf-8") as f:
        doc = json.load(f)
    return {n: str(out / n) for n in NEEDED}, doc


@pytest.fixture(scope="module")
def tape(request):
    path = request.config.getoption("--sdk-tape")
    assert path, "--sdk-tape names the holotape the runner launches"
    with open(path, "rb") as f:
        return f.read()


def pytest_generate_tests(metafunc):
    if "sdk_board" in metafunc.fixturenames:
        metafunc.parametrize("sdk_board", selected_boards(metafunc.config))


def qemu_path(s):
    """A path as it enters a QEMU option: a comma doubled."""
    return s.replace(",", ",,")


def fill(args, values):
    out = []
    for a in args:
        for k, v in values.items():
            a = a.replace("{" + k + "}", qemu_path(v))
        out.append(a)
    return out


@contextlib.contextmanager
def boot_from_json(doc, board, stub, image, sd):
    """Starts QEMU as boot.json says for `board`, in its default mode, with
    the serial ports of the harness's boot(), and yields a Vm. QEMU starts
    stopped and runs once the ports are connected, so no line is lost.

    harness.boot.boot() takes no ready argument list: it builds the
    arguments from the harness's own board table. So this uses the same
    Qemu and Vm classes, which bring the watcher, the pidfile and the
    sweep, and starts the same QMP keep-alive after cont, which a boot of
    raspi0 or raspi1ap needs on the GitHub Linux runner."""
    b = doc["boards"][board]
    # harness.qemu.Qemu adds -M and COMMON_ARGS itself.
    assert doc["common"] == COMMON_ARGS, doc["common"]
    args = fill(b["modes"][b["default_mode"]], {"stub": stub, "image": image})
    args += fill(doc["sd"], {"sd": sd})
    assert shutil.which(b["qemu"]), f"{b['qemu']} is not on the path"
    q = Qemu(b["qemu"], b["machine"], timeout=TIMEOUT, null_serial=False)
    paths = [q._sock(f"serial{i}") for i in range(2)]
    serial = []
    for i, p in enumerate(paths):
        serial += ["-chardev", f"socket,id=ser{i},path={p},server=on,wait=off",
                   "-serial", f"chardev:ser{i}"]
    q.extra = serial + ["-S"] + args
    vm = None
    try:
        q.start()
        socks = [_connect_unix(p, q, TIMEOUT) for p in paths]
        vm = Vm(q, socks)
        q.execute("cont")
        if keepalive_default():
            q.start_keepalive(KEEPALIVE_INTERVAL)
        yield vm
    finally:
        if vm is not None:
            vm.close()
        else:
            q.close()


def _mtools(*args):
    """Runs an mtools command and returns its output bytes."""
    env = minimal_env(MTOOLS_SKIP_CHECK="1")
    return subprocess.run(list(args), check=True, env=env, capture_output=True).stdout


def card_read(sd, name):
    return _mtools("mtype", "-i", f"{os.fspath(sd)}@@{PART_OFFSET}", "::" + name)


def card_files(sd):
    """Every file on the card, as paths without the leading slash."""
    out = _mtools("mdir", "-/", "-b", "-i", f"{os.fspath(sd)}@@{PART_OFFSET}", "::")
    names = [line.split("::/", 1)[1] for line in out.decode().splitlines() if "::/" in line]
    return sorted(n for n in names if not n.endswith("/"))


def test_boot_json_lists_the_four_boards(package, request):
    _, doc = package
    assert sorted(doc["boards"]) == BOARDS
    if "all" in request.config.getoption("--board"):
        assert sorted(selected_boards(request.config)) == BOARDS


def test_package_kernel_runs_a_tape(package, tape, sdk_board, tmp_path):
    assert have_mtools(), "mtools (mformat, mcopy, mmd) are not installed"
    files, doc = package
    assert sdk_board in doc["boards"], f"boot.json has no {sdk_board}"
    with open(files["boot/config.txt"], "rb") as f:
        config_txt = f.read()
    with open(files["boot/RUNNER.ROM"], "rb") as f:
        runner = f.read()
    with open(files["boot/PIPOS.IMG"], "rb") as f:
        kernel = f.read()
    sd = sim_card(tmp_path / "sd.img", files["boot/PIPOS.IMG"], CARD_RUNNER_CFG, tape,
                  tape_name=TAPE, config_txt=config_txt, runner=runner,
                  pipos_cfg=CARD_PIPOS_CFG)
    # Each file on the card, read back, has the source the spec names.
    want = {
        "PIPOS.IMG": kernel,                   # the package
        "config.txt": config_txt,              # the package
        "PIPBOY/PIPOS.CFG": CARD_PIPOS_CFG,    # the tool, by the spec
        "PIPBOY/ROM1.ROM": runner,             # the package's RUNNER.ROM
        "PIPBOY/RUNNER.CFG": CARD_RUNNER_CFG,  # the tool, by the spec
        "PIPBOY/HOLO/" + TAPE: tape,           # the project, here a stand-in
    }
    assert card_files(sd) == sorted(want), card_files(sd)
    for name, data in want.items():
        assert card_read(sd, name) == data, f"{name} on the card is not from its source"
    with boot_from_json(doc, sdk_board, files["boot/fwstub.bin"], files["boot/PIPOS.IMG"],
                        os.fspath(sd)) as vm:
        assert vm.qemu.keepalive_on == keepalive_default(), vm.qemu.keepalive_status()
        assert vm.qemu.machine == doc["boards"][sdk_board]["machine"]
        # The kernel and the stub are the package's, not the harness's.
        cmd = " ".join(vm.qemu.command_line())
        assert qemu_path(files["boot/PIPOS.IMG"]) in cmd, cmd
        assert qemu_path(files["boot/fwstub.bin"]) in cmd, cmd
        vm.wait_for(READY, timeout=TIMEOUT)
        lines = vm.lines()
        no_exception(vm)
        rom_exit = one(vm, "rom_exit")
    end, key = read_run(lines)
    assert key is not None, lines
    assert RUN_LINE.fullmatch(runner_lines(lines)[0])["tape"] == TAPE, lines
    assert end == ("done", f"@RC stage=runner ev=done result=exit rc=0 key={key}"), lines
    assert rom_exit["status"] == "0", lines
