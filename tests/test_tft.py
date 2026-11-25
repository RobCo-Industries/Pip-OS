"""The SPI panel backend in QEMU.

QEMU models the SPI0 controller but no panel: writes go out, every byte
read is 0, so tft_read_id() reports no panel. The test build
(-DPIPOS_TFT_TEST) taps every byte sent with its DC level. It prints one
ev=tft_send marker per send with the rectangle, the byte count and a
CRC-32 of the stream, and answers the tftdump script command with the
whole transcript since tft_init(). build/host/panel_model feeds it to
RobCo-Core's ILI9341 model, and the tests compare the panel picture with
the HDMI screenshot of the same frame.

What this cannot check: a real panel, its timing, orientation and colour
order. Nothing here ran on hardware.
"""

import os
import subprocess
import time
import zlib

import pytest
from PIL import Image

from harness.boot import boot
from harness.env import minimal_env

from hosttiming import judge_stream, judged, judged_split, park_args, retry_evidence

from pipos_helpers import (TIMEOUT, assert_rom_checks, booted, config, config_issues, dump,
                           make_card, no_exception, one, pipos_markers, press, rom_markers,
                           screenshot, script, wait_markers)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROM = "PIPBOY/ROM1.ROM"
CFG = "PIPBOY/PIPOS.CFG"
TAPE = "PIPBOY/HOLO/TAPE.HTP"
W, H = 320, 240
FULL_BYTES = W * H * 2
RATE = 8000
# tests/roms/tft_stream.c
CHUNKS = 3
CHUNK_LEN = 64000


@pytest.fixture(scope="session")
def tft_build(request, robco_build, kernel_image):
    """The test build of the kernel and the panel model, next to --build-dir."""
    build = os.path.join(ROOT, request.config.getoption("--build-dir"))
    image = robco_build(ROOT, target="kernel",
                        flags=(f"BUILD={build}/tft", "EXTRA_CFLAGS=-DPIPOS_TFT_TEST"),
                        image=os.path.join(build, "tft", "pipos.img"))
    subprocess.run(["make", "-s", "CI=1", f"BUILD={build}", "panel-model"], cwd=ROOT,
                   check=True, capture_output=True, env=minimal_env())
    return image, os.path.join(build, "host", "panel_model")


def display_cfg(extra):
    return config(beep="no", extra="[display]\n" + extra + "\n")


def card(tmp_path, cfg, files=None):
    return make_card(tmp_path / "sd.img", {CFG: cfg, **(files or {})})


def sends(vm):
    return pipos_markers(vm, "tft_send")


def panel_view(vm, model, rotation=90):
    """The panel picture rebuilt from the transcript: rows of RGB565 values.

    320x240 at 90 and 270 degrees, 240x320 at 0 and 180.
    """
    reply = script(vm, "tftdump")
    assert reply.lines[-1].startswith("end ") and reply.lines[-1].endswith(" full=0"), reply.lines[-1]
    out = subprocess.run([model, str(rotation)], input=("\n".join(reply.lines) + "\n").encode(),
                         capture_output=True, check=True, env=minimal_env())
    head, _, data = out.stdout.partition(b"\n")
    w, h = map(int, head.split())
    assert (w, h) == ((W, H) if rotation in (90, 270) else (H, W))
    return [[data[2 * (y * w + x)] | data[2 * (y * w + x) + 1] << 8 for x in range(w)]
            for y in range(h)]


def rgb565(r, g, b):
    return (r >> 3) << 11 | (g >> 2) << 5 | b >> 3


def assert_panel_is_hdmi(vm, model, tmp_path):
    """Pixel for pixel: the panel shows the HDMI frame, scaled down by 2, in RGB565."""
    img = screenshot(vm, tmp_path)
    small = img.resize((W, H), Image.NEAREST)
    assert small.resize((640, 480), Image.NEAREST).tobytes() == img.tobytes()
    view = panel_view(vm, model)
    px = small.load()
    bad = [(x, y) for y in range(H) for x in range(W) if view[y][x] != rgb565(*px[x, y])]
    assert not bad, f"{len(bad)} pixels differ, first {bad[:5]}"
    return small


def wait_until(cond, vm):
    t = time.monotonic()
    while not cond():
        assert time.monotonic() - t < TIMEOUT, vm.serial
        time.sleep(0.05)


def wait_rom(vm, ev, count):
    wait_until(lambda: len(rom_markers(vm, ev)) >= count, vm)


def ordered(vm):
    """(kind, marker) of the ROM's step and shown lines and the kernel's sends, in order."""
    out = []
    for m in vm.markers():
        if m.get("stage") == "testrom" and m.get("ev") in ("step", "shown"):
            out.append((m["ev"], m))
        elif m.get("stage") == "pipos" and m.get("ev") == "tft_send":
            out.append(("send", m))
    return out


