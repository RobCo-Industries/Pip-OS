#!/usr/bin/env python3
"""Write the system sound tables of Pip-OS from kernel/sound_params.json.

    sound_table.py               write kernel/sound_table.c and .h
    sound_table.py --check       exit 1 when the committed files differ
    sound_table.py --dump DIR    each buffer as a raw .u8 file, the note
                                 as note.voice
    sound_table.py --wav DIR     each sound as an 8-bit WAV file

Samples are unsigned 8-bit, 128 is silence, at rate_hz. The rising note
is a table of spans for RobCo-Core's voice engine, which the kernel plays
live. note.voice holds the spans as 28-byte little-endian records, the
bytes whose CRC-32 the trace prints. Its WAV is the engine's own integer
render at volume 100, from tools/voice_engine.py.

The same parameters give the same bytes on every system and Python 3.
The maths uses only add, subtract, multiply, divide and comparisons,
whose results IEEE 754 fixes. No math library call and no random
module: the tool builds its own logarithm, exponential and sine, and
its own noise generator.
"""

import argparse
import importlib.util
import json
import os
import sys
import wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PARAMS = os.path.join(ROOT, "kernel", "sound_params.json")
OUT_C = os.path.join(ROOT, "kernel", "sound_table.c")
OUT_H = os.path.join(ROOT, "kernel", "sound_table.h")


class ParamError(Exception):
    pass


def _load_engine():
    """tools/voice_engine.py, found beside this file. python -I leaves the
    script's folder off sys.path, so it is loaded by its path."""
    name = "pipos_voice_engine"
    if name not in sys.modules:
        path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "voice_engine.py")
        spec = importlib.util.spec_from_file_location(name, path)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        sys.modules[name] = mod
    return sys.modules[name]


voice = _load_engine()


# ---- Exact-operation maths ----------------------------------------------

def _ln_mantissa(m):
    """ln(m) for m in [1, 2]: 2 atanh(z), z = (m - 1) / (m + 1), a fixed sum."""
    z = (m - 1.0) / (m + 1.0)
    z2 = z * z
    term = z
    total = 0.0
    for k in range(40):
        total = total + term / (2 * k + 1)
        term = term * z2
    return 2.0 * total


LN2 = _ln_mantissa(2.0)


def ln(x):
    if not x > 0.0:
        raise ValueError("ln of a value not above 0")
    e = 0
    while x >= 2.0:
        x = x / 2.0
        e += 1
    while x < 1.0:
        x = x * 2.0
        e -= 1
    return e * LN2 + _ln_mantissa(x)


def exp(x):
    k = int(x / LN2)
    r = (x - k * LN2) / 16.0
    term = 1.0
    total = 1.0
    for i in range(1, 20):
        term = term * r / i
        total = total + term
    for _ in range(4):
        total = total * total
    while k > 0:
        total = total * 2.0
        k -= 1
    while k < 0:
        total = total / 2.0
        k += 1
    return total


LN10 = ln(10.0)


def db_to_gain(db):
    return exp(db * LN10 / 20.0)


def sine_cycle(p):
    """sin(2 pi p) for p in [0, 1), by a parabola and one correction."""
    x = 2.0 * p if p < 0.5 else 2.0 * p - 2.0
    y = 4.0 * x * (1.0 - abs(x))
    return 0.225 * (y * abs(y) - y) + y


def frac(p):
    return p - int(p)


def round_half_away(v):
    return int(v + 0.5) if v >= 0.0 else -int(-v + 0.5)


class XorShift32:
    def __init__(self, seed):
        if seed <= 0 or seed > 0xFFFFFFFF:
            raise ParamError("click.seed must be 1 to 4294967295")
        self.s = seed

    def next(self):
        s = self.s
        s ^= (s << 13) & 0xFFFFFFFF
        s ^= s >> 17
        s ^= (s << 5) & 0xFFFFFFFF
        self.s = s
        return s

    def unit(self):
        """A float in [0, 1): 32 bits over 2^32, exact."""
        return self.next() / 4294967296.0


# ---- The sounds ---------------------------------------------------------

def to_u8(values):
    return bytes(128 + round_half_away(v) for v in values)


