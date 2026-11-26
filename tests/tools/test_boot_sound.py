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

    def test_volume_capped_at_100(self):
        d = self.sound("a", bytes([255]) * 100)
        duty, _ = self.render([pcm(0, d, vol=250)])
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

    def test_ramp_ignores_the_volume(self):
        duty, _ = self.render([pcm(0, bs.RAMP, vol=10), end(40)])
        self.assertEqual(duty[frame(39)], bs.RAMP[(39 * RATE) // 1000 + 1] / 256)

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


class Unknown(Case):
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


class Limits(Case):
    def test_long_line(self):
        self.assertFails(["x" * (bs.MAX_LINE + 1)], "longer than 4096 bytes")

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
        for s in ("4096 bytes", "1000000 lines", "300 s", "op=pcm_mix",
                  "pins today", "newer library only"):
            self.assertIn(s, text)


if __name__ == "__main__":
    unittest.main()
