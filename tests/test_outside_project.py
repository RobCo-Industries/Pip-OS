"""A ROM project outside the Pip-OS tree runs its own pytest suite.

The project lives in a temporary folder: a ROM that never exits, a
Makefile that includes sdk/pipos-app.mk, and a tests/conftest.py that
uses only the import documented in docs/TESTING.md. Its one test boots
the ROM with booted(..., wait="jump"), waits for the line the ROM logs,
presses a button and waits for the ROM to log it. One board is enough:
this tests the import and the wait, not the boards.
"""

import os
import subprocess
import sys
import textwrap

from harness.env import minimal_env

from conftest import ROOT

BOARD = "raspi2b"

ROM_C = r"""
#include <pipos/pip.h>

int pip_main(const pipos_startup_t *startup)
{
    pip_event_t ev;

    (void)startup;
    pip_printf("outside: started\n");
    for (;;) {
        if (pip_wait_event(&ev, PIP_WAIT_FOREVER) == 1 && ev.type == PIP_EV_BUTTON) {
            pip_printf("outside: button %u %d\n", (unsigned)ev.code, (int)ev.value);
        }
    }
}
"""

MAKEFILE = """\
PIPOS_SDK := {sdk}
IMAGE     := ROM1.ROM
KIND      := rom
NAME      := OUTSIDE
SRCS      := src/main.c
include $(PIPOS_SDK)/pipos-app.mk
"""

CONFTEST = """\
import os
import sys

PIP_OS = {root!r}
sys.path.insert(0, os.path.join(PIP_OS, "sdk", "testing"))
pytest_plugins = ["pipos_testing.plugin"]
"""

TEST = """\
from pipos_testing import booted, make_card, no_exception, press

KERNEL = {kernel!r}
ROM = {rom!r}


def test_rom_starts_and_reads_a_button(board, tmp_path, need_mtools):
    with open(ROM, "rb") as f:
        sd = make_card(tmp_path / "sd.img", {{"PIPBOY/ROM1.ROM": f.read()}})
    with booted(KERNEL, board, sd=sd, wait="jump") as vm:
        vm.wait_for("outside: started\\n")
        press(vm, "select")
        vm.wait_for("outside: button 4 1\\n")
        vm.wait_for("outside: button 4 0\\n")
        assert "ev=ready" not in vm.serial, vm.serial
        no_exception(vm)
"""


def test_outside_project_runs_its_tests(kernel_image, tmp_path, need_mtools):
    proj = tmp_path / "proj"
    (proj / "src").mkdir(parents=True)
    (proj / "tests").mkdir()
    (proj / "src" / "main.c").write_text(ROM_C)
    (proj / "Makefile").write_text(MAKEFILE.format(sdk=os.path.join(ROOT, "sdk")))
    (proj / "pytest.ini").write_text("[pytest]\n")
    (proj / "tests" / "conftest.py").write_text(CONFTEST.format(root=ROOT))
    (proj / "tests" / "test_outside.py").write_text(textwrap.dedent(TEST.format(
        kernel=os.path.abspath(kernel_image), rom=str(proj / "build" / "ROM1.ROM"))))

    env = minimal_env(CI=None)
    subprocess.run(["make", "-s", "-C", str(proj)], check=True, env=env,
                   stdout=subprocess.DEVNULL)
    log = tmp_path / "pytest.log"
    with open(log, "w") as out:
        rc = subprocess.run([sys.executable, "-m", "pytest", "-q", "-p", "no:cacheprovider",
                             "--board", BOARD, "tests"], cwd=proj, env=env,
                            stdout=out, stderr=subprocess.STDOUT).returncode
    text = log.read_text()
    assert rc == 0, text
    assert "1 passed" in text, text
