"""Helpers for the Pip-OS target tests: ROM files, cards and boots.

ROM files follow IMAGE_FORMAT.md in RobCo-Core: a branch stub, a ROM1
header with the META flag, a META block and a payload. tools/rimg.py from
the submodule seals them. The payload never runs in these tests.
"""

import contextlib
import re
import struct
import time

import rimg
from PIL import Image
from harness.boot import boot
from harness.sdimage import make_sd_image

TIMEOUT = 60
READY = re.compile(r"@RC stage=pipos ev=ready rom=\d\n")
SCREEN = re.compile(r"@RC stage=pipos ev=screen [^\n]*\n(.*?)@RC stage=pipos ev=screen_end\n",
                    re.S)

ROM_BASE = 0x00400000
ROM_SIZE = 0x00400000
BRANCH_SELF = 0xEAFFFFFE  # b .
CMD_DUMP = "\x04"
CMD_PANIC = "\x10"

SCHEME_BG = {
    "green": (0x0A, 0x1A, 0x0E),
    "amber": (0x1A, 0x10, 0x06),
    "blue": (0x08, 0x10, 0x1E),
    "white": (0x12, 0x12, 0x14),
}


def rom_header(file_size, mem_size, name=b"", version=0, flags=rimg.FLAG_META):
    return struct.pack("<8sHHIIIIIIII16sI", b"ROM1\0\0\0\0", 1, 64, flags, ROM_BASE,
                       file_size, mem_size, 0, 1 << 16, version, 0, name, 0)


def make_rom(name="DEITRIX", version="3.0.3", title="Deitrix test ROM", payload_size=4096):
    """A sealed, valid ROM1 image."""
    size = 0x120 + payload_size
    data = bytearray(size)
    struct.pack_into("<I", data, 0, BRANCH_SELF)
    data[0x20:0x60] = rom_header(0, size + 0x10000)
    data[0x60:0x60 + len(title)] = title.encode("ascii")
    for i in range(payload_size):
        data[0x120 + i] = (i * 7 + 3) & 0xFF
    return rimg.seal(bytes(data), name=name, image_version=rimg.parse_version(version))


def make_big_rom():
    """A ROM with a correct CRC that does not fit the 4 MiB slot."""
    size = ROM_SIZE + 0x1000
    data = bytearray(size)
    struct.pack_into("<I", data, 0, BRANCH_SELF)
    data[0x20:0x60] = rom_header(size, size, b"BIGROM", rimg.parse_version("1.0.0"))
    struct.pack_into("<I", data, 0x3C, rimg.image_crc(data, size))
    return bytes(data)


def corrupt(data, offset=-1):
    out = bytearray(data)
    out[offset] ^= 0xFF
    return bytes(out)


def rom_crc(data):
    return struct.unpack_from("<I", data, 0x3C)[0]


def make_card(path, files, size_mib=16):
    """An SD card image with `files` ({"PIPBOY/ROM1.ROM": bytes, ...})."""
    make_sd_image(path, size_mib, files)
    return path


def config(scheme="green", delay=0, volume=100, beep="yes", extra=""):
    return (f"[display]\nscheme = {scheme}\nline_delay_ms = {delay}\n"
            f"[audio]\nvolume = {volume}\nbeep = {beep}\n{extra}").encode()


def pipos_markers(vm, ev=None):
    return [m for m in vm.markers()
            if m.get("stage") == "pipos" and (ev is None or m.get("ev") == ev)]


def one(vm, ev):
    found = pipos_markers(vm, ev)
    assert len(found) == 1, f"expected one ev={ev}, got {found}\n{vm.serial}"
    return found[0]


def config_summary(vm):
    found = [m for m in pipos_markers(vm, "config") if "scheme" in m]
    assert len(found) == 1, vm.serial
    return found[0]


def config_issues(vm):
    return [(m["rc"], m["key"]) for m in pipos_markers(vm, "config") if "key" in m]


def wait_markers(vm, ev, count=1, timeout=TIMEOUT, **match):
    """Waits until `count` pipos markers of `ev` with the given keys exist."""
    deadline = time.monotonic() + timeout
    while True:
        found = [m for m in pipos_markers(vm, ev)
                 if all(m.get(k) == v for k, v in match.items())]
        if len(found) >= count:
            return found
        assert time.monotonic() < deadline, f"wanted {count} ev={ev} {match}\n{vm.serial}"
        time.sleep(0.05)


def dump(vm):
    """Asks the kernel for a screen dump and returns the rows."""
    before = len(SCREEN.findall(vm.serial))
    vm.send(CMD_DUMP)
    deadline = time.monotonic() + TIMEOUT
    while True:
        found = SCREEN.findall(vm.serial)
        if len(found) > before:
            return [row[1:] for row in found[-1].splitlines()]
        assert time.monotonic() < deadline, f"no screen dump\n{vm.serial}"
        time.sleep(0.05)


def screenshot(vm, tmp_path):
    path = tmp_path / "screen.ppm"
    vm.screendump(path)
    with Image.open(path) as img:
        img = img.convert("RGB")
    assert img.size == (640, 480)
    return img


def no_exception(vm):
    assert not [m for m in vm.markers() if m.get("ev") in ("exception", "panic")], vm.serial


@contextlib.contextmanager
def booted(image, board, sd=None, **kwargs):
    """Boots Pip-OS and waits for ev=ready."""
    with boot(image, board, sd=sd, timeout=TIMEOUT, **kwargs) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        yield vm
