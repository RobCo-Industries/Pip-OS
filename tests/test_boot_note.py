"""The boot note: a rising note played live by the voice, with clicks over it.

With [audio] system_sound on, the note starts with the second boot line when
the typed text from that line on outlasts it. Clicks due while it plays mix
over it as overlays, and none starts in its last 30 ms. The audio trace's
op=voice line is the evidence.
"""

import os
import subprocess
import sys
import zlib

from pipos_helpers import booted, make_card, no_exception, pipos_markers

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CFG = "PIPBOY/PIPOS.CFG"
VOLUME = 80
CLICK_LEN = "789"
RATE = "37500"
TAIL_MS = 30
LINE1 = "*************** PIP-OS(R) V7.1.0.8 ***************"
LINE2 = "COPYRIGHT 2075 ROBCO IND."


def cfg(delay, sound="yes"):
    return (f"[display]\nchar_delay_ms = {delay}\nline_delay_ms = 0\n"
            f"[audio]\nvolume = {VOLUME}\nbeep = yes\nsystem_sound = {sound}\n").encode()


def table(tmp_path):
    """Click CRCs and the note's spans, CRC and length, from the generator's dump."""
    out = tmp_path / "pcm"
    subprocess.run([sys.executable, "-I", os.path.join(ROOT, "tools", "sound_table.py"),
                    "--dump", str(out)], check=True)
    clicks = [f"{zlib.crc32((out / f'click{k:02d}.u8').read_bytes()):08x}" for k in range(12)]
    voice = (out / "note.voice").read_bytes()
    assert len(voice) % 28 == 0
    spans = len(voice) // 28
    samples = sum(int.from_bytes(voice[i * 28:i * 28 + 4], "little") for i in range(spans))
    # SOUND_NOTE_MS rounds this length up. Trace times are whole ms, so the
    # gap from op=voice to op=pcm_end reads the length rounded down or up.
    note_ms = samples * 1000 // int(RATE)
    return clicks, spans, samples, f"{zlib.crc32(voice):08x}", note_ms


def test_note_rises_with_line_two(board, kernel_image, tmp_path, need_mtools):
    clicks, spans, samples, crc, note_ms = table(tmp_path)
    sd = make_card(tmp_path / "sd.img", {CFG: cfg(delay=30)})
    with booted(kernel_image, board, sd=sd) as vm:
        trace = pipos_markers(vm, "audio")
        no_exception(vm)
    serial = vm.serial
    assert not pipos_markers(vm, "audio_drop"), serial

    voices = [m for m in trace if m["op"] == "voice"]
    assert len(voices) == 1, voices
    v = voices[0]
    assert (v["rate"], v["spans"], v["len"], v["vol"], v["crc"]) == \
        (RATE, str(spans), str(samples), str(VOLUME), crc), v
    # After line 1's text and before line 2's.
    at = serial.index(" op=voice ")
    assert serial.index(LINE1 + "\n") < at < serial.index(LINE2 + "\n"), serial

    i = trace.index(v)
    end = next(j for j in range(i + 1, len(trace)) if trace[j]["op"] in ("pcm_end", "stop"))
    assert trace[end]["op"] == "pcm_end", trace[i:end + 1]
    start_ms, end_ms = int(v["t_ms"]), int(trace[end]["t_ms"])
    assert end_ms - start_ms >= note_ms, (start_ms, end_ms, note_ms)

    # Clicks over the note mix; none in its last 30 ms.
    between = trace[i + 1:end]
    assert all(m["op"] == "pcm_mix" for m in between), between
    assert between, "no click mixed over the note\n" + serial
    for m in between:
        assert m["len"] == CLICK_LEN, m
        assert int(m["t_ms"]) < end_ms - TAIL_MS, (m, end_ms)

    # One count of clicks across plain plays and overlays: each entry in turn.
    k = 0
    for m in trace:
        if m.get("len") != CLICK_LEN or m["op"] not in ("pcm", "pcm_mix"):
            continue
        assert m["crc"] == clicks[k % 12], (k, m)
        k += 1
    assert k > len(between), k


def test_fast_typing_plays_no_note(board, kernel_image, tmp_path, need_mtools):
    clicks = table(tmp_path)[0]
    sd = make_card(tmp_path / "sd.img", {CFG: cfg(delay=2)})
    with booted(kernel_image, board, sd=sd) as vm:
        trace = pipos_markers(vm, "audio")
        no_exception(vm)
    assert not [m for m in trace if m["op"] in ("voice", "pcm_mix")], vm.serial
    plain = [m for m in trace if m["op"] == "pcm" and m.get("len") == CLICK_LEN]
    assert plain, vm.serial
    assert [m["crc"] for m in plain] == [clicks[k % 12] for k in range(len(plain))], plain


def test_sound_off_plays_no_note(board, kernel_image, tmp_path, need_mtools):
    sd = make_card(tmp_path / "sd.img", {CFG: cfg(delay=30, sound="no")})
    with booted(kernel_image, board, sd=sd) as vm:
        no_exception(vm)
    assert " op=voice " not in vm.serial, vm.serial
    assert " op=pcm_mix " not in vm.serial, vm.serial
