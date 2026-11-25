"""The conformance ROM (conformance/) on the card from `make conformance-card`.

One run answers every prompt through the script channel and turns the
results pages. QEMU plays no sound a person hears, so the "heard" answers
are scripted: they prove the prompts work, not the sound. A second run
uses auto mode and must end by itself.
"""

import os
import shutil
import subprocess
import time

import pytest
from harness.boot import boot

from conftest import ROOT
from pipos_helpers import (TIMEOUT, _mtools, abi_syscalls, booted, no_exception, one,
                           pipos_markers, screenshot, wait_markers)
from test_card import check_golden
from test_time import RTC_DEVICE

HW_RTC = 1 << 0     # sys_info hw_flags bits, ABI 1.3
HW_AUDIO = 1 << 2

PART_OFFSET = 2048 * 512

# (press check, short name) of each button prompt, in order.
BUTTONS = [("btn_up", "btn_up"), ("btn_down", "btn_down"), ("btn_left", "btn_left"),
           ("btn_right", "btn_right"), ("btn_select", "btn_select"), ("btn_dial_press", "btn_dial"),
           ("btn_back", "btn_back")]
# The script name of each button.
KEYS = {"btn_up": "up", "btn_down": "down", "btn_left": "left", "btn_right": "right",
        "btn_select": "select", "btn_dial_press": "dial_push", "btn_back": "back"}