def sends_of_step(vm, n):
    """The sends from ev=step n=<n> to its ev=shown."""
    seq = ordered(vm)
    i = next(k for k, (kind, m) in enumerate(seq) if kind == "step" and m["n"] == str(n))
    j = next(k for k in range(i, len(seq)) if seq[k][0] == "shown")
    return [m for kind, m in seq[i + 1:j] if kind == "send"]


def rect(m):
    return tuple(int(m[k]) for k in ("x", "y", "w", "h"))


def judged_sends(strict, timed):
    """Judges a panel test whose exact sends a slow host can break.

    On raspi3b under heavy host load the library's 20 ms SPI deadline can
    expire in QEMU, and the kernel retries the present once. A retry changes
    only the sends. strict() boots, runs every other check and returns the
    VM. timed(vm) checks the exact sends. Only a timed failure in a run where
    the kernel marked a retry cannot judge.
    """
    judged_split(strict, timed, lambda vm: retry_evidence([vm]))


def test_shipped_image_has_no_hook(board, kernel_image, tft_build, tmp_path, need_mtools):
    with open(kernel_image, "rb") as f:
        shipped = f.read()
    with open(tft_build[0], "rb") as f:
        test = f.read()
    for word in (b"tft_send", b"end bytes="):
        assert word not in shipped and word in test, word
    sd = card(tmp_path, display_cfg("backend = tft"))
    with booted(kernel_image, board, sd=sd) as vm:
        reply = vm.script("tftdump", timeout=TIMEOUT)
        assert reply.status.startswith("err"), reply
        assert not sends(vm), vm.serial
        no_exception(vm)


def test_tft_with_nothing_attached(board, kernel_image, tmp_path, need_mtools):
    """The shipped image: no panel answers, so HDMI keeps the frames too."""
    sd = card(tmp_path, display_cfg("backend = tft"))
    with booted(kernel_image, board, sd=sd) as vm:
        m = one(vm, "display")
        assert (m["backend"], m["tft"], m["rc"], m["asked"], m["bad"]) == \
            ("both", "absent", "0", "tft", "0"), m
        assert 0 < int(m["hz"]) <= 10000000, m
        assert any(row.strip() for row in dump(vm))
        no_exception(vm)


def test_default_prints_no_display_marker(board, kernel_image, tmp_path, need_mtools):
    with booted(kernel_image, board, sd=card(tmp_path, config())) as vm:
        assert not pipos_markers(vm, "display"), vm.serial


def test_bad_pin_is_refused(board, tft_build, tmp_path, need_mtools):
    sd = card(tmp_path, display_cfg("backend = both\ntft_dc = 24\ntft_backlight = 18\n"
                                     "tft_reset = 9"))
    with booted(tft_build[0], board, sd=sd) as vm:
        assert config_issues(vm) == [("pin_input", "display.tft_dc"),
                                     ("pin_audio", "display.tft_backlight"),
                                     ("pin_bus", "display.tft_reset")], vm.serial
        m = one(vm, "display")
        assert (m["backend"], m["bad"]) == ("both", "3"), m
        no_exception(vm)


def test_init_failure_falls_back_to_hdmi(board, tft_build, test_rom, test_holo, tmp_path,
                                         need_mtools):
    sd = card(tmp_path, display_cfg("backend = tft\ntft_test = fail"),
              {ROM: test_rom("tft_frames"), TAPE: test_holo("tape")})
    with boot(tft_build[0], board, sd=sd, serial2=True, timeout=TIMEOUT) as vm:
        vm.wait_for("@RC stage=testrom ev=shown", timeout=TIMEOUT)
        m = one(vm, "display")
        assert (m["backend"], m["tft"], m["rc"], m["hz"]) == ("hdmi", "err", "-1", "0"), m
        assert "\nSPI DISPLAY FAILED, USING HDMI\n" in vm.serial, vm.serial
        # The ROM's frame reaches HDMI: its background colour fills the screen.
        img = screenshot(vm, tmp_path)
        assert img.getpixel((4, 4)) == (0x20, 0x30, 0x40), img.getpixel((4, 4))
        assert not sends(vm), vm.serial
        no_exception(vm)