def make_click(c, rate, strength, rnd):
    n = c["samples"]
    partials = []
    for b, db in enumerate(c["band_db"]):
        low = c["band_start_hz"] + b * c["band_width_hz"]
        gain = db_to_gain(db)
        for _ in range(c["partials_per_band"]):
            hz = low + rnd.unit() * c["band_width_hz"]
            partials.append((hz / rate, rnd.unit(), gain))
    env = [db_to_gain(db) for db in c["envelope_db"]]
    step = c["envelope_step_ms"] * rate / 1000.0
    raw = []
    for i in range(n):
        s = 0.0
        for inc, ph, gain in partials:
            s = s + gain * sine_cycle(frac(ph + i * inc))
        pos = i / step
        k = int(pos)
        if k >= len(env) - 1:
            e = env[-1]
        else:
            t = pos - k
            e = env[k] + (env[k + 1] - env[k]) * t
        raw.append(s * e)
    top = 0.0
    for v in raw:
        if abs(v) > top:
            top = abs(v)
    scale = c["peak_steps"] * strength / 100.0 / top
    return to_u8([v * scale for v in raw])


def make_blip(bp, rate):
    fade_in = bp["fade_in_ms"] * rate / 1000.0
    fade_out = bp["fade_out_ms"] * rate / 1000.0
    out = []
    for note in bp["notes"]:
        n = note["samples"]
        inc = note["hz"] / rate
        for i in range(n):
            v = note["peak_steps"] * sine_cycle(frac(i * inc))
            if i < fade_in:
                v = v * (i / fade_in)
            left = n - 1 - i
            if left < fade_out:
                v = v * (left / fade_out)
            out.append(v)
    return to_u8(out)


def make_beep(bp, rate):
    """A sine at its peak, with linear edges in and out."""
    n = bp["samples"]
    edge = bp["edge_ms"] * rate / 1000.0
    inc = bp["hz"] / rate
    out = []
    for i in range(n):
        v = bp["peak_steps"] * sine_cycle(frac(i * inc))
        if i < edge:
            v = v * (i / edge)
        left = n - 1 - i
        if left < edge:
            v = v * (left / edge)
        out.append(v)
    return to_u8(out)


# ---- The rising note: spans for the voice engine ------------------------
#
# The note is not stored as samples. The kernel plays it live with
# RobCo-Core's voice engine from a table of spans, see tools/voice_engine.py.
# Inside a span the engine moves the pitch linearly in inc and the level
# linearly in amp. The curve glides by a constant ratio, so each span
# starts on the curve and its chord runs to the next span's start.

TWO32 = 4294967296


class NoteCurve:
    """The pitch and level the parameters give, by sample index."""

    def __init__(self, nt, rate):
        self.rate = rate
        self.start_hz = nt["start_hz"]
        self.segs = []                  # (t0_ms, t1_ms, f0, ln(f1 / f0))
        t = 0
        f0 = nt["start_hz"]
        for seg in nt["segments"]:
            f1 = seg["end_hz"]
            self.segs.append((t, t + seg["duration_ms"], f0, ln(f1 / f0)))
            t += seg["duration_ms"]
            f0 = f1
        self.total_ms = t
        self.level = nt["level_steps"]
        self.fade_ms = nt["fade_in_ms"]
        self.whine_t0 = self.segs[-1][0]
        self.whine_ms = nt["segments"][-1]["duration_ms"]
        self.whine_k = nt["whine_db"] * LN10 / 20.0
        self.taper_ms = nt["whine_taper_ms"]

    def ms(self, k):
        return k * 1000.0 / self.rate

    def hz(self, k):
        t = self.ms(k)
        for t0, t1, f0, lr in self.segs:
            if t < t1:
                return f0 * exp(lr * (t - t0) / (t1 - t0))
        t0, t1, f0, lr = self.segs[-1]
        return f0 * exp(lr)

    def steps(self, k):
        """The peak of the fundamental in steps at sample k."""
        t = self.ms(k)
        v = self.level * self.start_hz / self.hz(k)
        if t < self.fade_ms:
            v = v * t / self.fade_ms
        if t >= self.whine_t0:
            v = v * exp(self.whine_k * (t - self.whine_t0) / self.whine_ms)
            left = self.total_ms - t
            if left < self.taper_ms:
                v = v * (left / self.taper_ms if left > 0.0 else 0.0)
        return v

    def inc(self, k):
        return int(self.hz(k) * TWO32 / self.rate)


def sample_of_ms(ms, rate):
    """The sample nearest to a time in ms, a half sample rounds up."""
    return (ms * rate * 2 + 1000) // 2000


