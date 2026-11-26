"""The voice engine of RobCo-Core in pure Python, sample for sample.

A voice is a note computed from a table of spans. Each span is seven
32-bit words, in this order:

    n      samples in the span, 1 to N_MAX
    phase  phase at its first sample, 2^32 is one cycle
    inc    phase step per sample at its first sample, at most INC_MAX
    dinc   change of inc per sample, signed
    amp    peak at its first sample, 1/65536 of a step, 0 to AMP_MAX
    damp   change of amp per sample, signed
    p2     gain of a second partial at twice the pitch, 32768 is equal

The integer steps below are the library's, so a render here gives the
bytes the device gives. Standard library only, and no math module: the
sine table comes from a fixed sum of exact operations.

tools/sound_table.py writes the spans and tools/boot_sound.py plays
them. Both import this file.
"""

import struct
import zlib

N_MAX = 65535
INC_MAX = 0x7FFFFFFF
AMP_MAX = 127 << 16
P2_MAX = 32768
SPAN_BYTES = 28
SINE_LEN = 1024
SINE_CRC = 0x05067686

_M32 = 0xFFFFFFFF
_PI = 3.141592653589793


def _sin(x):
    """sin(x) for x in [0, pi / 2], a fixed Taylor sum."""
    x2 = x * x
    term = x
    total = 0.0
    for k in range(1, 30):
        total = total + term
        term = -term * x2 / ((2 * k) * (2 * k + 1))
    return total


def _make_sine():
    quarter = SINE_LEN // 4
    out = []
    for k in range(SINE_LEN):
        j = k % (SINE_LEN // 2)
        if j > quarter:
            j = SINE_LEN // 2 - j
        v = 32767.0 * _sin(2.0 * _PI * j / SINE_LEN) + 0.5
        v = int(v)                      # v is not negative: int() is floor
        out.append(v if k < SINE_LEN // 2 else -v)
    return tuple(out)


SINE = _make_sine()


def sine_crc():
    return zlib.crc32(struct.pack("<%dh" % SINE_LEN, *SINE))


def s32(v):
    """A 32-bit word as a signed value."""
    v &= _M32
    return v - (1 << 32) if v & 0x80000000 else v


def span_at(sp, i):
    """Sample i of span sp, on 1/256 of a step, as the library's
    audio_voice_span_at()."""
    n, phase, inc, dinc, amp, damp, p2 = sp
    ph = (phase + i * inc + (dinc & _M32) * ((i * (i - 1) & _M32) // 2)) & _M32
    a = (amp + i * damp) >> 8
    q = (SINE[ph >> 22] * a) >> 15
    if p2:
        q += (((SINE[((ph << 1) & _M32) >> 22] * a) >> 15) * p2) >> 15
    return q


def out_byte(q, mix, vol):
    """The voice plus an overlay byte, the volume, one rounding, as the
    library's audio_voice_out()."""
    if vol > 100:
        vol = 100
    x = (q + (mix - 128) * 256) * vol
    r = (x + 12800) // 25600 if x >= 0 else -((12800 - x) // 25600)
    r += 128
    return 0 if r < 0 else 255 if r > 255 else r


def check(spans):
    """None when the spans pass the library's checks, else what fails."""
    if not spans:
        return "no span"
    for k, (n, phase, inc, dinc, amp, damp, p2) in enumerate(spans):
        if not 1 <= n <= N_MAX:
            return "span %d: n=%d is outside 1 to %d" % (k, n, N_MAX)
        if not 0 <= phase <= _M32:
            return "span %d: phase is not a 32-bit word" % k
        if not 0 <= inc <= INC_MAX:
            return "span %d: inc=%d is past %d" % (k, inc, INC_MAX)
        if not -(1 << 31) <= dinc < (1 << 31) or not -(1 << 31) <= damp < (1 << 31):
            return "span %d: dinc or damp is not a signed 32-bit word" % k
        if not 0 <= p2 <= P2_MAX:
            return "span %d: p2=%d is past %d" % (k, p2, P2_MAX)
        end = amp + (n - 1) * damp
        if not 0 <= amp <= AMP_MAX or not 0 <= end <= AMP_MAX:
            return "span %d: amp leaves 0 to %d" % (k, AMP_MAX)
    return None


def pack(spans):
    """The spans as 28-byte little-endian records, the bytes the trace's
    CRC covers."""
    return b"".join(struct.pack("<IIIiiiI", *sp) for sp in spans)


def unpack(data):
    if len(data) % SPAN_BYTES:
        raise ValueError("%d bytes is not a whole number of spans" % len(data))
    return [struct.unpack_from("<IIIiiiI", data, k) for k in range(0, len(data), SPAN_BYTES)]


def length(spans):
    return sum(sp[0] for sp in spans)


class Voice:
    """Sample pos of a voice, counted from its start, by spans in turn.
    Past the last span the value is 0."""

    def __init__(self, spans):
        self.spans = spans
        self.k = 0
        self.base = 0

    def at(self, pos):
        if pos < self.base:
            self.k = 0
            self.base = 0
        spans = self.spans
        while self.k < len(spans) and pos - self.base >= spans[self.k][0]:
            self.base += spans[self.k][0]
            self.k += 1
        if self.k >= len(spans):
            return 0
        return span_at(spans[self.k], pos - self.base)


def render(spans, vol=100):
    """The voice alone as unsigned 8-bit samples, no overlay."""
    out = bytearray()
    for sp in spans:
        for i in range(sp[0]):
            out.append(out_byte(span_at(sp, i), 128, vol))
    return bytes(out)
