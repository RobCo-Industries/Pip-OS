"""The centred square of RobCo-Core in pure Python, sample for sample.

Once the pin rests at the midpoint, the library plays a tone as a square
centred on 128, one sample per carrier period on a grid of TONE_RATE.
The integer steps below are those of the library's src/lib/audio_square.c:

    half = (rate * 128 + hz / 2) / hz, once per note, at least 256
    amp  = (peak * min(vol, 100) + 50) / 100, read when the note starts
    a note starts at an edge going up, its next edge half units ahead
    each sample walks 256 units, 1/256 of a sample each, and adds its
    sign times the units before each edge, so an edge inside a sample
    takes the mean of the two levels by its place
    out = 128 + round(amp * acc / 256), a half away from 128

A rest, hz 0, writes 128. The floor of 256 makes a pitch above rate / 2
play at rate / 2. Standard library only. tools/boot_sound.py imports it.
"""

TONE_PEAK = 127         # ROBCO_AUDIO_TONE_PEAK, the library's default
TONE_RATE = 37500       # ROBCO_AUDIO_TONE_RATE, the library's default
PEAK_MAX = 127


def amp(peak, vol):
    """The height of a note in steps."""
    return (peak * min(vol, 100) + 50) // 100


def half_period(hz, rate=TONE_RATE):
    """The half period in 1/256 of a sample; 0 for a rest."""
    if not hz:
        return 0
    return max(256, (rate * 128 + hz // 2) // hz)


def out_byte(acc, a):
    """128 plus amp * acc / 256, rounded a half away from 128."""
    x = a * acc
    r = (x + 128) // 256 if x >= 0 else -((128 - x) // 256)
    return min(255, max(0, 128 + r))


class Square:
    """One note. next() gives its samples in order from its first, with no skip."""

    def __init__(self, hz, a, rate=TONE_RATE):
        self.half = half_period(hz, rate)
        self.left = self.half
        self.sign = 1
        self.amp = a

    def acc(self):
        if self.half == 0:
            return 0
        units = 256
        total = 0
        while units >= self.left:
            total += self.sign * self.left
            units -= self.left
            self.sign = -self.sign
            self.left = self.half
        total += self.sign * units
        self.left -= units
        return total

    def next(self):
        return out_byte(self.acc(), self.amp)


def render(hz, a, n, rate=TONE_RATE):
    """The first n samples of a note, as bytes."""
    q = Square(hz, a, rate)
    return bytes(q.next() for _ in range(n))
