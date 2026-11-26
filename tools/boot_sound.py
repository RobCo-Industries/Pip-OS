#!/usr/bin/env python3
"""Rebuild what the audio pin would play from the audio trace of a serial log.

    boot_sound.py LOG OUT.wav --pcm DIR [--pcm DIR ...] [--scale MODE]

QEMU has no PWM model, so the kernel's audio runs on RobCo-Core's trace
backend and prints one marker per change. This tool reads those markers
and writes the pin's output as a WAV file, so a person can hear a boot.

Lines it reads, each an "@RC ... ev=audio op=..." marker of any stage:

  printed by the RobCo-Core commit Pip-OS pins today:
    op=pcm rate= len= vol= crc= t_ms=    a sample play starts
    op=pcm_next len= crc= t_ms=          a queued buffer takes over
    op=pcm_end n= last= t_ms=            the play ends by itself
    op=tone hz= vol= t_ms=               a tone starts
    op=stop hz=0 vol= t_ms=              what plays stops, or a tone ends
  printed by the newer library only:
    op=pcm_mix len= crc= t_ms=           an overlay starts over the play
    the rest ramp, a plain op=pcm rate=22050 len=882 vol=100 whose CRC is
    the library's ramp: after it, silence is duty 128, not duty 0
  ev=audio_drop n=                       records lost: a warning

Rules it applies, from the library's driver:

- Sample bytes come from the .u8 files of the --pcm folders, found by the
  CRC-32 of their bytes. An unknown CRC plays as silence, a buffer of 128,
  with one warning per CRC. The pin still moves to the midpoint for it.
- Mix: play + overlay - 128, clamped to 0 to 255, then the volume.
- Volume: 128 + (s - 128) * vol / 100, vol capped at 100. --scale cut
  rounds toward 128, as the pinned library does. --scale nearest rounds to
  the nearest step, a half step away from 128, as the newer library does.
  --scale auto, the default, picks nearest when the log has an op=pcm_mix
  line or the rest ramp, and cut otherwise.
- A sample play holds duty s / 256 of the 37.5 kHz carrier. A tone is the
  pin's pulse: period round(9600000 / hz) counts, high for
  (period / 2 * vol + 50) / 100 of them. Silence is duty 0, or duty 128
  after the rest ramp.
- Output: 16-bit mono at 37500 Hz, one frame per carrier period. A frame
  holds the sample due at its start, or the pulse's mean over the frame
  for a tone. Then a one-pole 20 Hz high-pass stands for the coupling
  capacitor, so a jump of the rest level is heard as a thump. A duty step
  of 100 percent is full scale, so a tone never clips.

Limits: a line of at most 4096 bytes, at most 1000000 lines, at most
300 s of output plus a 0.3 s tail, at most 4096 .u8 files of at most 4 MiB each.

Exit status: 0 on success, 2 on a bad log, a bad option or a file error,
with one line that says what and where.
"""

import argparse
import array
import math
import os
import re
import struct
import sys
import zlib

OUT_RATE = 37500            # 9.6 MHz PWM clock / range 256
PWM_CLOCK = 9600000
HP_HZ = 20.0

MAX_LINE = 4096
MAX_LINES = 1000000
MAX_SECONDS = 300
MAX_FILES = 4096
MAX_FILE_BYTES = 4 << 20
TAIL_MS = 300

RAMP_LEN = 882
RAMP_RATE = 22050

FIELDS = {
    "pcm": ("rate", "len", "vol", "crc", "t_ms"),
    "pcm_next": ("len", "crc", "t_ms"),
    "pcm_end": ("n", "last", "t_ms"),
    "pcm_mix": ("len", "crc", "t_ms"),
    "tone": ("hz", "vol", "t_ms"),
    "stop": ("hz", "vol", "t_ms"),
}
HEX_FIELDS = ("crc",)
# Times may go back this far: the tick's clock and the PCM end time are
# read apart and each drops its fraction of a millisecond.
BACK_SLACK_MS = 10

NUM = re.compile(r"[0-9]{1,10}\Z")
HEX = re.compile(r"[0-9a-fA-F]{8}\Z")


class LogError(Exception):
    pass