# (call, [check names]) in the order the ROM runs them. "none" holds the
# checks of unknown numbers.
CHECKS = [
    ("draw_line", ["line_drawn", "line_offscreen"]),
    ("draw_point", ["point_drawn", "point_offscreen"]),
    ("draw_text", ["text_width", "text_scale9", "text_scale_first", "text_bad_ptr"]),
    ("clear", ["clear_ok"]),
    ("gfx_info", ["gfx_info_rc", "gfx_info_fields", "gfx_info_bad_ptr"]),
    ("rect", ["rect_filled", "rect_outline", "rect_zero_size", "rect_bad_fill"]),
    ("polyline", ["poly_closed", "poly_count1", "poly_count257", "poly_bad_closed", "poly_bad_ptr"]),
    ("arc", ["arc_circle", "arc_radius0", "arc_start360", "arc_null"]),
    ("blit_1bpp", ["blit1_drawn", "blit1_short_stride", "blit1_bad_data"]),
    ("blit_rgb", ["blitrgb_drawn", "blitrgb_bad_flags", "blitrgb_straddle"]),
    ("set_clip", ["clip_set", "clip_zero_width", "clip_reset"]),
    ("set_color", ["color_set"]),
    ("text_metrics", ["metrics_value", "metrics_scale0", "metrics_bad_ptr"]),
    ("present", ["present_ok"]),
    ("set_font", ["font_system", "font_other"]),
    ("read_buttons", ["buttons_mask"]),
    ("read_dial", ["dial_range"]),
    ("poll_event", ["poll_empty", "poll_bad_ptr"]),
    ("wait_event", ["wait_zero", "wait_100_rc", "wait_100_time", "wait_range"]),
    ("tone", ["tone_ok", "tone_playing", "tone_freq19", "tone_ms0", "tone_refused_keeps"]),
    ("sample", ["sample_ok", "sample_playing", "sample_len0", "sample_len_first", "sample_bad_ptr"]),
    ("audio_stop", ["stop_ok", "stop_silent", "stop_idle"]),
    ("volume", ["volume_prev", "volume_reads_back", "volume_101", "volume0_status"]),
    ("audio_status", ["status_idle", "status_ends"]),
    ("play_seq", ["seq_ok", "seq_playing", "seq_count0", "seq_bad_note", "seq_bad_plays_none",
                 "seq_null"]),
    ("sample_queue", ["squeue_starts", "squeue_playing", "squeue_chained", "squeue_busy",
                      "squeue_len0", "squeue_len65537", "squeue_bad_ptr", "squeue_still_busy",
                      "squeue_stop", "squeue_stop_silent", "squeue_restarts",
                      "squeue_restart_chains", "squeue_restart_busy"]),
    ("read_sensor", ["sensor_rtc", "sensor_battery", "sensor_geiger", "sensor_gps", "sensor_accel",
                    "sensor_temp", "sensor_heart", "sensor_7", "sensor_bad_ptr"]),
    ("sensor_available", ["available_rtc", "available_7"]),
    ("get_time", ["time_at_boot", "time_bad_ptr"]),
    ("get_battery", ["battery"]),
    ("ticks_ms", ["ticks_range", "ticks_advance"]),
    ("sleep_ms", ["sleep_rc", "sleep_50", "sleep_range"]),
    ("exit", ["exit_low8", "exit_zero"]),
    ("sys_info", ["sysinfo_rc", "sysinfo_fields", "sysinfo_bad_ptr", "sysinfo_hw_rtc",
                  "sysinfo_hw_audio", "sysinfo_hw_reserved"]),
    ("mem_info", ["meminfo_rom", "meminfo_straddle"]),
    ("power", ["power_screen_off", "power_screen_on", "power_bad_op"]),
    ("log", ["log_ok", "log_no_nul", "log_bad_ptr"]),
    ("rand", ["rand_range", "rand_varies"]),
    ("set_time", ["set_time_ok", "set_time_reads", "set_time_value"]),
    ("read_save", ["save_read", "save_read_same", "save_read_missing", "save_read_straddle"]),
    ("write_save", ["save_write", "save_too_big", "save_bad_name", "save_name_no_nul"]),
    ("save_info", ["save_info_rc", "save_info_size", "save_info_missing"]),
    ("save_delete", ["save_delete", "save_gone", "save_delete_missing"]),
    ("file_open", ["open_handles", "open_ninth", "open_lower_case", "open_missing", "open_relative",
                  "open_save_dir", "open_folder"]),
    ("file_read", ["read_whole", "read_magic", "read_eof", "read_size0", "read_bad_buf", "read_closed"]),
    ("file_close", ["close_ok", "close_twice", "close_bad"]),
    ("dir_list", ["dir_tape", "dir_root", "dir_past_end", "dir_max0", "dir_file", "dir_bad_out"]),
    ("holo_enumerate", ["enum_tape", "enum_data_tape", "enum_flags", "enum_max0", "enum_max33",
                        "enum_null"]),
    ("holo_launch", ["launch_tape", "launch_lower_case", "launch_fault", "launch_after_fault",
                    "launch_data_tape", "launch_missing", "launch_bad_name", "launch_null_name",
                    "launch_args_no_nul", "launch_args_99"]),
    ("holo_last_status", ["last_before_launch", "last_status", "last_error"]),
    ("holo_eject_event", ["eject_mask", "eject_ev_tape"]),
    ("none", ["unknown_number", "unknown_gap"]),
    *[part for btn, short in BUTTONS
      for part in (("wait_event", [btn]), ("read_buttons", [f"{short}_state", f"{short}_clear"]))],
    ("read_dial", ["dial_clockwise", "dial_cw_position", "dial_counter", "dial_ccw_position"]),
    ("tone", ["heard_tone"]),
    ("play_seq", ["heard_tune"]),
    ("sample", ["heard_sample"]),
    ("sample_queue", ["heard_chain"]),
    ("present", ["picture_right"]),
]
NAMES = [n for _, names in CHECKS for n in names]
INTERACTIVE = NAMES[NAMES.index("btn_up"):]

# No sensor, battery or clock chip in QEMU. The clock is unset at boot.
ABSENT = {"sensor_rtc", "sensor_battery", "sensor_geiger", "sensor_gps", "sensor_accel",
          "sensor_temp", "sensor_heart", "time_at_boot", "battery"}

# The answer the script gives to each prompt. SELECT is yes. A button is
# held until its _state check shows: a key goes down, then up, as two lines.
# UP is tapped first: the ROM asks again. UP replays the sample once; the
# answer waits until the trace shows the second start, as a stop before
# then would cancel it.
ANSWERS = {
    **{btn: [f"key {KEYS[btn]} 1", ("wait", f"{short}_state"), f"key {KEYS[btn]} 0"]
       for btn, short in BUTTONS},
    "btn_up": ["key up", ("retry", "btn_up"), "key up 1", ("wait", "btn_up_state"), "key up 0"],
    # A turn the wrong way first: the ROM ignores it but read_dial counts it.
    "dial_clockwise": ["dial -1", "dial 2"], "dial_counter": ["dial -3"],
    "heard_tone": ["key select"], "heard_tune": ["key select"],
    "heard_sample": ["key up", ("pcm", 2), "key select"],
    "heard_chain": ["key select"],
    "picture_right": ["key select"],
}