def note_breaks(nt, rate, curve):
    """Sample indexes where a span must start, and the total length."""
    total = sample_of_ms(curve.total_ms, rate)
    br = {0, sample_of_ms(nt["fade_in_ms"], rate),
          sample_of_ms(curve.total_ms - nt["whine_taper_ms"], rate)}
    for t0, t1, _, _ in curve.segs:
        br.add(sample_of_ms(t1, rate))
    # The second partial stops at the first sample whose pitch reaches
    # the bound. The pitch never falls, so a binary search finds it.
    lim = nt["second_partial_below_hz"]
    lo, hi = 0, total
    while lo < hi:
        mid = (lo + hi) // 2
        if curve.hz(mid) >= lim:
            hi = mid
        else:
            lo = mid + 1
    br.add(lo)
    return sorted(b for b in br if 0 <= b < total), total, lo


def build_note(nt, rate):
    """The note as a list of spans (n, phase, inc, dinc, amp, damp, p2),
    and a summary of its end."""
    curve = NoteCurve(nt, rate)
    breaks, total, p2_stop = note_breaks(nt, rate, curve)
    per = nt["span_ms"] * rate // 1000
    # Span starts: each piece between breaks in equal parts of at most
    # span_ms, the longer parts first.
    starts = []
    for a, b in zip(breaks, breaks[1:] + [total]):
        m = (b - a + per - 1) // per
        q, r = divmod(b - a, m)
        k = a
        for j in range(m):
            starts.append(k)
            k += q + (1 if j < r else 0)
    starts.append(total)
    p2_gain = round_half_away(32768.0 * db_to_gain(nt["second_partial_db"]))

    incs = [curve.inc(k) for k in starts]
    amps = [round_half_away(curve.steps(k) * 65536.0) for k in starts]
    spans = []
    phase = 0
    for j in range(len(starts) - 1):
        n = starts[j + 1] - starts[j]
        # The nearest step, a half away from zero, so inc ends near the
        # next span's start.
        d = incs[j + 1] - incs[j]
        dinc = (2 * d + n) // (2 * n) if d >= 0 else -((2 * -d + n) // (2 * n))
        # Toward zero: the last sample stays between this start and the
        # next, so the level never steps past the law.
        d = amps[j + 1] - amps[j]
        damp = d // n if d >= 0 else -((-d) // n)
        p2 = p2_gain if starts[j + 1] <= p2_stop else 0
        spans.append((n, phase, incs[j], dinc, amps[j], damp, p2))
        phase = (phase + n * incs[j] + (dinc & 0xFFFFFFFF) * (n * (n - 1) // 2)) & 0xFFFFFFFF

    # The end: the first span from which every peak, the second partial
    # with it, stays under end_below_steps. The note stops there, so no
    # interrupt runs for silence.
    floor_amp = round_half_away(nt["end_below_steps"] * 65536.0)

    def peak(sp):
        top = max(sp[4], sp[4] + (sp[0] - 1) * sp[5])
        return top + top * sp[6] // 32768
    cut = len(spans)
    while cut > 0 and peak(spans[cut - 1]) < floor_amp:
        cut -= 1
    return spans[:cut]


def check_params(p):
    rate = p["rate_hz"]
    if not 4000 <= rate <= 37500:
        raise ParamError("rate_hz must be 4000 to 37500")
    c = p["click"]
    if c["peak_steps"] > c["ceiling_steps"]:
        raise ParamError("click.peak_steps %d is above click.ceiling_steps %d"
                         % (c["peak_steps"], c["ceiling_steps"]))
    if len(c["strength_percent"]) != c["count"]:
        raise ParamError("click.strength_percent needs click.count entries")
    for s in c["strength_percent"]:
        if not 0 < s <= 100:
            raise ParamError("click.strength_percent entries must be 1 to 100")
    if c["band_start_hz"] + len(c["band_db"]) * c["band_width_hz"] > rate / 2:
        raise ParamError("click bands pass half of rate_hz")
    for note in p["blip"]["notes"]:
        if note["hz"] >= rate / 2:
            raise ParamError("blip notes must be under half of rate_hz")
    bp = p["beep"]
    if bp["hz"] >= rate / 2:
        raise ParamError("beep.hz must be under half of rate_hz")
    if not 2 * bp["edge_ms"] * rate <= bp["samples"] * 1000:
        raise ParamError("beep edges are longer than the beep")
    nt = p["note"]
    if not nt["segments"]:
        raise ParamError("note.segments is empty")
    for seg in nt["segments"]:
        if not seg["duration_ms"] > 0:
            raise ParamError("note segments need a duration_ms above 0")
        if not 0 < seg["end_hz"] < rate / 2:
            raise ParamError("note pitches must be under half of rate_hz")
    if not 0 < nt["start_hz"] < rate / 2:
        raise ParamError("note.start_hz must be under half of rate_hz")
    if not 2 * nt["second_partial_below_hz"] <= rate / 2:
        raise ParamError("note.second_partial_below_hz: the second partial passes half of rate_hz")
    if not 0 < nt["level_steps"] <= 100:
        raise ParamError("note.level_steps must be 1 to 100")
    if not nt["second_partial_db"] <= 0:
        raise ParamError("note.second_partial_db must be 0 or under")
    if not 0 < nt["span_ms"] * rate // 1000 <= voice.N_MAX:
        raise ParamError("note.span_ms gives a span outside 1 to %d samples" % voice.N_MAX)
    if not nt["whine_taper_ms"] <= nt["segments"][-1]["duration_ms"]:
        raise ParamError("note.whine_taper_ms is longer than the last segment")


def build(p):
    """The sounds as {'clicks': [bytes], 'blip': bytes, 'beep': bytes,
    'note': [span]}."""
    check_params(p)
    rate = p["rate_hz"]
    c = p["click"]
    rnd = XorShift32(c["seed"])
    clicks = [make_click(c, rate, s, rnd) for s in c["strength_percent"]]
    for k, data in enumerate(clicks):
        for b in data:
            if abs(b - 128) > c["ceiling_steps"]:
                raise ParamError("click %d passes click.ceiling_steps" % k)
    note = build_note(p["note"], rate)
    bad = voice.check(note)
    if bad:
        raise ParamError("the note's spans fail the voice engine's limits: %s" % bad)
    return {"clicks": clicks, "blip": make_blip(p["blip"], rate),
            "beep": make_beep(p["beep"], rate), "note": note}


# ---- Output ---------------------------------------------------------------

HEADER_LINE = "/* Generated by tools/sound_table.py from kernel/sound_params.json. Do not edit. */\n"


def c_bytes(data, indent):
    lines = []
    for i in range(0, len(data), 12):
        lines.append(indent + ", ".join("0x%02x" % b for b in data[i:i + 12]) + ",")
    return "\n".join(lines) + "\n"


def note_ms(spans, rate):
    """The note's length in whole ms, rounded up."""
    return (voice.length(spans) * 1000 + rate - 1) // rate


def c_words(spans, indent):
    lines = []
    for sp in spans:
        lines.append(indent + "{" + ", ".join("0x%08x" % (w & 0xFFFFFFFF) for w in sp) + "},")
    return "\n".join(lines) + "\n"


def render(p, s):
    c = p["click"]
    h = (HEADER_LINE +
         "/*\n * The system sounds: unsigned 8-bit samples, 128 is silence, at\n"
         " * SOUND_RATE_HZ. Each is at its level for volume 100.\n */\n"
         "#ifndef PIPOS_SOUND_TABLE_H\n#define PIPOS_SOUND_TABLE_H\n\n"
         "#include <stdint.h>\n\n"
         "#define SOUND_RATE_HZ       %du\n"
         "#define SOUND_CLICK_COUNT   %du\n"
         "#define SOUND_CLICK_LEN     %du\n"
         "#define SOUND_CLICK_CEILING %du  /* steps from 128 no click passes */\n"
         "#define SOUND_BLIP_LEN      %du\n"
         "#define SOUND_BEEP_LEN      %du\n"
         "#define SOUND_NOTE_SPANS    %du\n"
         "#define SOUND_NOTE_MS       %du  /* the sum of n over the rate, rounded up */\n\n"
         "/* Disk clicks, played in turn. */\n"
         "extern const uint8_t sound_click[SOUND_CLICK_COUNT][SOUND_CLICK_LEN];\n"
         "/* The line blip: two short notes. */\n"
         "extern const uint8_t sound_blip[SOUND_BLIP_LEN];\n"
         "/* The soft beep: a sine with linear edges. */\n"
         "extern const uint8_t sound_beep[SOUND_BEEP_LEN];\n"
         "/*\n * The rising note, spans for RobCo-Core's voice engine at SOUND_RATE_HZ.\n"
         " * Each row is n, phase, inc, dinc, amp, damp, p2, the order and size of\n"
         " * audio_voice_span_t. dinc and damp are signed, stored as two's complement.\n"
         " */\n"
         "extern const uint32_t sound_note_spans[SOUND_NOTE_SPANS][7];\n\n"
         "#endif\n") % (p["rate_hz"], c["count"], c["samples"], c["ceiling_steps"],
                        len(s["blip"]), len(s["beep"]), len(s["note"]),
                        note_ms(s["note"], p["rate_hz"]))
    body = [HEADER_LINE, '#include "sound_table.h"\n\n',
            "const uint8_t sound_click[SOUND_CLICK_COUNT][SOUND_CLICK_LEN] = {\n"]
    for data in s["clicks"]:
        body += ["    {\n", c_bytes(data, "        "), "    },\n"]
    body += ["};\n\nconst uint8_t sound_blip[SOUND_BLIP_LEN] = {\n", c_bytes(s["blip"], "    "),
             "};\n\nconst uint8_t sound_beep[SOUND_BEEP_LEN] = {\n", c_bytes(s["beep"], "    "),
             "};\n\nconst uint32_t sound_note_spans[SOUND_NOTE_SPANS][7] = {\n",
             c_words(s["note"], "    "), "};\n"]
    return "".join(body), h


def buffers(s):
    out = [("click%02d" % k, d) for k, d in enumerate(s["clicks"])]
    return out + [("blip", s["blip"]), ("beep", s["beep"])]


def write_wav(path, rate, data):
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(1)
        w.setframerate(rate)
        w.writeframes(data)


def write_wavs(d, rate, s):
    os.makedirs(d, exist_ok=True)
    for name, data in buffers(s):
        write_wav(os.path.join(d, name + ".wav"), rate, data)
    gap = bytes([128]) * (rate // 2)
    write_wav(os.path.join(d, "clicks_in_a_row.wav"), rate,
              gap.join(s["clicks"]))
    write_wav(os.path.join(d, "note.wav"), rate, voice.render(s["note"], 100))


def write_dump(d, s):
    os.makedirs(d, exist_ok=True)
    for name, data in buffers(s):
        with open(os.path.join(d, name + ".u8"), "wb") as f:
            f.write(data)
    with open(os.path.join(d, "note.voice"), "wb") as f:
        f.write(voice.pack(s["note"]))


def read_text(path):
    try:
        with open(path, "r", encoding="utf-8", newline="") as f:
            return f.read()
    except FileNotFoundError:
        return None


def main(argv=None):
    ap = argparse.ArgumentParser(description="Write the system sound tables.")
    ap.add_argument("--params", default=PARAMS)
    ap.add_argument("--out-c", default=OUT_C)
    ap.add_argument("--out-h", default=OUT_H)
    ap.add_argument("--check", action="store_true",
                    help="exit 1 when the C files differ from what the tool writes")
    ap.add_argument("--dump", metavar="DIR",
                    help="write each buffer as a raw .u8 file and the note as note.voice")
    ap.add_argument("--wav", metavar="DIR", help="write each sound as an 8-bit WAV file")
    a = ap.parse_args(argv)

    with open(a.params, "r", encoding="utf-8") as f:
        p = json.load(f)
    try:
        s = build(p)
    except (ParamError, KeyError, TypeError, ValueError) as e:
        print("sound_table.py: bad parameters: %s" % e, file=sys.stderr)
        return 2
    c_text, h_text = render(p, s)

    if a.dump:
        write_dump(a.dump, s)
    if a.wav:
        write_wavs(a.wav, p["rate_hz"], s)
    if a.check:
        stale = [path for path, text in ((a.out_c, c_text), (a.out_h, h_text))
                 if read_text(path) != text]
        for path in stale:
            print("sound_table.py: %s is stale: run make sound-table, then commit it"
                  % os.path.relpath(path, ROOT), file=sys.stderr)
        return 1 if stale else 0
    if not (a.dump or a.wav):
        for path, text in ((a.out_c, c_text), (a.out_h, h_text)):
            with open(path, "w", encoding="utf-8", newline="\n") as f:
                f.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