def ramp_bytes():
    """The library's rest ramp: 0 to 128 on 3t^2 - 2t^3, rounded to nearest."""
    n = RAMP_LEN - 1
    n3 = n * n * n
    return bytes((128 * (3 * i * i * n - 2 * i * i * i) + n3 // 2) // n3
                 for i in range(RAMP_LEN))


RAMP = ramp_bytes()
RAMP_CRC = zlib.crc32(RAMP)


def scale_cut(s, vol):
    d = (s - 128) * vol
    q = abs(d) // 100
    return 128 + (q if d >= 0 else -q)


def scale_nearest(s, vol):
    d = (s - 128) * vol
    return 128 + ((d + 50) // 100 if d >= 0 else -((50 - d) // 100))


# ---- reading ----------------------------------------------------------

def parse_marker(text, where):
    """The ev=audio marker in `text`, as (op, fields), or None."""
    i = text.find("@RC ")
    if i < 0:
        return None
    kv = {}
    for tok in text[i + 4:].split():
        if "=" not in tok:
            raise LogError("%s: a field without '=': %r" % (where, tok))
        k, v = tok.split("=", 1)
        if k in kv:
            raise LogError("%s: field %s twice" % (where, k))
        kv[k] = v
    ev = kv.get("ev")
    if ev == "audio_drop":
        return ("drop", kv)
    if ev != "audio":
        return None
    op = kv.get("op")
    if op not in FIELDS:
        raise LogError("%s: unknown audio op %r" % (where, op))
    out = {}
    for f in FIELDS[op]:
        v = kv.get(f)
        if v is None:
            raise LogError("%s: op=%s has no %s=" % (where, op, f))
        if f in HEX_FIELDS:
            if not HEX.match(v):
                raise LogError("%s: %s=%s is not 8 hex digits" % (where, f, v))
            out[f] = int(v, 16)
        else:
            if not NUM.match(v):
                raise LogError("%s: %s=%s is not a whole number" % (where, f, v))
            out[f] = int(v)
    if op in ("pcm", "pcm_next", "pcm_mix") and out["len"] == 0:
        raise LogError("%s: op=%s with len=0" % (where, op))
    if op == "pcm" and not 1000 <= out["rate"] <= 48000:
        raise LogError("%s: rate=%d is outside 1000 to 48000" % (where, out["rate"]))
    if op == "tone" and not 20 <= out["hz"] <= 20000:
        raise LogError("%s: hz=%d is outside 20 to 20000" % (where, out["hz"]))
    if op == "stop" and out["hz"] != 0:
        raise LogError("%s: op=stop with hz=%d, not 0" % (where, out["hz"]))
    if out["t_ms"] > MAX_SECONDS * 1000:
        raise LogError("%s: t_ms=%d is past the %d s limit" % (where, out["t_ms"], MAX_SECONDS))
    return (op, out)


def read_log(path):
    """[(where, op, fields)] in log order, and the count of dropped records."""
    events = []
    dropped = 0
    name = os.path.basename(path)
    with open(path, "rb") as f:
        for no, raw in enumerate(f, 1):
            where = "%s:%d" % (name, no)
            if no > MAX_LINES:
                raise LogError("%s: more than %d lines" % (name, MAX_LINES))
            if len(raw) > MAX_LINE + 1:
                raise LogError("%s: line longer than %d bytes" % (where, MAX_LINE))
            text = raw.decode("utf-8", "replace").rstrip("\r\n")
            m = parse_marker(text, where)
            if m is None:
                continue
            if m[0] == "drop":
                n = m[1].get("n", "")
                dropped += int(n) if NUM.match(n) else 1
                continue
            events.append((where, m[0], m[1]))
    return events, dropped


def load_pcm(dirs):
    """{crc: bytes} from the .u8 files of `dirs`."""
    found = {}
    count = 0
    for d in dirs:
        try:
            names = sorted(os.listdir(d))
        except OSError as e:
            raise LogError("--pcm %s: %s" % (d, e.strerror))
        for n in names:
            if not n.endswith(".u8"):
                continue
            count += 1
            if count > MAX_FILES:
                raise LogError("--pcm: more than %d .u8 files" % MAX_FILES)
            p = os.path.join(d, n)
            if os.path.getsize(p) > MAX_FILE_BYTES:
                raise LogError("%s: larger than %d bytes" % (p, MAX_FILE_BYTES))
            with open(p, "rb") as f:
                data = f.read()
            if data:
                found.setdefault(zlib.crc32(data), data)
    return found


# ---- rendering --------------------------------------------------------

def frame_of(ms):
    return (ms * OUT_RATE + 999) // 1000


class Play:
    """A sample play: the chained buffers as one stream, and its overlay."""

    def __init__(self, t0, rate, vol, data, ramp):
        self.t0 = t0
        self.rate = rate
        self.vol = 100 if ramp else min(vol, 100)
        self.data = bytearray(data)
        self.ramp = ramp
        self.mix = None         # (first sample index, bytes)
        self.ended = False

    def index(self, frame):
        """The sample due at the start of `frame`."""
        return ((frame * 1000 - self.t0 * OUT_RATE) * self.rate) // (OUT_RATE * 1000)

    def end_ms(self):
        return self.t0 + (len(self.data) * 1000 + self.rate - 1) // self.rate


class Tone:
    def __init__(self, t0, hz, vol):
        vol = min(vol, 100)
        self.t0 = t0
        self.rng = max(2, (PWM_CLOCK + hz // 2) // hz)
        self.duty = ((self.rng // 2) * vol + 50) // 100 / self.rng


class Renderer:
    def __init__(self, pcm, scale, warn):
        self.pcm = pcm
        self.scale = scale
        self.warn = warn
        self.unknown = set()
        self.duty = array.array("f")
        self.rest = 0.0
        self.cur = None

    def lookup(self, crc, length):
        data = self.pcm.get(crc)
        if data is not None and len(data) == length:
            return data
        if length == RAMP_LEN and crc == RAMP_CRC:
            return RAMP
        if crc not in self.unknown:
            self.unknown.add(crc)
            self.warn("no .u8 file has crc=%08x len=%d: it plays as silence" % (crc, length))
        return bytes([128]) * length

    def fill_to(self, frame, level):
        n = frame - len(self.duty)
        if n > 0:
            self.duty.extend([level] * n)

    def close(self, t):
        """Renders what plays up to time t, then silence takes over."""
        end = frame_of(t)
        if end > (MAX_SECONDS * 1000 + TAIL_MS) * OUT_RATE // 1000:
            raise LogError("the output would pass %d s" % MAX_SECONDS)
        cur = self.cur
        self.cur = None
        if cur is None:
            self.fill_to(end, self.rest)
            return
        start = len(self.duty)
        if isinstance(cur, Tone):
            self.render_tone(cur, start, end)
        else:
            self.render_play(cur, start, end)

    def render_tone(self, tone, start, end):
        d = tone.duty
        per = PWM_CLOCK / tone.rng          # pulse periods per second
        out = self.duty
        t0 = tone.t0 / 1000.0

        def high(x):
            k = math.floor(x)
            return k * d + min(x - k, d)

        if d == 0:              # volume 0: the driver writes silence
            out.extend([self.rest] * max(0, end - start))
            return
        for f in range(start, end):
            a = (f / OUT_RATE - t0) * per
            b = ((f + 1) / OUT_RATE - t0) * per
            if b <= 0:
                out.append(self.rest)
            else:
                a = max(a, 0.0)
                out.append((high(b) - high(a)) / (b - a))

    def render_play(self, p, start, end):
        out = self.duty
        n = len(p.data)
        mix0, mix = p.mix if p.mix else (n, b"")
        scale = self.scale
        cache = {}
        after = 0.5 if p.ramp else self.rest
        for f in range(start, end):
            i = p.index(f)
            if i < 0:
                out.append(self.rest)
                continue
            if i >= n:
                out.append(after)
                continue
            s = p.data[i]
            j = i - mix0
            if 0 <= j < len(mix):
                s = min(255, max(0, s + mix[j] - 128))
            v = cache.get(s)
            if v is None:
                v = cache[s] = scale(s, p.vol) / 256.0
            out.append(v)
        if p.ramp:
            self.rest = 0.5

    def run(self, events):
        last = 0
        for where, op, f in events:
            t = f["t_ms"]
            if t + BACK_SLACK_MS < last:
                raise LogError("%s: t_ms=%d comes before t_ms=%d of an earlier line"
                               % (where, t, last))
            t = max(t, last)
            last = t
            cur = self.cur
            if op == "pcm":
                self.close(t)
                ramp = f["len"] == RAMP_LEN and f["crc"] == RAMP_CRC
                data = self.lookup(f["crc"], f["len"])
                self.cur = Play(t, f["rate"], f["vol"], data, ramp)
            elif op == "pcm_next":
                if not isinstance(cur, Play) or cur.ended:
                    raise LogError("%s: op=pcm_next with no sample play" % where)
                cur.data += self.lookup(f["crc"], f["len"])
            elif op == "pcm_mix":
                if not isinstance(cur, Play) or cur.ended:
                    raise LogError("%s: op=pcm_mix with no sample play" % where)
                if cur.mix and cur.mix[0] + len(cur.mix[1]) > cur.index(frame_of(t)):
                    raise LogError("%s: op=pcm_mix while an overlay plays" % where)
                k = -((cur.t0 - t) * cur.rate // 1000)      # first grid point at or after t
                cur.mix = (k, self.lookup(f["crc"], f["len"]))
            elif op == "pcm_end":
                if not isinstance(cur, Play) or cur.ended:
                    raise LogError("%s: op=pcm_end with no sample play" % where)
                cur.ended = True
            elif op == "tone":
                self.close(t)
                self.cur = Tone(t, f["hz"], f["vol"])
            else:  # stop
                self.close(t)
        cur = self.cur
        stop = last
        if isinstance(cur, Play):
            stop = max(stop, cur.end_ms())
        self.close(stop + TAIL_MS)


def high_pass(duty):
    """One-pole high-pass at HP_HZ over a duty from 0 to 1, as 16-bit samples."""
    rc = 1.0 / (2 * math.pi * HP_HZ)
    a = rc / (rc + 1.0 / OUT_RATE)
    out = array.array("h", bytes(2 * len(duty)))
    y = 0.0
    x0 = 0.0                # the pin rests low before the first sound
    clipped = 0
    for i, x in enumerate(duty):
        y = a * (y + x - x0)
        x0 = x
        v = int(round(y * 32767))
        if v > 32767:
            v, clipped = 32767, clipped + 1
        elif v < -32767:
            v, clipped = -32767, clipped + 1
        out[i] = v
    return out, clipped


def write_wav(path, samples):
    if sys.byteorder != "little":
        samples = array.array("h", samples)
        samples.byteswap()
    data = samples.tobytes()
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVE")
        f.write(b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, OUT_RATE, OUT_RATE * 2, 2, 16))
        f.write(b"data" + struct.pack("<I", len(data)))
        f.write(data)


def main(argv=None):
    ap = argparse.ArgumentParser(
        prog="boot_sound.py",
        description=__doc__.split("\n\n", 1)[1],
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log", metavar="LOG", help="a serial log with ev=audio markers")
    ap.add_argument("out", metavar="OUT.wav")
    ap.add_argument("--pcm", metavar="DIR", action="append", default=[], required=True,
                    help="a folder of .u8 files, such as from sound_table.py --dump")
    ap.add_argument("--scale", choices=("auto", "cut", "nearest"), default="auto",
                    help="how the volume rounds, see above")
    a = ap.parse_args(argv)

    def warn(msg):
        print("boot_sound.py: warning: %s" % msg, file=sys.stderr)

    try:
        events, dropped = read_log(a.log)
        pcm = load_pcm(a.pcm)
        scale = a.scale
        if scale == "auto":
            new = any(op == "pcm_mix" or (op == "pcm" and f["len"] == RAMP_LEN
                                          and f["crc"] == RAMP_CRC)
                      for _, op, f in events)
            scale = "nearest" if new else "cut"
        if dropped:
            warn("the trace lost %d records: the file misses sounds" % dropped)
        if not events:
            warn("the log has no ev=audio marker: the file is silent")
        r = Renderer(pcm, scale_nearest if scale == "nearest" else scale_cut, warn)
        r.run(events)
        samples, clipped = high_pass(r.duty)
        if clipped:
            warn("%d samples clipped" % clipped)
        write_wav(a.out, samples)
    except LogError as e:
        print("boot_sound.py: %s" % e, file=sys.stderr)
        return 2
    except OSError as e:
        print("boot_sound.py: %s: %s" % (e.filename or "", e.strerror), file=sys.stderr)
        return 2
    print("boot_sound.py: %s: %.2f s, %d markers, scale %s"
          % (a.out, len(samples) / OUT_RATE, len(events), scale))
    return 0


if __name__ == "__main__":
    sys.exit(main())
