"""Tests of tools/boot_sound.py on hand-written serial logs.

Standard library only: `make test-host` runs them with unittest. Most
checks read the duty the tool computes per carrier period, before the
high-pass, so the numbers are exact.
"""

import contextlib
import importlib.util
import io
import os
import tempfile
import time
import unittest
import wave
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TOOL = os.path.join(ROOT, "tools", "boot_sound.py")

_spec = importlib.util.spec_from_file_location("boot_sound", TOOL)
bs = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(bs)

RATE = 22050
FR = bs.OUT_RATE


def crc(data):
    return "%08x" % zlib.crc32(data)


def pcm(t, data, vol=100, rate=RATE):
    return ("@RC stage=pipos ev=audio op=pcm rate=%d len=%d vol=%d crc=%s t_ms=%d"
            % (rate, len(data), vol, crc(data), t))


def mix(t, data):
    return "@RC stage=pipos ev=audio op=pcm_mix len=%d crc=%s t_ms=%d" % (len(data), crc(data), t)


def nxt(t, data):
    return "@RC stage=pipos ev=audio op=pcm_next len=%d crc=%s t_ms=%d" % (len(data), crc(data), t)


def end(t, n=0):
    return "@RC stage=pipos ev=audio op=pcm_end n=%d last=0 t_ms=%d" % (n, t)


def tone(t, hz, vol=100):
    return "@RC stage=pipos ev=audio op=tone hz=%d vol=%d t_ms=%d" % (hz, vol, t)


def stop(t):
    return "@RC stage=pipos ev=audio op=stop hz=0 vol=100 t_ms=%d" % t


def frame(ms):
    return bs.frame_of(ms)


