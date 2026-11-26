import os, sys  # noqa: E401

# The SDK's test package, the way a ROM project loads it. Importing it puts
# RobCo-Core's harness and tools (rimg) on sys.path. The plugin loads
# RobCo-Core's harness.plugin. Pip-OS adds --build-dir for the image under
# test. robco_build() builds into build or build/<variant> only, so a BUILD
# flag picks any other directory and image names the result inside it.
# hosttiming adds the session rule for "host timing:" skips and
# --timing-skips-max.
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "sdk", "testing"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
pytest_plugins = ["pipos_testing.plugin", "hosttiming"]

import pytest  # noqa: E402


def pytest_addoption(parser):
    parser.addoption("--build-dir", default="build",
                     help="build directory, as BUILD in the Makefile")
    parser.addoption("--sdk-zip", default=None,
                     help="test_sdk_boot.py: the SDK package to boot, pipos-<version>.zip")
    parser.addoption("--sdk-tape", default=None,
                     help="test_sdk_boot.py: the holotape the package's runner launches")
    parser.addoption("--boot-sound-log", default=None,
                     help="test_boot_sound_log.py: where to save the serial log of the boot")


def pytest_ignore_collect(collection_path, config):
    """test_sdk_boot.py runs only when it is given a package, and
    test_boot_sound_log.py only when it is given a log path, so the suite
    neither skips them nor boots what it was not asked for."""
    if collection_path.name == "test_sdk_boot.py" and not config.getoption("--sdk-zip"):
        return True
    if collection_path.name == "test_boot_sound_log.py" and not config.getoption("--boot-sound-log"):
        return True
    return None


def build_args(build_dir):
    """robco_build() arguments that build into build_dir and return its image."""
    path = os.path.join(ROOT, build_dir)
    return {"flags": (f"BUILD={path}",), "image": os.path.join(path, "pipos.img")}


@pytest.fixture(scope="session")
def kernel_image(request, robco_build):
    path = robco_build(ROOT, **build_args(request.config.getoption("--build-dir")))
    print(f"\nkernel image: {path}")
    return path


@pytest.fixture(scope="session")
def test_rom(request, kernel_image):
    """test_rom(name) returns the bytes of <build>/roms/<name>/<name>.rom, built with the kernel."""
    roms = os.path.join(ROOT, request.config.getoption("--build-dir"), "roms")

    def get(name):
        with open(os.path.join(roms, name, f"{name}.rom"), "rb") as f:
            return f.read()

    return get


@pytest.fixture(scope="session")
def test_holo(request, kernel_image):
    """test_holo(name) returns the bytes of <build>/holotapes/<name>/<name>.htp, built with the kernel."""
    tapes = os.path.join(ROOT, request.config.getoption("--build-dir"), "holotapes")

    def get(name):
        with open(os.path.join(tapes, name, f"{name}.htp"), "rb") as f:
            return f.read()

    return get
