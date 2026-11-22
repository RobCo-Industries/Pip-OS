import os, sys  # noqa: E401
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "../external/robco-core/tests"))
pytest_plugins = ["harness.plugin"]

# Pip-OS: rimg for the ROM helpers, and --build-dir for the image under test.
# robco_build() builds into build or build/<variant> only, so a BUILD flag
# picks any other directory and image names the result inside it.

import pytest  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "external", "robco-core", "tools"))


def pytest_addoption(parser):
    parser.addoption("--build-dir", default="build",
                     help="build directory, as BUILD in the Makefile")


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
