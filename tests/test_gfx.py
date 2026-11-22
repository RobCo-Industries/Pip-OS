"""The graphics calls on the target: results and error codes, screen
goldens, and who owns the screen before, during and after a ROM.

Set PIPOS_UPDATE_GOLDEN=1 to rewrite tests/golden/gfx_<scene>.png from
the current run instead of comparing against it. The surface is the same
on every board, so one golden per scene covers all of them.
"""

import os

import pytest
from PIL import Image

from harness.boot import boot

from pipos_helpers import (SCHEME_BG, TIMEOUT, assert_rom_checks, booted, dump, make_card,
                           no_exception, one, pipos_markers, rom_one, screenshot)

ROM = "PIPBOY/ROM1.ROM"
GOLDEN_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "golden")
RED = (0xFF, 0x00, 0x00)
# The green scheme: background, dim, normal, bright.
GREEN = {(0x0A, 0x1A, 0x0E), (0x1F, 0x8A, 0x3D), (0x33, 0xE0, 0x66), (0xB0, 0xFF, 0xC8)}

CALL_CHECKS = [
    "info_rc", "info_w", "info_h", "info_format", "info_backend", "info_fg", "info_kernel",
    "info_straddle", "info_intact",
    "line", "line_extreme", "point", "point_far", "clear", "color", "font0", "font1",
    "text_w", "text_scale8", "text_empty", "text_scale0", "text_scale9", "text_255",
    "text_256", "text_kernel", "text_bb", "text_slot_end", "text_far",
    "tm_ok", "tm_empty", "tm_255", "tm_scale9", "tm_256", "tm_kernel",
    "rect_outline", "rect_fill", "rect_zero", "rect_fill2", "rect_far",
    "poly_2", "poly_256", "poly_1", "poly_257", "poly_closed2", "poly_kernel", "poly_edge",
    "poly_straddle",
    "arc_ok", "arc_edges", "arc_r0", "arc_r513", "arc_start360", "arc_end361", "arc_fill2",
    "arc_kernel", "arc_straddle",
    "b1_ok", "b1_w0", "b1_w321", "b1_h241", "b1_stride", "b1_flags", "b1_edge", "b1_past",
    "b1_data_kernel", "b1_desc_kernel",
    "rgb_full", "rgb_stride", "rgb_edge", "rgb_past", "rgb_data_bb",
    "clip_set", "clip_w0", "clip_h0", "clip_far", "clip_reset",
    "present",
]


def card(tmp_path, data):
    return make_card(tmp_path / "sd.img", {ROM: data})


def colours(img):
    return {c for _, c in img.getcolors(maxcolors=1 << 20)}


def test_back_buffer_is_kernel_only(board, kernel_image):
    with booted(kernel_image, board) as vm:
        m = one(vm, "gfx")
        assert (m["bb"], m["size"], m["user"]) == ("0x00c00000", str(320 * 240 * 4), "0"), m
        assert int(m["heap_end"], 16) <= 0x00C00000, m


def test_calls(board, kernel_image, test_rom, tmp_path, need_mtools):
    with booted(kernel_image, board, sd=card(tmp_path, test_rom("gfx_calls"))) as vm:
        assert_rom_checks(vm, CALL_CHECKS)
        p = rom_one(vm, "present")
        ms = int(p["ms"])
        print(f"\n{board}: {p['count']} presents in {ms} ms")
        assert ms >= 0, vm.serial
        assert one(vm, "rom_exit")["status"] == "0"
        no_exception(vm)


@pytest.mark.parametrize("scene", ["prims", "text", "blits"])
def test_scene_golden(board, kernel_image, test_rom, tmp_path, need_mtools, scene):
    sd = card(tmp_path, test_rom(f"gfx_{scene}"))
    with boot(kernel_image, board, sd=sd, serial2=True, timeout=TIMEOUT) as vm:
        vm.wait_for("ev=drawn", timeout=TIMEOUT, line=True)
        img = screenshot(vm, tmp_path)
        no_exception(vm)
    # Scaled by 2: every 2x2 block is one colour.
    small = img.resize((320, 240), Image.NEAREST)
    assert small.resize((640, 480), Image.NEAREST).tobytes() == img.tobytes()
    golden = os.path.join(GOLDEN_DIR, f"gfx_{scene}.png")
    if os.environ.get("PIPOS_UPDATE_GOLDEN"):
        img.save(golden)
    with Image.open(golden) as g:
        assert g.convert("RGB").tobytes() == img.tobytes(), f"screen differs from {golden}"


def test_no_present_leaves_display(board, kernel_image, test_rom, tmp_path, need_mtools):
    sd = card(tmp_path, test_rom("gfx_nopresent"))
    with boot(kernel_image, board, sd=sd, serial2=True, timeout=TIMEOUT) as vm:
        vm.wait_for("ev=drawn", timeout=TIMEOUT, line=True)
        img = screenshot(vm, tmp_path)
        rows = dump(vm)
    assert colours(img) <= GREEN, colours(img)
    assert img.getpixel((639, 479)) == SCHEME_BG["green"]
    assert "PIP-OS READY" in rows, rows


@pytest.mark.parametrize("rom", ["gfx_exit", "gfx_fault"])
def test_kernel_takes_screen_back(board, kernel_image, test_rom, tmp_path, need_mtools, rom):
    with booted(kernel_image, board, sd=card(tmp_path, test_rom(rom))) as vm:
        img = screenshot(vm, tmp_path)
        rows = dump(vm)
        if rom == "gfx_exit":
            assert one(vm, "rom_exit")["status"] == "3", vm.serial
        else:
            assert one(vm, "rom_kill")["reason"] == "data_abort", vm.serial
            assert any(r.startswith("ROM(1) ") and " AT 0x" in r for r in rows), rows
        assert not pipos_markers(vm, "exception")
    assert RED not in colours(img)
    assert colours(img) <= GREEN, colours(img)
    assert img.getpixel((639, 479)) == SCHEME_BG["green"]
    assert "PIP-OS READY" in rows, rows
