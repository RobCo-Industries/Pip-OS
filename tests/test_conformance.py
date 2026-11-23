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
from pipos_helpers import TIMEOUT, _mtools, abi_syscalls, booted, no_exception, one, screenshot
from test_card import check_golden

PART_OFFSET = 2048 * 512

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
    ("read_sensor", ["sensor_rtc", "sensor_battery", "sensor_geiger", "sensor_gps", "sensor_accel",
                    "sensor_temp", "sensor_heart", "sensor_7", "sensor_bad_ptr"]),
    ("sensor_available", ["available_rtc", "available_7"]),
    ("get_time", ["time_at_boot", "time_bad_ptr"]),
    ("get_battery", ["battery"]),
    ("ticks_ms", ["ticks_range", "ticks_advance"]),
    ("sleep_ms", ["sleep_rc", "sleep_50", "sleep_range"]),
    ("exit", ["exit_low8", "exit_zero"]),
    ("sys_info", ["sysinfo_rc", "sysinfo_fields", "sysinfo_bad_ptr"]),
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
    ("holo_enumerate", ["enum_tape", "enum_data_tape", "enum_max0", "enum_max33", "enum_null"]),
    ("holo_launch", ["launch_tape", "launch_lower_case", "launch_fault", "launch_after_fault",
                    "launch_data_tape", "launch_missing", "launch_bad_name", "launch_null_name",
                    "launch_args_no_nul", "launch_args_99"]),
    ("holo_last_status", ["last_before_launch", "last_status", "last_error"]),
    ("holo_eject_event", ["eject_none"]),
    ("none", ["unknown_number", "unknown_gap"]),
    ("wait_event", ["btn_up", "btn_down", "btn_left", "btn_right", "btn_select", "btn_dial_press"]),
    ("read_dial", ["dial_clockwise", "dial_cw_position", "dial_counter", "dial_ccw_position"]),
    ("tone", ["heard_tone"]),
    ("play_seq", ["heard_tune"]),
    ("sample", ["heard_sample"]),
    ("present", ["picture_right"]),
]
NAMES = [n for _, names in CHECKS for n in names]
INTERACTIVE = NAMES[NAMES.index("btn_up"):]

# No sensor, battery or clock chip in QEMU. The clock is unset at boot.
ABSENT = {"sensor_rtc", "sensor_battery", "sensor_geiger", "sensor_gps", "sensor_accel",
          "sensor_temp", "sensor_heart", "time_at_boot", "battery"}

# The answer the script gives to each prompt. SELECT is yes.
ANSWERS = {
    "btn_up": ["key up"], "btn_down": ["key down"], "btn_left": ["key left"],
    "btn_right": ["key right"], "btn_select": ["key select"],
    "btn_dial_press": ["key dial_push"],
    # A turn the wrong way first: the ROM ignores it but read_dial counts it.
    "dial_clockwise": ["dial -1", "dial 2"], "dial_counter": ["dial -3"],
    "heard_tone": ["key select"], "heard_tune": ["key select"], "heard_sample": ["key select"],
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
    return [m for m in conform(vm) if m["ev"] in ("prompt", "page", "summary")]


def next_step(vm, done):
    """Waits for the prompt, page or summary marker number done + 1."""
    deadline = time.monotonic() + TIMEOUT
    while True:
        found = steps(vm)
        if len(found) > done:
            return found[done]
        assert time.monotonic() < deadline, f"no step {done + 1}\n{vm.serial}"
        time.sleep(0.05)


def results(vm):
    return [(m["call"], m["name"], m["result"]) for m in conform(vm, "check")]


def assert_results(vm, skip):
    got = results(vm)
    assert [n for _, n, _ in got] == NAMES, vm.serial
    assert [(c, n) for c, n, _ in got] == [(c, n) for c, names in CHECKS for n in names]
    assert not [g for g in got if g[2] == "FAIL"], \
        f"failed: {[m for m in conform(vm, 'check') if m['result'] == 'FAIL']}\n{vm.serial}"
    assert {n for _, n, r in got if r == "ABSENT"} == ABSENT, got
    assert {n for _, n, r in got if r == "SKIP"} == skip, got
    # Every call of abi.h has checks of its own.
    assert {c for c, _, _ in got} - {"none"} == set(abi_syscalls().values())
    tape = conform(vm, "tape_check")
    assert tape and all(m["ok"] == "1" for m in tape), tape
    assert "conform: ROM slot text read by the tape\n" in vm.serial
    summary = one_conform(vm, "summary")
    assert (summary["total"], summary["fail"], summary["skip"], summary["absent"]) == \
        (str(len(NAMES)), "0", str(len(skip)), str(len(ABSENT))), summary


def one_conform(vm, ev):
    found = conform(vm, ev)
    assert len(found) == 1, f"expected one ev={ev}, got {found}\n{vm.serial}"
    return found[0]


def test_conformance_interactive(board, kernel_image, conformance_card, tmp_path, need_mtools):
    sd = card_copy(conformance_card, tmp_path)
    answered = []
    page1 = summary = None
    # booted() waits for ev=ready, which comes after the ROM ends.
    with boot(kernel_image, board, sd=sd, serial2=True, timeout=TIMEOUT) as vm:
        done = 0
        while True:
            step = next_step(vm, done)
            done += 1
            if step["ev"] == "prompt":
                answered.append(step["name"])
                for line in ANSWERS[step["name"]]:
                    reply = vm.script(line, timeout=TIMEOUT)
                    assert reply.status == "ok", (line, reply)
            elif step["ev"] == "page":
                img = screenshot(vm, tmp_path)
                img.save(tmp_path / f"conf-page{step['n']}.png")
                if step["n"] == "1":
                    page1 = img
                reply = vm.script("key down", timeout=TIMEOUT)
                assert reply.status == "ok", reply
            else:
                summary = screenshot(vm, tmp_path)
                summary.save(tmp_path / "conf-summary.png")
                vm.script("key select", timeout=TIMEOUT)
                break
        vm.wait_for("@RC stage=pipos ev=rom_exit ", timeout=TIMEOUT)
        vm.wait_for("@RC stage=pipos ev=ready ", timeout=TIMEOUT)
        assert answered == list(ANSWERS), answered
        assert_results(vm, skip=set())
        assert one(vm, "rom_exit")["status"] == "0", vm.serial
        pages = int(conform(vm, "page")[0]["of"])
        assert [m["n"] for m in conform(vm, "page")] == [str(i) for i in range(1, pages + 1)]
        no_exception(vm)
    # After the results, so a FAIL shows its serial line first.
    check_golden(page1, "conform_page1")
    check_golden(summary, "conform_summary")


def test_conformance_auto(board, kernel_image, conformance_card, tmp_path, need_mtools):
    """Auto mode: no prompt, every interactive check SKIP, the run ends by itself."""
    sd = card_copy(conformance_card, tmp_path, auto=True)
    with booted(kernel_image, board, sd=sd) as vm:
        assert not conform(vm, "prompt"), vm.serial
        assert one_conform(vm, "start")["mode"] == "auto"
        assert_results(vm, skip=set(INTERACTIVE))
        assert one(vm, "rom_exit")["status"] == "0", vm.serial
        no_exception(vm)