@pytest.fixture(scope="session")
def conformance_card(request, kernel_image):
    build = os.path.join(ROOT, request.config.getoption("--build-dir"))
    subprocess.run(["make", "-s", "-C", ROOT, "CI=1", f"BUILD={build}", "conformance-card"],
                   check=True, stdout=subprocess.DEVNULL)
    return os.path.join(build, "conformance.img")


def card_copy(card, tmp_path, auto=False):
    sd = tmp_path / "sd.img"
    shutil.copyfile(card, sd)
    if auto:
        cfg = tmp_path / "CONFORM.CFG"
        cfg.write_bytes(b"auto\n")
        _mtools("mcopy", "-i", f"{sd}@@{PART_OFFSET}", str(cfg), "::PIPBOY/CONFORM.CFG")
    return sd


def conform(vm, ev=None):
    return [m for m in vm.markers()
            if m.get("stage") == "conform" and (ev is None or m.get("ev") == ev)]


def steps(vm):
    return [m for m in conform(vm) if m["ev"] in ("intro", "prompt", "page", "summary")]


# An answered step shows the next one within this. A lost answer waits for
# the ROM's own timeout instead: 30 s for a prompt, 10 minutes for a page.
STEP_S = 10


def next_step(vm, done, timeout=TIMEOUT):
    """Waits for the prompt, page or summary marker number done + 1."""
    deadline = time.monotonic() + timeout
    while True:
        found = steps(vm)
        if len(found) > done:
            return found[done]
        assert time.monotonic() < deadline, f"no step {done + 1} within {timeout} s\n{vm.serial}"
        time.sleep(0.005)


def wait_conform(vm, ev, name, timeout=STEP_S):
    """Waits for the ev marker of name."""
    deadline = time.monotonic() + timeout
    while not [m for m in conform(vm, ev) if m["name"] == name]:
        assert time.monotonic() < deadline, f"no {ev} {name} within {timeout} s\n{vm.serial}"
        time.sleep(0.005)


def wait_conform_ev(vm, ev, timeout=STEP_S):
    """Waits for an ev marker."""
    deadline = time.monotonic() + timeout
    while not conform(vm, ev):
        assert time.monotonic() < deadline, f"no {ev} within {timeout} s\n{vm.serial}"
        time.sleep(0.005)


def finish_pages(vm, done):
    """From step done + 1, the first page: turns every page with DOWN, then ends on the summary."""
    while True:
        step = next_step(vm, done)
        done += 1
        key = "key down" if step["ev"] == "page" else "key select"
        reply = vm.script(key, timeout=TIMEOUT)
        assert reply.status == "ok", reply
        if step["ev"] == "summary":
            return


def wait_check(vm, name):
    wait_conform(vm, "check", name)


def results(vm):
    return [(m["call"], m["name"], m["result"]) for m in conform(vm, "check")]


def hw_flags(vm):
    """sys_info's hw_flags, from the ROM's "conform: board_rev" line."""
    lines = [ln for ln in vm.serial.splitlines() if ln.startswith("conform: board_rev ")]
    assert len(lines) == 1, vm.serial
    return int(lines[0].rsplit(" hw_flags ", 1)[1], 16)


def assert_results(vm, skip, absent=ABSENT):
    got = results(vm)
    assert [n for _, n, _ in got] == NAMES, vm.serial
    assert [(c, n) for c, n, _ in got] == [(c, n) for c, names in CHECKS for n in names]
    assert not [g for g in got if g[2] == "FAIL"], \
        f"failed: {[m for m in conform(vm, 'check') if m['result'] == 'FAIL']}\n{vm.serial}"
    assert {n for _, n, r in got if r == "ABSENT"} == absent, got
    assert {n for _, n, r in got if r == "SKIP"} == skip, got
    # Every call of abi.h has checks of its own.
    assert {c for c, _, _ in got} - {"none"} == set(abi_syscalls().values())
    tape = conform(vm, "tape_check")
    assert tape and all(m["ok"] == "1" for m in tape), tape
    assert "conform: ROM slot text read by the tape\n" in vm.serial
    # The summary shows again after a step back from it: each time the same.
    found = conform(vm, "summary")
    assert found and all(m == found[0] for m in found), found
    summary = found[0]
    assert (summary["total"], summary["fail"], summary["skip"], summary["absent"]) == \
        (str(len(NAMES)), "0", str(len(skip)), str(len(absent))), summary
    assert_pages_show_all(vm, summary)