def test_both_panel_matches_hdmi(board, tft_build, test_rom, test_holo, tmp_path, need_mtools):
    def strict():
        image, model = tft_build
        sd = card(tmp_path, display_cfg("backend = both"),
                  {ROM: test_rom("tft_frames"), TAPE: test_holo("tape")})
        with boot(image, board, sd=sd, serial2=True, timeout=TIMEOUT,
                  extra_args=park_args(board)) as vm:
            wait_rom(vm, "shown", 1)
            disp = one(vm, "display")
            assert (disp["backend"], disp["tft"]) == ("both", "absent"), disp
            assert_panel_is_hdmi(vm, model, tmp_path)
            for n in range(1, 5):
                press(vm, "select")
                wait_rom(vm, "shown", n + 1)
                if n == 2:
                    assert_panel_is_hdmi(vm, model, tmp_path)
            press(vm, "select")
            vm.wait_for("@RC stage=pipos ev=rom_exit", timeout=TIMEOUT)
            assert_rom_checks(vm, ["holo", "off", "on"])
            no_exception(vm)
        return vm

    def timed(vm):
        first = sends(vm)[0]
        assert first["why"] == "full" and rect(first) == (0, 0, W, H), first
        unchanged = sends_of_step(vm, 1)
        assert [(rect(m), m["bytes"]) for m in unchanged] == [((0, 0, 0, 0), "0")], unchanged
        small = sends_of_step(vm, 2)
        assert [rect(m) for m in small] == [(101, 57, 3, 2)], small
        # CASET, PASET and RAMWR with their parameters, then 2 bytes per pixel.
        assert int(small[0]["bytes"]) == 11 + 3 * 2 * 2, small
        holo = sends_of_step(vm, 3)
        assert [(m["why"], rect(m)) for m in holo] == [("full", (0, 0, W, H))], holo
        # The wake itself sends the whole frame, so the present after it sends nothing.
        seq = ordered(vm)
        shown3 = [k for k, (kind, m) in enumerate(seq) if kind == "shown"][3]
        step4 = next(k for k, (kind, m) in enumerate(seq) if kind == "step" and m["n"] == "4")
        woke = [m for kind, m in seq[shown3 + 1:step4] if kind == "send"]
        assert [(m["why"], rect(m)) for m in woke] == [("full", (0, 0, W, H))], woke
        assert int(woke[0]["bytes"]) >= FULL_BYTES, woke
        assert [rect(m) for m in sends_of_step(vm, 4)] == [(0, 0, 0, 0)]

    judged_sends(strict, timed)


