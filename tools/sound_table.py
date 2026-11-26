#!/usr/bin/env python3
"""Write the system sound tables of Pip-OS from kernel/sound_params.json.

    sound_table.py               write kernel/sound_table.c and .h
    sound_table.py --check       exit 1 when the committed files differ
    sound_table.py --dump DIR    each buffer as a raw .u8 file
    sound_table.py --wav DIR     each sound as an 8-bit WAV file

Samples are unsigned 8-bit, 128 is silence, at rate_hz.

The same parameters give the same bytes on every system and Python 3.
The maths uses only add, subtract, multiply, divide and comparisons,
whose results IEEE 754 fixes. No math library call and no random
module: the tool builds its own logarithm, exponential and sine, and
its own noise generator.
"""

import argparse
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


def build(p):
    """The sounds as {'clicks': [bytes], 'blip': bytes, 'beep': bytes}.

    The note block of the parameters is kept for a later change, which
    plays the note live from a table of spans. This tool ignores it.
    """
    check_params(p)
    rate = p["rate_hz"]
    c = p["click"]
    rnd = XorShift32(c["seed"])
    clicks = [make_click(c, rate, s, rnd) for s in c["strength_percent"]]
    for k, data in enumerate(clicks):
        for b in data:
            if abs(b - 128) > c["ceiling_steps"]:
                raise ParamError("click %d passes click.ceiling_steps" % k)
    return {"clicks": clicks, "blip": make_blip(p["blip"], rate),
            "beep": make_beep(p["beep"], rate)}


# ---- Output ---------------------------------------------------------------

HEADER_LINE = "/* Generated by tools/sound_table.py from kernel/sound_params.json. Do not edit. */\n"


def c_bytes(data, indent):
    lines = []
    for i in range(0, len(data), 12):
        lines.append(indent + ", ".join("0x%02x" % b for b in data[i:i + 12]) + ",")
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
         "#define SOUND_BEEP_LEN      %du\n\n"
         "/* Disk clicks, played in turn. */\n"
         "extern const uint8_t sound_click[SOUND_CLICK_COUNT][SOUND_CLICK_LEN];\n"
         "/* The line blip: two short notes. */\n"
         "extern const uint8_t sound_blip[SOUND_BLIP_LEN];\n"
         "/* The soft beep: a sine with linear edges. */\n"
         "extern const uint8_t sound_beep[SOUND_BEEP_LEN];\n\n"
         "#endif\n") % (p["rate_hz"], c["count"], c["samples"], c["ceiling_steps"],
                        len(s["blip"]), len(s["beep"]))
    body = [HEADER_LINE, '#include "sound_table.h"\n\n',
            "const uint8_t sound_click[SOUND_CLICK_COUNT][SOUND_CLICK_LEN] = {\n"]
    for data in s["clicks"]:
        body += ["    {\n", c_bytes(data, "        "), "    },\n"]
    body += ["};\n\nconst uint8_t sound_blip[SOUND_BLIP_LEN] = {\n", c_bytes(s["blip"], "    "),
             "};\n\nconst uint8_t sound_beep[SOUND_BEEP_LEN] = {\n", c_bytes(s["beep"], "    "),
             "};\n"]
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


def write_dump(d, s):
    os.makedirs(d, exist_ok=True)
    for name, data in buffers(s):
        with open(os.path.join(d, name + ".u8"), "wb") as f:
            f.write(data)


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
    ap.add_argument("--dump", metavar="DIR", help="write each buffer as a raw .u8 file")
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
