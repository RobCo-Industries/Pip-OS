"""Helpers for the Pip-OS target tests: ROM files, cards and boots.

Valid ROM files come from rimg.pack in RobCo-Core's tools/rimg.py. The
broken ones start from a valid ROM and change one field. The payload
never runs in these tests.
"""

import contextlib
import os
import re
import struct
import subprocess
import tempfile
import time

import rimg
from PIL import Image
from harness.boot import boot
from harness.sdimage import make_sd_image

TIMEOUT = 60
READY = re.compile(r"@RC stage=pipos ev=ready rom=\d\n")

ROM_BASE = 0x00400000
ROM_SIZE = 0x00400000

SCHEME_BG = {
    "green": (0x0A, 0x1A, 0x0E),
    "amber": (0x1A, 0x10, 0x06),
    "blue": (0x08, 0x10, 0x1E),
    "white": (0x12, 0x12, 0x14),
}


ICON = bytes((i * 37) & 0xFF for i in range(rimg.ICON_SIZE))


def make_rom(name="DEITRIX", version="3.0.3", title="Deitrix test ROM", payload_size=4096):
    """A sealed, valid ROM1 image from rimg.pack, with a title and an icon."""
    payload = bytes((i * 7 + 3) & 0xFF for i in range(payload_size))
    return rimg.pack(payload, "ROM1", 0, ROM_BASE, name=name,
                     image_version=rimg.parse_version(version),
                     mem_size=0x120 + payload_size + 0x10000, title=title, icon=ICON)


def reseal(data):
    """Rewrites the CRC of a broken image, so only the intended fault remains."""
    out = bytearray(data)
    size = struct.unpack_from("<I", out, 0x34)[0]
    struct.pack_into("<I", out, 0x3C, rimg.image_crc(out, size))
    return bytes(out)


def make_big_rom():
    """A ROM with a correct CRC that does not fit the 4 MiB slot."""
    size = ROM_SIZE + 0x1000
    data = bytearray(make_rom(name="BIGROM", version="1.0.0")).ljust(size, b"\0")
    struct.pack_into("<II", data, 0x34, size, size)
    return reseal(data)


def make_rom_without_meta():
    """A ROM1 image with a correct CRC whose header lacks the META flag."""
    data = bytearray(make_rom())
    struct.pack_into("<I", data, 0x2C, 0)
    return reseal(data)


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


SECTOR = 512
FAT16_LBA = 0x0E


def _mtools(*args):
    env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
    subprocess.run(list(args), check=True, env=env, capture_output=True)


def _fat_format(img, offset_lba, sectors, files):
    """Formats FAT16 at `offset_lba` of `img` and copies `files` into it."""
    target = f"{os.fspath(img)}@@{offset_lba * SECTOR}"
    _mtools("mformat", "-i", target, "-c", "1", "-v", "PIPOS", "-T", str(sectors),
            "-h", "64", "-s", "32", "-H", str(offset_lba), "::")
    made = set()
    with tempfile.TemporaryDirectory(prefix="pipcard") as tmp:
        for i, (name, data) in enumerate(sorted(files.items())):
            parts = [p for p in name.split("/") if p]
            dirs = parts if name.endswith("/") else parts[:-1]
            for j in range(1, len(dirs) + 1):
                d = "/".join(dirs[:j])
                if d not in made:
                    _mtools("mmd", "-i", target, "::" + d)
                    made.add(d)
            if not name.endswith("/"):
                src = os.path.join(tmp, f"f{i}")
                with open(src, "wb") as f:
                    f.write(data)
                _mtools("mcopy", "-i", target, src, "::" + "/".join(parts))


def make_multi_card(path, parts, size_mib=16):
    """A card with one MBR entry per item of `parts`, in slot order.

    Each item is (type byte, size in MiB, files). Files None leaves the
    partition zero-filled; a dict formats it as FAT16 and copies the files.
    Partitions start at LBA 2048 and follow each other.
    """
    size = size_mib << 20
    entries = b""
    layout = []
    lba = 2048
    for ptype, mib, files in parts:
        sectors = (mib << 20) // SECTOR
        entries += struct.pack("<B3sB3sII", 0, b"\xfe\xff\xff", ptype, b"\xfe\xff\xff",
                               lba, sectors)
        layout.append((lba, sectors, files))
        lba += sectors
    assert lba * SECTOR <= size and len(parts) <= 4
    with open(path, "wb") as f:
        f.write(bytes(446) + entries.ljust(64, b"\0") + b"\x55\xaa")
        f.truncate(size)
    for start, sectors, files in layout:
        if files is not None:
            _fat_format(path, start, sectors, files)
    return path


def make_superfloppy_card(path, files, size_mib=16):
    """A card with no partition table: a FAT16 volume starts at block 0."""
    with open(path, "wb") as f:
        f.truncate(size_mib << 20)
    _fat_format(path, 0, (size_mib << 20) // SECTOR, files)
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


def dump_reply(vm):
    """Runs the dump command on the script channel and returns its lines."""
    reply = vm.script("dump", timeout=TIMEOUT)
    assert reply.status == "ok", reply
    return reply.lines


def dump(vm):
    """The screen rows from the dump command."""
    return [line[1:] for line in dump_reply(vm) if line.startswith("|")]


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
def booted(image, board, sd=None, serial2=True, **kwargs):
    """Boots Pip-OS with the script channel and waits for ev=ready."""
    with boot(image, board, sd=sd, serial2=serial2, timeout=TIMEOUT, **kwargs) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        yield vm
