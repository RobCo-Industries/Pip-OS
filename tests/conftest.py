"""Pytest options and fixtures for the Pip-OS target tests.

The QEMU harness comes from the RobCo-Core submodule in external/robco-core.

--board NAME   run board-parametrised tests on NAME (repeatable).
               Default: all four boards. "all" selects all four too.
"""

import os
import subprocess
import sys

import pytest

TESTS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TESTS)
CORE = os.path.join(ROOT, "external", "robco-core")
sys.path.insert(0, os.path.join(CORE, "tests"))
sys.path.insert(0, os.path.join(CORE, "tools"))
sys.path.insert(0, TESTS)

from harness.boot import BOARDS  # noqa: E402
from harness.qemu import have_machine  # noqa: E402
from harness.sdimage import have_mtools  # noqa: E402


def pytest_addoption(parser):
    parser.addoption("--board", action="append", default=[],
                     help="board to test on (repeatable, or 'all')")


def selected_boards(config):
    names = config.getoption("--board") or ["all"]
    if "all" in names:
        return list(BOARDS)
    unknown = [n for n in names if n not in BOARDS]
    if unknown:
        raise pytest.UsageError(f"unknown --board {unknown}, expected {list(BOARDS)} or all")
    return list(dict.fromkeys(names))


def pytest_generate_tests(metafunc):
    if "board" in metafunc.fixturenames:
        metafunc.parametrize("board", selected_boards(metafunc.config), indirect=True)


@pytest.fixture
def board(request):
    name = request.param
    if not have_machine(BOARDS[name].binary, name):
        pytest.skip(f"{BOARDS[name].binary} -M {name} not available")
    return name


@pytest.fixture(scope="session")
def kernel_image():
    """Builds the kernel once per session and returns build/pipos.img."""
    subprocess.run(["make", "-s", "-C", ROOT, "CI=1", "all"], check=True,
                   stdout=subprocess.DEVNULL)
    return os.path.join(ROOT, "build", "pipos.img")


@pytest.fixture
def need_mtools():
    if not have_mtools():
        pytest.skip("mtools (mformat, mcopy, mmd) not installed")