def assert_pages_show_all(vm, summary):
    """The pages show one row per serial check line, the last check last."""
    lines = len(conform(vm, "check"))
    assert lines == len(NAMES), vm.serial
    assert summary["rows"] == str(lines), summary
    pages = int(summary["pages"])
    shown = conform(vm, "page")[:pages]
    assert [m["n"] for m in shown] == [str(i) for i in range(1, pages + 1)], vm.serial
    # Row numbers run 1 to the last check with no gap from page to page.
    expect = 1
    for m in shown:
        assert m["first"] == str(expect), (m, vm.serial)
        expect = int(m["last"]) + 1
    assert shown[-1]["last"] == str(lines), shown[-1]
    assert shown[-1]["last_name"] == NAMES[-1], shown[-1]


def one_conform(vm, ev):
    found = conform(vm, ev)
    assert len(found) == 1, f"expected one ev={ev}, got {found}\n{vm.serial}"
    return found[0]


def test_conformance_interactive(board, kernel_image, conformance_card, tmp_path, need_mtools):
    sd = card_copy(conformance_card, tmp_path)
    answered = []
    intro = picture = page1 = summary = None
    # booted() waits for ev=ready, which comes after the ROM ends.
    with boot(kernel_image, board, sd=sd, serial2=True, timeout=TIMEOUT) as vm:
        # Each answer goes out as soon as its marker shows, with no screenshot
        # first except on page 1 and the summary, which have goldens. The ROM
        # drops stale events before it shows a screen, never after its marker,
        # so every answer counts and the next step comes within STEP_S.
        done = 0
        timeout = TIMEOUT
        while True:
            step = next_step(vm, done, timeout)
            done += 1
            timeout = STEP_S
            if step["ev"] == "intro":
                intro = screenshot(vm, tmp_path)
                reply = vm.script("key select", timeout=TIMEOUT)
                assert reply.status == "ok", reply
            elif step["ev"] == "prompt":
                answered.append(step["name"])
                if step["name"] == "picture_right":
                    picture = screenshot(vm, tmp_path)
                for line in ANSWERS[step["name"]]:
                    if isinstance(line, tuple) and line[0] == "retry":
                        wait_conform(vm, "retry", line[1])
                        continue
                    if isinstance(line, tuple) and line[0] == "wait":
                        wait_check(vm, line[1])
                        continue
                    if isinstance(line, tuple):
                        wait_markers(vm, "audio", count=line[1], timeout=STEP_S, op="pcm",
                                     len="8000")
                        continue
                    reply = vm.script(line, timeout=TIMEOUT)
                    assert reply.status == "ok", (line, reply)
            elif step["ev"] == "page":
                if step["n"] == "1":
                    page1 = screenshot(vm, tmp_path)
                reply = vm.script("key down", timeout=TIMEOUT)
                assert reply.status == "ok", reply
            elif summary is None:
                # Back to the last page once, then forward to the summary again.
                summary = screenshot(vm, tmp_path)
                reply = vm.script("key up", timeout=TIMEOUT)
                assert reply.status == "ok", reply
            else:
                reply = vm.script("key select", timeout=TIMEOUT)
                assert reply.status == "ok", reply
                break
        vm.wait_for("@RC stage=pipos ev=rom_exit ", timeout=TIMEOUT)
        vm.wait_for("@RC stage=pipos ev=ready ", timeout=TIMEOUT)
        assert answered == list(ANSWERS), answered
        assert one_conform(vm, "intro_answer")["start"] == "1", vm.serial
        assert_results(vm, skip=set())
        assert one(vm, "rom_exit")["status"] == "0", vm.serial
        assert [m["name"] for m in conform(vm, "retry")] == ["btn_up"], vm.serial
        # The 1 s sample: once with its prompt, once more for UP.
        assert len([m for m in pipos_markers(vm, "audio")
                    if m.get("op") == "pcm" and m.get("len") == "8000"]) == 2, vm.serial
        pages = int(conform(vm, "page")[0]["of"])
        assert [m["n"] for m in conform(vm, "page")] == \
            [str(i) for i in range(1, pages + 1)] + [str(pages)], vm.serial
        assert [m["ev"] for m in steps(vm)][-pages - 3:] == \
            ["page"] * pages + ["summary", "page", "summary"], vm.serial
        no_exception(vm)
    # After the results, so a FAIL shows its serial line first.
    check_golden(intro, "conform_intro")
    check_golden(picture, "conform_picture")
    check_golden(page1, "conform_page1")
    check_golden(summary, "conform_summary")