def test_tft_only_shows_boot_text_and_panic_on_panel(board, tft_build, tmp_path, need_mtools):
    image, model = tft_build
    sd = card(tmp_path, display_cfg("backend = tft\ntft_test = panel"))
    with booted(image, board, sd=sd) as vm:
        disp = one(vm, "display")
        assert (disp["backend"], disp["tft"]) == ("tft", "ok"), disp
        view = panel_view(vm, model)
        # The green scheme: background 0x0A1A0E, text 0x33E066.
        bg, fg = rgb565(0x0A, 0x1A, 0x0E), rgb565(0x33, 0xE0, 0x66)
        lines = [y // 16 for y in range(H) if fg in view[y]]
        assert len(set(lines)) >= 5, sorted(set(lines))
        assert {v for row in view for v in row} <= {bg, fg}
        # HDMI keeps the boot console.
        assert any(row.strip() for row in dump(vm))
        before = len(sends(vm))
        vm.send("panic PANEL\n", port=1)
        vm.wait_for("ev=panic msg=PANEL", timeout=TIMEOUT)
        wait_until(lambda: len(sends(vm)) > before, vm)
        # The panic lines change the bottom of the panel console.
        assert int(sends(vm)[-1]["h"]) > 0, vm.serial


def test_stream_while_redrawing(board, tft_build, test_rom, tmp_path, need_mtools):
    """Full-frame presents do not starve PCM streaming.

    PCM plays from a timer interrupt, which the polled SPI transfer leaves
    on. The ROM queues the next chunk between presents, so a chunk must
    outlast one present. The ROM reports the longest time between its queue
    calls: when that reaches a chunk's length, the host stalled QEMU and
    the run cannot judge the claim. See tests/hosttiming.py.
    """
    image, _ = tft_build
    crcs = [f"{zlib.crc32(bytes((k * 7 + i * 31) & 0xFF for k in range(CHUNK_LEN))):08x}"
            for i in range(CHUNKS)]

    def attempt():
        sd = card(tmp_path, display_cfg("backend = both"), {ROM: test_rom("tft_stream")})
        with booted(image, board, sd=sd, extra_args=park_args(board)) as vm:
            assert_rom_checks(vm, ["start", "queue"])
            stream = rom_markers(vm, "stream")[0]
            frames = int(stream["frames"])
            tr = wait_markers(vm, "audio", count=CHUNKS + 1)
            full = [m for m in sends(vm) if int(m["bytes"]) >= FULL_BYTES]
            no_exception(vm)
        print(f"\n{board}: {frames} frames, {len(full)} full sends while streaming, "
              f"longest present {stream['max_ms']} ms, longest queue gap "
              f"{stream['max_gap_ms']} ms")
        judge_stream(stream, tr, CHUNKS, CHUNK_LEN, RATE, crcs)
        assert frames >= CHUNKS and len(full) >= CHUNKS, (frames, len(full))

    judged(attempt)


def test_rotation_0_sends_only_the_visible_window(board, tft_build, test_rom, tmp_path,
                                                  need_mtools):
    """At 0 degrees the panel shows the left 240x240: changes right of it are never sent."""
    def strict():
        image, model = tft_build
        sd = card(tmp_path, display_cfg("backend = both\ntft_rotation = 0"),
                  {ROM: test_rom("tft_window")})
        with boot(image, board, sd=sd, serial2=True, timeout=TIMEOUT,
                  extra_args=park_args(board)) as vm:
            wait_rom(vm, "shown", 1)
            for n in range(1, 3):
                press(vm, "select")
                wait_rom(vm, "shown", n + 1)
            img = screenshot(vm, tmp_path)
            view = panel_view(vm, model, rotation=0)
            no_exception(vm)
        small = img.resize((W, H), Image.NEAREST)
        assert small.resize((640, 480), Image.NEAREST).tobytes() == img.tobytes()
        px = small.load()
        assert px[10, 20] == (0xFF, 0x00, 0xFF) and px[300, 200] == (0x00, 0xFF, 0x00)
        bad = [(x, y) for y in range(H) for x in range(H) if view[y][x] != rgb565(*px[x, y])]
        assert not bad, f"{len(bad)} pixels differ, first {bad[:5]}"
        return vm

    def timed(vm):
        first = sends_of_step(vm, 0)
        # The ROM's scene covers the whole window, and no more.
        assert [rect(m) for m in first] == [(0, 0, H, H)], first
        hidden = sends_of_step(vm, 1)
        assert [(rect(m), m["bytes"]) for m in hidden] == [((0, 0, 0, 0), "0")], hidden
        pixel = sends_of_step(vm, 2)
        assert [(m["why"], rect(m)) for m in pixel] == [("diff", (10, 20, 1, 1))], pixel
        # CASET, PASET and RAMWR with their parameters, then one pixel.
        assert int(pixel[0]["bytes"]) == 11 + 2, pixel

    judged_sends(strict, timed)


def test_panic_wakes_a_blanked_panel(board, tft_build, test_rom, tmp_path, need_mtools):
    """A panic after power blanked the screen wakes the panel, then sends the whole console."""
    def strict():
        image, _ = tft_build
        sd = card(tmp_path, display_cfg("backend = both"), {ROM: test_rom("tft_window")})
        with boot(image, board, sd=sd, serial2=True, timeout=TIMEOUT,
                  extra_args=park_args(board)) as vm:
            wait_rom(vm, "shown", 1)
            for n in range(1, 3):
                press(vm, "select")
                wait_rom(vm, "shown", n + 1)
            press(vm, "select")
            wait_rom(vm, "dark", 1)
            assert_rom_checks(vm, ["off"])
            assert screenshot(vm, tmp_path).getcolors() == [(640 * 480, (0, 0, 0))]
            vm.send("panic DARK\n", port=1)
            vm.wait_for("ev=panic msg=DARK", timeout=TIMEOUT)
            wait_until(lambda: any(m["why"] == "full" for m in after_panic(vm)), vm)
            # HDMI: the panic text lands on the black screen.
            img = screenshot(vm, tmp_path)
            assert len(img.getcolors(640 * 480)) > 1, img.getcolors(640 * 480)
        return vm

    def timed(vm):
        seq = after_panic(vm)
        assert [m["why"] for m in seq] == ["wake", "full"], seq
        # SLPOUT, then DISPON: one command byte each, DC low.
        want = zlib.crc32(bytes([0, 0x11, 0, 0x29]))
        assert (rect(seq[0]), seq[0]["bytes"], seq[0]["crc"]) == \
            ((0, 0, 0, 0), "2", f"{want:08x}"), seq[0]
        assert rect(seq[1]) == (0, 0, W, H) and int(seq[1]["bytes"]) >= FULL_BYTES, seq[1]

    judged_sends(strict, timed)


def after_panic(vm):
    """The sends after the panic marker."""
    out, seen = [], False
    for m in vm.markers():
        if m.get("stage") == "pipos" and m.get("ev") == "panic":
            seen = True
        elif seen and m.get("stage") == "pipos" and m.get("ev") == "tft_send":
            out.append(m)
    return out