class Case(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = self.tmp.name
        self.pcm_dir = os.path.join(self.dir, "pcm")
        os.mkdir(self.pcm_dir)

    def tearDown(self):
        self.tmp.cleanup()

    def sound(self, name, data):
        with open(os.path.join(self.pcm_dir, name + ".u8"), "wb") as f:
            f.write(data)
        return data

    def log(self, lines):
        path = os.path.join(self.dir, "serial.log")
        with open(path, "w") as f:
            f.write("\n".join(lines) + "\n")
        return path

    def render(self, lines, scale="cut"):
        """The duty per carrier period and the warnings."""
        events, _ = bs.read_log(self.log(lines))
        warnings = []
        sc = bs.scale_nearest if scale == "nearest" else bs.scale_cut
        r = bs.Renderer(bs.load_pcm([self.pcm_dir]), sc, warnings.append)
        r.run(events)
        return r.duty, warnings

    def main(self, lines, *extra):
        out = os.path.join(self.dir, "out.wav")
        err = io.StringIO()
        with contextlib.redirect_stderr(err), contextlib.redirect_stdout(io.StringIO()):
            rc = bs.main([self.log(lines), out, "--pcm", self.pcm_dir] + list(extra))
        return rc, err.getvalue(), out

    def assertFails(self, lines, text):
        rc, err, _ = self.main(lines)
        self.assertEqual(rc, 2, err)
        self.assertIn(text, err)
        self.assertNotIn("Traceback", err)
        self.assertEqual(len(err.strip().splitlines()), 1, err)


class Placement(Case):
    def test_buffer_lands_at_its_time(self):
        d = self.sound("a", bytes([200]) * 441)       # 20 ms
        duty, w = self.render(["boot text", pcm(100, d), end(120)])
        self.assertEqual(w, [])
        self.assertEqual(duty[frame(100) - 1], 0.0)
        self.assertEqual(duty[frame(100)], 200 / 256)
        self.assertEqual(duty[frame(119)], 200 / 256)
        self.assertEqual(duty[frame(121)], 0.0)

    def test_samples_are_held_per_carrier_period(self):
        d = self.sound("ramp", bytes(range(100, 200)))
        duty, _ = self.render([pcm(0, d), end(5)])
        for f in range(0, 150):
            i = f * RATE // FR
            self.assertEqual(duty[f], (100 + i) / 256 if i < 100 else 0.0)

    def test_queued_buffer_follows_without_gap(self):
        a = self.sound("a", bytes([150]) * 220)
        b = self.sound("b", bytes([170]) * 220)
        duty, _ = self.render([pcm(0, a), nxt(10, b), end(20)])
        self.assertEqual(duty[frame(9)], 150 / 256)
        self.assertEqual(duty[(225 * FR) // RATE], 170 / 256)
        self.assertEqual(duty[frame(21)], 0.0)

    def test_play_at_the_output_rate_is_one_sample_per_frame(self):
        d = self.sound("fast", bytes((60 + 7 * i) % 256 for i in range(750)))   # 20 ms
        duty, w = self.render([pcm(40, d, rate=FR), end(60)])
        self.assertEqual(w, [])
        start = frame(40)
        self.assertEqual(start, 40 * FR // 1000)
        self.assertEqual(duty[start - 1], 0.0)
        for i in range(750):
            self.assertEqual(duty[start + i], d[i] / 256)
        self.assertEqual(duty[start + 750], 0.0)


class Volume(Case):
    def test_cut_toward_128(self):
        self.sound("a", bytes([200, 129, 50]) * 100)
        duty, _ = self.render([pcm(0, bytes([200, 129, 50]) * 100, vol=50)])
        self.assertEqual([round(duty[f * FR // RATE + 1] * 256) for f in range(3)],
                         [164, 128, 89])

    def test_nearest_step(self):
        self.sound("a", bytes([200, 129, 50]) * 100)
        duty, _ = self.render([pcm(0, bytes([200, 129, 50]) * 100, vol=50)], scale="nearest")
        self.assertEqual([round(duty[f * FR // RATE + 1] * 256) for f in range(3)],
                         [164, 129, 89])

    def test_volume_100_is_full_scale(self):
        d = self.sound("a", bytes([255]) * 100)
        duty, _ = self.render([pcm(0, d, vol=100)])
        self.assertEqual(duty[0], 255 / 256)

    def test_auto_picks_nearest_on_new_lines(self):
        d = self.sound("a", bytes([129]) * 2205)
        o = self.sound("o", bytes([128]) * 10)
        for lines, want in (([pcm(0, d, vol=50)], "cut"),
                            ([pcm(0, d, vol=50), mix(0, o)], "nearest")):
            out = io.StringIO()
            with contextlib.redirect_stdout(out), contextlib.redirect_stderr(io.StringIO()):
                bs.main([self.log(lines), os.path.join(self.dir, "o.wav"),
                         "--pcm", self.pcm_dir])
            self.assertIn("scale " + want, out.getvalue())


class Mix(Case):
    def test_overlay_adds_then_scales(self):
        play = self.sound("p", bytes([140]) * 2205)    # 100 ms
        over = self.sound("o", bytes([150]) * 441)     # 20 ms
        duty, _ = self.render([pcm(0, play, vol=50), mix(40, over), end(100)])
        self.assertEqual(duty[frame(39)], 134 / 256)   # 128 + 12 * 50 / 100
        self.assertEqual(duty[frame(41)], 145 / 256)   # 128 + 34 * 50 / 100
        self.assertEqual(duty[frame(61)], 134 / 256)

    def test_overlay_clamps(self):
        play = self.sound("p", bytes([250]) * 2205)
        over = self.sound("o", bytes([250]) * 441)
        duty, _ = self.render([pcm(0, play), mix(10, over)])
        self.assertEqual(duty[frame(11)], 255 / 256)

    def test_overlay_starts_on_the_play_grid(self):
        play = self.sound("p", bytes([128]) * 2205)
        over = self.sound("o", bytes([200]) * 441)
        duty, _ = self.render([pcm(0, play), mix(10, over)])
        first = next(f for f, v in enumerate(duty) if v > 0.6)
        self.assertEqual(first * RATE // FR, 221)      # ceil(10 ms * 22050 Hz)

    def test_overlay_ends_with_the_play(self):
        play = self.sound("p", bytes([128]) * 441)
        over = self.sound("o", bytes([200]) * 2205)
        duty, _ = self.render([pcm(0, play), mix(5, over), stop(15)])
        self.assertEqual(duty[frame(14)], 200 / 256)
        self.assertEqual(duty[frame(16)], 0.0)

    def overlays(self, times):
        play = self.sound("p", bytes([128]) * (RATE * 2))      # 2 s
        over = self.sound("o", bytes([200]) * 441)             # 20 ms
        lines = [pcm(0, play)] + [mix(t, over) for t in times] + [end(2000)]
        duty, w = self.render(lines)
        self.assertEqual(w, [])
        for t in times:
            self.assertEqual(duty[frame(t + 1)], 200 / 256, t)
            self.assertEqual(duty[frame(t + 19)], 200 / 256, t)
            self.assertEqual(duty[frame(t + 21)], 0.5, t)
        self.assertEqual(duty[frame(times[0] - 1)], 0.5)

    def test_two_overlays(self):
        self.overlays([100, 300])

    def test_three_overlays(self):
        self.overlays([100, 130, 500])

    def test_nine_overlays(self):
        self.overlays([100 + 60 * k for k in range(9)])

    def test_overlay_while_one_plays_is_a_bad_log(self):
        play = self.sound("p", bytes([128]) * RATE)
        over = self.sound("o", bytes([200]) * 441)
        self.assertFails([pcm(0, play), mix(100, over), mix(110, over)],
                         "serial.log:3: op=pcm_mix while an overlay plays")

    def test_overlay_right_after_the_last_one(self):
        # 441 samples from 100 ms end at 120 ms. t_ms drops the fraction, so
        # a line at 119 ms may be one that started at the end: it follows on.
        play = self.sound("p", bytes([128]) * RATE)
        a = self.sound("a", bytes([200]) * 441)
        b = self.sound("b", bytes([100]) * 441)
        duty, _ = self.render([pcm(0, play), mix(100, a), mix(119, b)])
        self.assertEqual(duty[frame(119) + 10], 200 / 256)
        self.assertEqual(duty[frame(121)], 100 / 256)
        self.assertEqual(duty[frame(139)], 100 / 256)

    def test_overlay_across_queued_buffer(self):
        a = self.sound("a", bytes([128]) * 220)                # 10 ms
        b = self.sound("b", bytes([140]) * 2205)
        over = self.sound("o", bytes([200]) * 441)
        duty, _ = self.render([pcm(0, a), mix(5, over), nxt(10, b), end(110)])
        self.assertEqual(duty[frame(7)], 200 / 256)
        self.assertEqual(duty[frame(15)], 212 / 256)           # 140 + 200 - 128
        self.assertEqual(duty[frame(26)], 140 / 256)

    def test_overlay_on_a_cut_play(self):
        a = self.sound("a", bytes([128]) * RATE)
        b = self.sound("b", bytes([128]) * RATE)
        over = self.sound("o", bytes([200]) * 2205)            # 100 ms
        duty, _ = self.render([pcm(0, a), mix(10, over), stop(30), pcm(40, b), end(1040)])
        self.assertEqual(duty[frame(29)], 200 / 256)
        self.assertEqual(duty[frame(31)], 0.0)
        self.assertEqual(set(duty[frame(41):frame(200)]), {0.5})

    def test_mix_without_play_fails(self):
        over = self.sound("o", bytes([200]) * 10)
        self.assertFails([mix(5, over)], "serial.log:1: op=pcm_mix with no sample play")


class Rest(Case):
    def test_duty_0_without_the_ramp(self):
        d = self.sound("a", bytes([128]) * 441)
        duty, _ = self.render([pcm(0, d), end(20)])
        self.assertEqual(duty[frame(10)], 0.5)
        self.assertEqual(duty[-1], 0.0)

    def test_ramp_moves_rest_to_midpoint(self):
        d = self.sound("a", bytes([128]) * 441)
        duty, w = self.render([pcm(0, bs.RAMP), end(40), pcm(100, d), end(120)])
        self.assertEqual(w, [])
        self.assertEqual(bs.RAMP[0], 0)
        self.assertEqual(bs.RAMP[-1], 128)
        self.assertEqual(duty[0], 0.0)
        self.assertEqual(duty[frame(70)], 0.5)
        self.assertEqual(duty[-1], 0.5)

    def test_ramp_bytes_at_another_volume_are_a_play(self):
        duty, _ = self.render([pcm(0, bs.RAMP, vol=10), end(40), stop(100)])
        self.assertEqual(duty[frame(39)], bs.scale_cut(bs.RAMP[(39 * RATE) // 1000 + 1], 10) / 256)
        self.assertEqual(duty[frame(60)], 0.0)
        self.assertEqual(duty[-1], 0.0)

    def test_ramp_bytes_at_another_rate_are_a_play(self):
        duty, _ = self.render([pcm(0, bs.RAMP, rate=11025), end(80), stop(200)])
        self.assertEqual(duty[frame(79)], bs.RAMP[(79 * 11025) // 1000 + 1] / 256)
        self.assertEqual(duty[frame(100)], 0.0)
        self.assertEqual(duty[-1], 0.0)

    def test_ramp_cut_early_leaves_duty_0(self):
        d = self.sound("a", bytes([128]) * 441)
        for cut in (stop(20), tone(20, 1000, vol=0), pcm(20, d)):
            duty, _ = self.render([pcm(0, bs.RAMP), cut, stop(60), pcm(100, d), end(120)])
            self.assertEqual(duty[frame(19)], bs.RAMP[(19 * RATE) // 1000 + 1] / 256)
            self.assertEqual(duty[frame(80)], 0.0, cut)
            self.assertEqual(duty[-1], 0.0, cut)

    def test_second_ramp_cut_early_leaves_duty_0(self):
        duty, _ = self.render([pcm(0, bs.RAMP), end(40), pcm(100, bs.RAMP), stop(120), stop(200)])
        self.assertEqual(duty[frame(80)], 0.5)
        self.assertEqual(duty[frame(160)], 0.0)
        self.assertEqual(duty[-1], 0.0)

    def test_ramp_cut_after_its_last_sample_leaves_duty_0(self):
        # The last sample sounded at 40 ms, but the stop came before the
        # ramp's own end line: the library never set the midpoint rest.
        duty, _ = self.render([pcm(0, bs.RAMP), stop(100)])
        self.assertEqual(duty[frame(60)], 0.5)      # the pin holds the last sample
        self.assertEqual(duty[-1], 0.0)

    def test_ramp_that_ends_by_itself_rests_at_the_midpoint(self):
        duty, _ = self.render([pcm(0, bs.RAMP), end(40), stop(100)])
        self.assertEqual(duty[-1], 0.5)
        # QEMU's sample interrupt is slow: the end line can come much later.
        duty, _ = self.render([pcm(0, bs.RAMP), end(216), stop(300)])
        self.assertEqual(duty[frame(100)], 0.5)
        self.assertEqual(duty[-1], 0.5)

    def test_tone_after_a_ramp_cut_after_its_last_sample_is_a_pulse(self):
        cut, _ = self.render([pcm(0, bs.RAMP), stop(100), tone(200, 1000), stop(260)])
        plain, _ = self.render([tone(200, 1000), stop(260)])
        self.assertEqual(list(cut[frame(200):frame(260)]), list(plain[frame(200):frame(260)]))

    def test_mix_or_queue_on_the_ramp_is_a_bad_log(self):
        o = self.sound("o", bytes([200]) * 10)
        self.assertFails([pcm(0, bs.RAMP), mix(5, o)], "serial.log:2: op=pcm_mix on the rest ramp")
        self.assertFails([pcm(0, bs.RAMP), nxt(5, o)], "serial.log:2: op=pcm_next on the rest ramp")

    def test_auto_scale_and_the_ramp(self):
        for lines, want in (([pcm(0, bs.RAMP), end(40)], "nearest"),
                            ([pcm(0, bs.RAMP), stop(20)], "nearest"),
                            ([pcm(0, bs.RAMP, vol=10), end(40)], "cut"),
                            ([pcm(0, bs.RAMP, rate=11025), end(80)], "cut")):
            out = io.StringIO()
            with contextlib.redirect_stdout(out), contextlib.redirect_stderr(io.StringIO()):
                bs.main([self.log(lines), os.path.join(self.dir, "o.wav"), "--pcm", self.pcm_dir])
            self.assertIn("scale " + want, out.getvalue(), lines)

    def test_thump_reaches_the_file(self):
        d = self.sound("a", bytes([128]) * 4410)
        rc, err, out = self.main([pcm(100, d), end(300)])
        self.assertEqual(rc, 0, err)
        with wave.open(out) as w:
            self.assertEqual((w.getnchannels(), w.getsampwidth(), w.getframerate()),
                             (1, 2, FR))
            raw = w.readframes(w.getnframes())
        s = [int.from_bytes(raw[i:i + 2], "little", signed=True) for i in range(0, len(raw), 2)]
        self.assertGreater(max(s[frame(100):frame(110)]), 16000)
        self.assertLess(min(s[frame(300):frame(310)]), -16000)
        self.assertEqual(s[0], 0)


def same_crc(prefix, target):
    """`prefix` plus four bytes whose CRC-32 is `target`."""
    table = []
    for i in range(256):
        c = i
        for _ in range(8):
            c = (c >> 1) ^ 0xEDB88320 if c & 1 else c >> 1
        table.append(c)
    top = {v >> 24: i for i, v in enumerate(table)}
    state = target ^ 0xFFFFFFFF
    for _ in range(4):
        i = top[state >> 24]
        state = ((state ^ table[i]) << 8 | i) & 0xFFFFFFFF
    start = zlib.crc32(prefix) ^ 0xFFFFFFFF
    return prefix + (state ^ start).to_bytes(4, "little")


class Unknown(Case):
    def test_two_files_with_one_crc_are_told_apart_by_length(self):
        a = self.sound("a", bytes([200]) * 441)
        b = self.sound("b", same_crc(bytes([60]) * 216, zlib.crc32(a)))
        self.assertEqual(zlib.crc32(a), zlib.crc32(b))
        self.assertNotEqual(len(a), len(b))
        duty, w = self.render([pcm(0, a), end(20), pcm(50, b), end(60)])
        self.assertEqual(w, [])
        self.assertEqual(duty[frame(10)], 200 / 256)
        self.assertEqual(duty[frame(55)], 60 / 256)

    def test_unknown_crc_plays_silence_with_one_warning(self):
        d = bytes([200]) * 441
        duty, w = self.render([pcm(0, d), end(20), pcm(50, d), end(70)])
        self.assertEqual(len(w), 1)
        self.assertIn("crc=" + crc(d), w[0])
        self.assertEqual(duty[frame(10)], 0.5)

    def test_same_crc_other_length_is_unknown(self):
        self.sound("a", bytes([200]) * 441)
        line = pcm(0, bytes([200]) * 441).replace("len=441", "len=400")
        _, w = self.render([line])
        self.assertEqual(len(w), 1)


class ToneCase(Case):
    def test_tone_is_the_pin_pulse(self):
        duty, _ = self.render([tone(0, 1000), stop(100)])
        # 9600 counts at 9.6 MHz, high for 4800: the mean over 1 ms is one half.
        self.assertAlmostEqual(sum(duty[0:FR // 10]) / (FR // 10), 0.5, places=3)
        self.assertEqual(max(duty[0:frame(10)]), 1.0)
        self.assertEqual(min(duty[0:frame(10)]), 0.0)
        self.assertEqual(duty[frame(101)], 0.0)

    def test_tone_volume_sets_the_duty(self):
        duty, _ = self.render([tone(0, 1000, vol=20), stop(100)])
        # (4800 * 20 + 50) / 100 = 960 counts of 9600.
        self.assertAlmostEqual(sum(duty[0:FR // 10]) / (FR // 10), 0.1, places=3)

    def test_tone_at_volume_0_rests(self):
        duty, _ = self.render([pcm(0, bs.RAMP), end(40), tone(50, 1000, vol=0), stop(80)])
        self.assertEqual(set(duty[frame(50):frame(80)]), {0.5})


class Cut(Case):
    def test_stop_cuts_the_play(self):
        d = self.sound("a", bytes([200]) * 2205)
        duty, _ = self.render([pcm(0, d), stop(30)])
        self.assertEqual(duty[frame(29)], 200 / 256)
        self.assertEqual(duty[frame(31)], 0.0)

    def test_new_play_cuts_the_old(self):
        a = self.sound("a", bytes([200]) * 2205)
        b = self.sound("b", bytes([100]) * 441)
        duty, _ = self.render([pcm(0, a), stop(30), pcm(30, b), end(50)])
        self.assertEqual(duty[frame(31)], 100 / 256)

    def test_play_that_ends(self):
        d = self.sound("a", bytes([200]) * 441)
        duty, _ = self.render([pcm(0, d), end(20), stop(500)])
        self.assertEqual(duty[frame(19)], 200 / 256)
        self.assertEqual(set(duty[frame(21):]), {0.0})

    def test_play_without_end_line_runs_to_its_length(self):
        d = self.sound("a", bytes([200]) * 441)
        duty, _ = self.render([pcm(0, d)])
        self.assertEqual(duty[frame(19)], 200 / 256)
        self.assertEqual(len(duty), frame(20 + bs.TAIL_MS))


class BadLogs(Case):
    def test_out_of_order(self):
        self.assertFails([stop(500), stop(100)],
                         "serial.log:2: t_ms=100 comes before t_ms=500")

    def test_small_step_back_is_allowed(self):
        rc, err, _ = self.main([stop(500), stop(495)])
        self.assertEqual(rc, 0, err)

    def test_missing_field(self):
        self.assertFails(["@RC stage=pipos ev=audio op=tone hz=1000 t_ms=5"],
                         "serial.log:1: op=tone has no vol=")

    def test_bad_number(self):
        self.assertFails(["@RC stage=pipos ev=audio op=tone hz=1k vol=5 t_ms=5"],
                         "hz=1k is not a whole number")

    def test_bad_crc(self):
        self.assertFails(["@RC stage=pipos ev=audio op=pcm rate=22050 len=4 vol=5 crc=xyz t_ms=5"],
                         "crc=xyz is not 8 hex digits")

    def test_unknown_op(self):
        self.assertFails(["@RC stage=pipos ev=audio op=sing t_ms=5"], "unknown audio op 'sing'")

    def test_token_without_equals(self):
        self.assertFails(["@RC stage=pipos ev=audio op=stop junk"], "a field without '='")

    def test_end_without_play(self):
        self.assertFails([end(5)], "op=pcm_end with no sample play")

    def test_next_without_play(self):
        self.assertFails([nxt(5, b"\x80")], "op=pcm_next with no sample play")

    def test_stop_with_hz(self):
        self.assertFails(["@RC stage=pipos ev=audio op=stop hz=5 vol=5 t_ms=5"],
                         "op=stop with hz=5")

    def test_other_lines_are_skipped(self):
        rc, err, _ = self.main(["garbage \x00\xff", "@RC stage=pipos ev=ready",
                                "@RC stage=pipos ev=audio_drop n=3", stop(5)])
        self.assertEqual(rc, 0, err)
        self.assertIn("lost 3 records", err)

    def test_missing_log(self):
        rc, err, _ = self.main([])
        os.remove(os.path.join(self.dir, "serial.log"))
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            rc = bs.main([os.path.join(self.dir, "serial.log"), "o.wav", "--pcm", self.pcm_dir])
        self.assertEqual(rc, 2)
        self.assertNotIn("Traceback", err.getvalue())


class Bounds(Case):
    """Each number of a line is bounded when read, before anything uses it."""

    def fast_fail(self, line, text):
        t = time.monotonic()
        self.assertFails([line], text)
        self.assertLess(time.monotonic() - t, 0.5)

    def test_huge_len_with_unknown_crc(self):
        self.fast_fail("@RC stage=pipos ev=audio op=pcm rate=22050 len=9999999999 vol=100 "
                       "crc=12345678 t_ms=0", "serial.log:1: len=9999999999 is outside 1 to 11250000")

    def test_len_one_past_the_bound(self):
        for op in ("pcm rate=22050 vol=100", "pcm_next", "pcm_mix"):
            self.fast_fail("@RC stage=pipos ev=audio op=%s len=11250001 crc=12345678 t_ms=0" % op,
                           "len=11250001 is outside 1 to 11250000")

    def test_len_zero(self):
        self.fast_fail("@RC stage=pipos ev=audio op=pcm_mix len=0 crc=12345678 t_ms=0",
                       "len=0 is outside")

    def test_len_at_the_bound_is_read(self):
        events, _ = bs.read_log(self.log(
            ["@RC stage=pipos ev=audio op=pcm rate=22050 len=11250000 vol=100 crc=12345678 t_ms=0"]))
        self.assertEqual(events[0][2]["len"], bs.MAX_PCM_LEN)

    def test_chained_buffers_past_the_bound(self):
        line = "@RC stage=pipos ev=audio op=%s len=6000000 crc=12345678 t_ms=0"
        rc, err, _ = self.main([line.replace("%s", "pcm rate=22050 vol=100"), line % "pcm_next"])
        self.assertEqual(rc, 2, err)
        self.assertIn("serial.log:2: the play's buffers pass 11250000", err.splitlines()[-1])

    def test_rate(self):
        for rate in (3999, 37501, 9999999999):
            self.fast_fail("@RC stage=pipos ev=audio op=pcm rate=%d len=4 vol=100 crc=12345678 "
                           "t_ms=0" % rate, "rate=%d is outside 4000 to 37500" % rate)

    def test_vol(self):
        self.fast_fail("@RC stage=pipos ev=audio op=pcm rate=8000 len=4 vol=101 crc=12345678 t_ms=0",
                       "vol=101 is past 100")
        self.fast_fail("@RC stage=pipos ev=audio op=tone hz=440 vol=9999999999 t_ms=0",
                       "vol=9999999999 is past 100")
        self.fast_fail("@RC stage=pipos ev=audio op=stop hz=0 vol=101 t_ms=0", "vol=101 is past 100")

    def test_hz(self):
        for hz in (19, 20001, 9999999999):
            self.fast_fail("@RC stage=pipos ev=audio op=tone hz=%d vol=5 t_ms=0" % hz,
                           "hz=%d is outside 20 to 20000" % hz)

    def test_end_counts(self):
        self.fast_fail("@RC stage=pipos ev=audio op=pcm_end n=11250001 last=0 t_ms=0",
                       "n=11250001 is past 11250000")
        self.fast_fail("@RC stage=pipos ev=audio op=pcm_end n=1 last=11250000 t_ms=0",
                       "last=11250000 is past 11249999")

    def test_t_ms(self):
        self.fast_fail("@RC stage=pipos ev=audio op=stop hz=0 vol=5 t_ms=9999999999",
                       "t_ms=9999999999 is past the 300 s limit")

    def test_drop_count(self):
        for n in ("n=4294967296", "n=x", "m=3"):
            self.fast_fail("@RC stage=pipos ev=audio_drop " + n,
                           "serial.log:1: ev=audio_drop needs n= from 0 to 4294967295")


class Limits(Case):
    def test_long_line(self):
        self.assertFails(["x" * (bs.MAX_LINE + 1)], "longer than 4096 bytes")

    def test_long_line_is_not_read_whole(self):
        path = os.path.join(self.dir, "serial.log")
        with open(path, "wb") as f:
            f.write(b"x" * (64 << 20))
        seen = []
        real = io.BufferedReader.readline
        class Spy(io.BufferedReader):
            def readline(self, size=-1):
                line = real(self, size)
                seen.append(len(line))
                return line
        old = bs.open if hasattr(bs, "open") else None
        bs.open = lambda p, m: Spy(io.FileIO(p, "r"))
        try:
            with self.assertRaises(bs.LogError) as e:
                bs.read_log(path)
        finally:
            if old is None:
                del bs.open
            else:
                bs.open = old
        self.assertIn("longer than 4096 bytes", str(e.exception))
        self.assertLessEqual(max(seen), bs.MAX_LINE + 2)

    def test_files_together_past_limit(self):
        old = bs.MAX_PCM_BYTES
        bs.MAX_PCM_BYTES = 1000
        try:
            self.sound("a", bytes([1]) * 600)
            self.sound("b", bytes([2]) * 600)
            self.assertFails(["x"], "--pcm: the .u8 and .voice files pass 1000 bytes together")
        finally:
            bs.MAX_PCM_BYTES = old

    def test_time_past_limit(self):
        self.assertFails([stop(bs.MAX_SECONDS * 1000 + 1)], "past the 300 s limit")

    def test_play_past_limit(self):
        d = self.sound("a", bytes([128]) * 2000000)
        self.assertFails([pcm(0, d, rate=4000)], "the output would pass 300 s")

    def test_too_many_lines(self):
        old = bs.MAX_LINES
        bs.MAX_LINES = 3
        try:
            self.assertFails([stop(1)] * 4, "more than 3 lines")
        finally:
            bs.MAX_LINES = old

    def test_help_states_limits_and_line_forms(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out), self.assertRaises(SystemExit):
            bs.main(["--help"])
        text = out.getvalue()
        for s in ("4096 bytes", "1000000 lines", "300 s", "op=pcm_mix", "4000 to 37500",
                  "20 to 20000", "11250000", "4294967295",
                  "pins today", "newer library only", "op=voice", "149796", ".voice"):
            self.assertIn(s, text)



# ---- Centred tones -----------------------------------------------------------

SQ = bs.square
RAMPED = [pcm(0, bs.RAMP), end(40)]


def as_bytes(duty):
    out = [round(d * 256) for d in duty]
    for o, d in zip(out, duty):
        assert o == d * 256, d
    return out


def pulse(hz, vol, n):
    """The pin's pulse of a tone from frame 0, by the pulse path."""
    r = bs.Renderer({}, bs.scale_cut, lambda m: None)
    r.render_tone(bs.Tone(0, hz, vol), 0, n)
    return list(r.duty)


class CentredTone(Case):
    def test_mean_is_128_and_ends_step_by_its_height(self):
        duty, _ = self.render(RAMPED + [tone(50, 1000), stop(52)])
        a, b = frame(50), frame(52)
        x = as_bytes(duty[a:b])
        # half 4800 units, a period of 37.5 samples: two periods are 75.
        self.assertEqual(sum(x[:75]), 128 * 75)
        self.assertEqual(duty[a - 1], 0.5)
        self.assertEqual(duty[b], 0.5)
        self.assertLessEqual(abs(x[0] - 128), 127)
        self.assertLessEqual(abs(x[-1] - 128), 127)
        self.assertEqual(x[0], 255)

    def test_without_the_ramp_the_tone_is_the_pulse(self):
        duty, _ = self.render([tone(0, 1000, vol=60), stop(100)])
        self.assertEqual(list(duty[:frame(100)]), pulse(1000, 60, frame(100)))

    def test_a_ramp_cut_early_keeps_the_pulse(self):
        duty, _ = self.render([pcm(0, bs.RAMP), stop(20), tone(40, 1000), stop(140)])
        a = frame(40)
        n = frame(140) - a
        want = pulse(1000, 100, n)
        self.assertEqual(len(duty[a:a + n]), n)
        for i in range(n):
            # The pulse path works in float time from the tone's start.
            self.assertAlmostEqual(duty[a + i], want[i], places=9)
        self.assertEqual(duty[a + n], 0.0)

    def test_height_follows_the_volume(self):
        for vol, amp in ((100, 127), (80, 102), (20, 25)):
            duty, _ = self.render(RAMPED + [tone(50, 1000, vol=vol), stop(60)])
            x = as_bytes(duty[frame(50):frame(60)])
            self.assertEqual((max(x), min(x)), (128 + amp, 128 - amp), vol)

    def test_tone_peak_sets_the_height(self):
        events, _ = bs.read_log(self.log(RAMPED + [tone(50, 1000), stop(60)]))
        r = bs.Renderer({}, bs.scale_cut, lambda m: None, peak=64)
        r.run(events)
        x = as_bytes(r.duty[frame(50):frame(60)])
        self.assertEqual((max(x), min(x)), (192, 64))

    def test_a_pitch_above_half_the_grid_plays_half_the_grid(self):
        duty, _ = self.render(RAMPED + [tone(50, 20000), stop(52)])
        x = as_bytes(duty[frame(50):frame(52)])
        self.assertEqual(x, [255 if i % 2 == 0 else 1 for i in range(len(x))])

    def test_sequence_with_a_rest(self):
        duty, _ = self.render(RAMPED + [tone(50, 1000), stop(60), tone(70, 500, vol=50),
                                        stop(90)])
        self.assertEqual(set(duty[frame(60):frame(70)]), {0.5})
        x = as_bytes(duty[frame(70):frame(90)])
        # A new note: an edge going up, at the volume of its own line.
        self.assertEqual(x[0], 128 + 64)
        self.assertEqual(min(x), 128 - 64)
        self.assertEqual(x, list(SQ.render(500, 64, len(x))))

    def test_help_states_the_tone_rule(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out), self.assertRaises(SystemExit):
            bs.main(["--help"])
        text = " ".join(out.getvalue().split())
        for s in ("centred square", "rest ramp reached its end", "cut early",
                  "max(256, (37500 * 128 + hz / 2) / hz)", "18750 Hz",
                  "(peak * vol + 50) / 100", "edge going up", "next note",
                  "no late IRQ", "assumes the kernel's peak", "--tone-peak"):
            self.assertIn(s, text)

    def test_tone_peak_out_of_range_fails(self):
        err = io.StringIO()
        with contextlib.redirect_stderr(err), self.assertRaises(SystemExit) as e:
            bs.main([self.log([stop(0)]), os.path.join(self.dir, "o.wav"),
                     "--pcm", self.pcm_dir, "--tone-peak", "128"])
        self.assertEqual(e.exception.code, 2)
        self.assertIn("--tone-peak must be 0 to 127", err.getvalue())


SQUARE_C = os.path.join(ROOT, "external", "robco-core", "src", "lib", "audio_square.c")
SQUARE_MAIN = r"""
#include <stdio.h>
#include <stdlib.h>
#include <robco/audio.h>

int main(int argc, char **argv)
{
    uint32_t hz = (uint32_t)strtoul(argv[1], 0, 10);
    unsigned vol = (unsigned)strtoul(argv[2], 0, 10);
    uint32_t n = (uint32_t)strtoul(argv[3], 0, 10);
    audio_square_t q;

    audio_square_start(&q, hz, 37500, audio_square_amp(127, vol));
    for (uint32_t i = 0; i < n; i++) {
        putchar(audio_square_at(&q, i));
    }
    return 0;
}
"""


def c_compiler():
    for cc in ("cc", "gcc", "clang"):
        for d in os.environ.get("PATH", "").split(os.pathsep):
            if d and os.access(os.path.join(d, cc), os.X_OK):
                return os.path.join(d, cc)
    return None


class SquareAgainstLibrary(unittest.TestCase):
    """The library's own square, built with the host compiler, gives the
    same samples as tools/tone_square.py."""

    def test_same_bytes_as_the_c_square(self):
        if not os.path.exists(SQUARE_C):
            self.skipTest("the pinned RobCo-Core has no src/lib/audio_square.c")
        cc = c_compiler()
        if cc is None:
            self.skipTest("no host C compiler")
        import subprocess
        with tempfile.TemporaryDirectory() as d:
            main = os.path.join(d, "main.c")
            with open(main, "w", encoding="utf-8") as f:
                f.write(SQUARE_MAIN)
            exe = os.path.join(d, "square")
            subprocess.run([cc, "-std=gnu11", "-O1", "-Wall", "-Werror",
                            "-I", os.path.join(ROOT, "external", "robco-core", "include"),
                            "-o", exe, main, SQUARE_C], check=True)
            for hz in (0, 20, 440, 1000, 2637, 7902, 18750, 20000):
                for vol in (0, 1, 20, 50, 80, 100):
                    got = subprocess.run([exe, str(hz), str(vol), "4000"], check=True,
                                         stdout=subprocess.PIPE).stdout
                    want = SQ.render(hz, SQ.amp(SQ.TONE_PEAK, vol), 4000)
                    self.assertEqual(got, want, (hz, vol))


# ---- Voices ----------------------------------------------------------------

V = bs.voice
SINE_TOP = 256 << 22            # the phase of the sine table's peak


def voice_line(t, spans, vol=100, rate=FR, length=None, count=None):
    data = V.pack(spans)
    return ("@RC stage=pipos ev=audio op=voice rate=%d spans=%d len=%d vol=%d crc=%s t_ms=%d"
            % (rate, len(spans) if count is None else count,
               V.length(spans) if length is None else length, vol, crc(data), t))


class Voice(Case):
    def spans(self, name, spans):
        with open(os.path.join(self.pcm_dir, name + ".voice"), "wb") as f:
            f.write(V.pack(spans))
        return spans

    def test_voice_overlay_and_end(self):
        sp = self.spans("v", [(1000, 0, 0x10000000, 0, 20 << 16, 0, 0),
                              (1000, 0, 0x01000000, 50, 20 << 16, -300, 2603)])
        over = self.sound("o", bytes([140]) * 300)
        duty, w = self.render([voice_line(40, sp, vol=80), mix(50, over), end(94)])
        self.assertEqual(w, [])
        v0 = frame(40)
        m0 = frame(50) - v0
        voc = V.Voice(sp)
        for i in range(2000):
            o = over[i - m0] if 0 <= i - m0 < 300 else 128
            self.assertEqual(duty[v0 + i], V.out_byte(voc.at(i), o, 80) / 256, i)
        self.assertEqual(duty[v0 - 1], 0.0)
        self.assertEqual(duty[v0 + 2000], 0.0)
        # The test vector of the engine: sample 4 is 5119, sample 5 4730.
        self.assertEqual(duty[v0 + 4], V.out_byte(5119, 128, 80) / 256)

    def test_one_rounding(self):
        # The voice is 100 on 1/256 of a step, the overlay +1 step, the
        # volume 40: (100 + 256) * 40 / 25600 rounds once to 1 step. Two
        # roundings, the voice first, would give 0.
        amp = 101 << 8
        sp = self.spans("v", [(3750, SINE_TOP, 0, 0, amp, 0, 0)])
        self.assertEqual(V.span_at(sp[0], 0), 100)
        over = self.sound("o", bytes([129]) * 375)
        duty, _ = self.render([voice_line(0, sp, vol=40), mix(20, over), end(100)])
        self.assertEqual(duty[frame(10)], 128 / 256)
        self.assertEqual(duty[frame(21)], 129 / 256)
        self.assertEqual(bs.scale_nearest(128 + 0, 40) + bs.scale_nearest(129, 40) - 128, 128)

    def test_scale_option_does_not_change_a_voice(self):
        sp = self.spans("v", [(3750, SINE_TOP, 0, 0, 30 << 16, 0, 0)])
        a, _ = self.render([voice_line(0, sp, vol=50)], scale="cut")
        b, _ = self.render([voice_line(0, sp, vol=50)], scale="nearest")
        self.assertEqual(a, b)
        self.assertEqual(a[5], V.out_byte(V.span_at(sp[0], 5), 128, 50) / 256)

    def test_stop_cuts_a_voice(self):
        sp = self.spans("v", [(3750, SINE_TOP, 0, 0, 30 << 16, 0, 0)])
        duty, _ = self.render([voice_line(0, sp), stop(40)])
        self.assertNotEqual(duty[frame(39)], 0.0)
        self.assertEqual(duty[frame(41)], 0.0)

    def test_unknown_voice_is_silence_with_one_warning(self):
        sp = [(3750, SINE_TOP, 0, 0, 30 << 16, 0, 0)]
        duty, w = self.render([voice_line(0, sp), end(100), voice_line(200, sp), end(300)])
        self.assertEqual(len(w), 1)
        self.assertIn("no .voice file has crc=", w[0])
        self.assertEqual(duty[frame(50)], 128 / 256)
        self.assertEqual(duty[frame(250)], 128 / 256)

    def test_found_by_crc_and_length(self):
        sp = self.spans("v", [(3750, SINE_TOP, 0, 0, 30 << 16, 0, 0)])
        _, w = self.render([voice_line(0, sp, count=2, length=3750)])
        self.assertEqual(len(w), 1)

    def test_queue_on_a_voice_is_a_bad_log(self):
        sp = self.spans("v", [(3750, SINE_TOP, 0, 0, 30 << 16, 0, 0)])
        d = self.sound("a", bytes([140]) * 100)
        self.assertFails([voice_line(0, sp), nxt(10, d)], "serial.log:2: op=pcm_next on a voice")

    def test_len_must_match_the_spans(self):
        sp = self.spans("v", [(3750, SINE_TOP, 0, 0, 30 << 16, 0, 0)])
        self.assertFails([voice_line(0, sp, length=3751)],
                         "len=3751, but the spans of crc=")

    def test_bad_voice_file(self):
        with open(os.path.join(self.pcm_dir, "odd.voice"), "wb") as f:
            f.write(bytes(27))
        self.assertFails([end(0)], "27 bytes is not a whole number of spans")

    def test_voice_file_outside_the_limits(self):
        self.spans("v", [(10, 0, 0, 0, 0, -1, 0)])
        self.assertFails([end(0)], "span 0: amp leaves 0 to")

    def test_empty_voice_file_is_an_error(self):
        open(os.path.join(self.pcm_dir, "v.voice"), "wb").close()
        self.assertFails([end(0)], "v.voice: no span")

    def test_empty_sample_file_is_skipped(self):
        open(os.path.join(self.pcm_dir, "e.u8"), "wb").close()
        rc, err, _ = self.main([stop(0)])
        self.assertEqual(rc, 0, err)

    def test_auto_picks_nearest_on_a_voice(self):
        sp = self.spans("v", [(3750, SINE_TOP, 0, 0, 30 << 16, 0, 0)])
        out = io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(io.StringIO()):
            bs.main([self.log([voice_line(0, sp)]), os.path.join(self.dir, "o.wav"),
                     "--pcm", self.pcm_dir])
        self.assertIn("scale nearest", out.getvalue())

    def test_bounds(self):
        base = "@RC stage=pipos ev=audio op=voice rate=%s spans=%s len=%s vol=%s crc=12345678 t_ms=0"
        for args, text in (((37501, 1, 1, 100), "rate=37501 is outside 4000 to 37500"),
                           ((3999, 1, 1, 100), "rate=3999 is outside"),
                           ((37500, 0, 1, 100), "spans=0 is outside 1 to 149796"),
                           ((37500, 149797, 1, 100), "spans=149797 is outside 1 to 149796"),
                           ((37500, 1, 0, 100), "len=0 is outside"),
                           ((37500, 1, 11250001, 100), "len=11250001 is outside"),
                           ((37500, 1, 1, 101), "vol=101 is past 100")):
            t = time.monotonic()
            self.assertFails([base % args], text)
            self.assertLess(time.monotonic() - t, 0.5)
        self.assertFails(["@RC stage=pipos ev=audio op=voice rate=37500 len=1 vol=1 "
                          "crc=12345678 t_ms=0"], "op=voice has no spans=")

    def test_voice_at_the_bound_is_read(self):
        _, w = self.render(["@RC stage=pipos ev=audio op=voice rate=37500 spans=149796 "
                            "len=1 vol=100 crc=12345678 t_ms=0", stop(1)])
        self.assertEqual(len(w), 1)


if __name__ == "__main__":
    unittest.main()