def test_conformance_auto(board, kernel_image, conformance_card, tmp_path, need_mtools):
    """Auto mode: no prompt, every interactive check SKIP, the run ends by itself."""
    sd = card_copy(conformance_card, tmp_path, auto=True)
    with booted(kernel_image, board, sd=sd) as vm:
        assert not conform(vm, "prompt"), vm.serial
        assert one_conform(vm, "start")["mode"] == "auto"
        assert_results(vm, skip=set(INTERACTIVE))
        assert hw_flags(vm) == HW_AUDIO, vm.serial
        assert one(vm, "rom_exit")["status"] == "0", vm.serial
        no_exception(vm)


def test_conformance_auto_rtc(board, kernel_image, conformance_card, tmp_path, need_mtools):
    """Auto mode with QEMU's clock chip: sys_info sets PIP_HW_RTC and get_time
    returns a time at boot, so the clock checks pass instead of ABSENT."""
    sd = card_copy(conformance_card, tmp_path, auto=True)
    with booted(kernel_image, board, sd=sd, extra_args=RTC_DEVICE) as vm:
        assert one(vm, "clock")["rtc"] == "present", vm.serial
        assert_results(vm, skip=set(INTERACTIVE), absent=ABSENT - {"sensor_rtc", "time_at_boot"})
        assert hw_flags(vm) == HW_RTC | HW_AUDIO, vm.serial
        assert one(vm, "rom_exit")["status"] == "0", vm.serial
        no_exception(vm)


@pytest.fixture(scope="session")
def pwm_kernel_image(request, robco_build):
    """The kernel with the PWM audio backend forced. On raspi0 and raspi1ap
    QEMU's PWM registers abort, so audio_init() returns AUDIO_ERR_NO_PWM and
    stays on the trace backend. On raspi2b and raspi3b PWM starts."""
    path = os.path.join(ROOT, request.config.getoption("--build-dir"), "audio-pwm")
    return robco_build(ROOT, flags=(f"BUILD={path}",
                                    "EXTRA_CFLAGS=-Isdk/include -DROBCO_AUDIO_BACKEND=1"),
                       image=os.path.join(path, "pipos.img"))


def test_conformance_auto_audio_fallback(board, pwm_kernel_image, conformance_card, tmp_path,
                                         need_mtools):
    """Auto mode when audio_init() fails but a backend still plays: the sound
    calls work, so sys_info sets PIP_HW_AUDIO and every check passes."""
    sd = card_copy(conformance_card, tmp_path, auto=True)
    with booted(pwm_kernel_image, board, sd=sd) as vm:
        init = one(vm, "audio_init")
        if board in ("raspi0", "raspi1ap"):
            assert (init["rc"], init["backend"]) == ("-5", "trace"), vm.serial
        else:
            assert (init["rc"], init["backend"]) == ("0", "pwm"), vm.serial
        assert_results(vm, skip=set(INTERACTIVE))
        assert hw_flags(vm) == HW_AUDIO, vm.serial
        assert one(vm, "rom_exit")["status"] == "0", vm.serial
        no_exception(vm)


def test_conformance_skip_interactive(board, kernel_image, conformance_card, tmp_path,
                                      need_mtools):
    """LEFT on the intro: no prompt, every interactive check SKIP."""
    if board not in ("raspi0", "raspi2b"):
        pytest.skip("the skip path is the same on every board")
    sd = card_copy(conformance_card, tmp_path)
    with boot(kernel_image, board, sd=sd, serial2=True, timeout=TIMEOUT) as vm:
        next_step(vm, 0)
        assert steps(vm)[0]["ev"] == "intro", vm.serial
        reply = vm.script("key left", timeout=TIMEOUT)
        assert reply.status == "ok", reply
        wait_conform_ev(vm, "intro_answer")
        assert one_conform(vm, "intro_answer")["start"] == "0", vm.serial
        finish_pages(vm, 1)
        vm.wait_for("@RC stage=pipos ev=ready ", timeout=TIMEOUT)
        assert not conform(vm, "prompt"), vm.serial
        assert one_conform(vm, "start")["mode"] == "interactive"
        assert_results(vm, skip=set(INTERACTIVE))
        assert one(vm, "rom_exit")["status"] == "0", vm.serial
        no_exception(vm)
