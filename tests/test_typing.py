"""Boot text that writes itself: [display] char_delay_ms and [audio] key_click.

QEMU has no PWM model, so clicks show as the trace backend's
"ev=audio op=tone|stop" markers. The kernel polls the trace while it
writes a line, so the markers of a line's clicks come before that line on
the UART, and the line itself arrives whole.
"""

import re

from pipos_helpers import (booted, config_issues, config_summary, dump, make_card,
                           no_exception, pipos_markers)

CFG = "PIPBOY/PIPOS.CFG"
DELAY_MS = 2

# The rule of kernel/keyclick.h, restated so a change there fails here.
BASE_HZ, STEP_HZ, STEPS = 3000, 150, 5


def click_hz(c, pos):
    if c == " " or not ("!" <= c <= "~"):
        return 0
    return BASE_HZ + STEP_HZ * ((ord(c) + pos) % STEPS)


def cfg(delay=None, click=None, beep="no"):
    text = "[display]\nscheme = green\n"
    if delay is not None:
        text += f"char_delay_ms = {delay}\n"
    text += f"[audio]\nvolume = 100\nbeep = {beep}\n"
    if click is not None:
        text += f"key_click = {click}\n"
    return text.encode()


def card(tmp_path, text):
    return make_card(tmp_path / "sd.img", {CFG: text})


def boot_lines(vm):
    """The boot text lines on the UART: what the screen dump shows, in order."""
    rows = [r for r in dump(vm)]
    while rows and not rows[-1]:
        rows.pop()
    return rows


def clicks_per_line(vm, lines):
    """Walks the serial log: the audio markers seen before each boot line."""
    out = []
    pending = []
    want = list(lines)
    for raw in vm.serial.splitlines():
        if raw.startswith("@RC "):
            m = dict(kv.split("=", 1) for kv in raw[4:].split() if "=" in kv)
            if m.get("stage") == "pipos" and m.get("ev") == "audio":
                pending.append((m["op"], int(m["hz"]), int(m["t_ms"])))
            continue
        if want and raw == want[0]:
            out.append((want.pop(0), pending))
            pending = []
    assert not want, (want, vm.serial)
    return out


def test_clicks_follow_the_text(board, kernel_image, tmp_path, need_mtools):
    with booted(kernel_image, board, sd=card(tmp_path, cfg(DELAY_MS, "yes"))) as vm:
        cfgm = config_summary(vm)
        assert (cfgm["char_delay_ms"], cfgm["key_click"]) == (str(DELAY_MS), "1"), vm.serial
        lines = boot_lines(vm)
        per_line = clicks_per_line(vm, lines)
        no_exception(vm)
    assert not pipos_markers(vm, "audio_drop"), vm.serial
    assert "PIP-OS READY" in lines
    last_stop = -1
    total = 0
    for text, marks in per_line:
        want = [click_hz(c, i) for i, c in enumerate(text) if click_hz(c, i)]
        assert [m[1] for m in marks if m[0] == "tone"] == want, (text, marks)
        # Each click ends, on its own, before the next one starts.
        assert [m[0] for m in marks] == ["tone", "stop"] * len(want), (text, marks)
        for op, _, t in marks:
            assert t >= last_stop, (text, marks)
            if op == "stop":
                last_stop = t
        total += len(want)
    assert total > 300


def test_no_click_keeps_the_text(board, kernel_image, tmp_path, need_mtools):
    with booted(kernel_image, board, sd=card(tmp_path, cfg(DELAY_MS, "no"))) as vm:
        lines = boot_lines(vm)
        cfgm = config_summary(vm)
        no_exception(vm)
    assert (cfgm["char_delay_ms"], cfgm["key_click"]) == (str(DELAY_MS), "0")
    assert not pipos_markers(vm, "audio"), vm.serial
    for line in lines:
        assert line + "\n" in vm.serial
    assert "PIP-OS READY" in lines


def test_delay_zero_makes_no_sound(board, kernel_image, tmp_path, need_mtools):
    with booted(kernel_image, board, sd=card(tmp_path, cfg(0, "yes"))) as vm:
        cfgm = config_summary(vm)
        no_exception(vm)
    assert (cfgm["char_delay_ms"], cfgm["key_click"]) == ("0", "1")
    assert not pipos_markers(vm, "audio"), vm.serial


def test_beep_ends_before_the_first_click(board, kernel_image, tmp_path, need_mtools):
    with booted(kernel_image, board, sd=card(tmp_path, cfg(DELAY_MS, "yes", beep="yes"))) as vm:
        tr = [(m["op"], int(m["hz"])) for m in pipos_markers(vm, "audio")]
        no_exception(vm)
    assert tr[:3] == [("tone", 1000), ("stop", 0), ("tone", click_hz("*", 0))], tr


def test_bad_values(board, kernel_image, tmp_path, need_mtools):
    for delay, click in (("101", "loud"), ("-1", "2"), ("20ms", "")):
        sd = make_card(tmp_path / f"sd{delay}.img", {CFG: cfg(delay, click)})
        with booted(kernel_image, board, sd=sd) as vm:
            issues = config_issues(vm)
            cfgm = config_summary(vm)
            no_exception(vm)
        assert ("badvalue", "display.char_delay_ms") in issues, vm.serial
        assert ("badvalue", "audio.key_click") in issues, vm.serial
        assert (cfgm["rc"], cfgm["char_delay_ms"], cfgm["key_click"]) == ("ok", "0", "0")
        assert not pipos_markers(vm, "audio"), vm.serial


SUMMARY = re.compile(r"^@RC stage=pipos ev=config rc=ok path=\S+ line=0 scheme=.*$", re.M)


def test_keys_off_change_nothing_on_the_uart(board, kernel_image, tmp_path, need_mtools):
    """A card without the keys and one with char_delay_ms = 0, key_click = yes."""
    logs = []
    for name, text in (("absent", cfg()), ("zero", cfg(0, "yes"))):
        sd = make_card(tmp_path / f"{name}.img", {CFG: text})
        with booted(kernel_image, board, sd=sd) as vm:
            logs.append(vm.serial)
            no_exception(vm)
    summaries = [SUMMARY.findall(log) for log in logs]
    assert [len(s) for s in summaries] == [1, 1], logs
    assert summaries[0] != summaries[1]
    assert SUMMARY.sub("", logs[0]) == SUMMARY.sub("", logs[1])
