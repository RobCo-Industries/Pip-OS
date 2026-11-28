"""The GPIO buttons and the dial, driven through injected pin levels.

The test drives all nine input lines high before the boot, so the
library's stuck-line guard keeps the GPIO backend on. Then it presses and
releases each button and turns the dial one Gray state at a time through
qtest. It holds each line until the ROM prints the event, so a slow host
only makes the test slower. The kernel's own ev=key markers must show the
same events, all from the GPIO backend.

What it does not show: QEMU has no pulls, no edges and no contact bounce,
and the test drives every line high before the boot. Real contacts stay on
the hardware checklist.
"""

import time

from harness.pigpio import CW, PiGpio

from pipos_helpers import (TIMEOUT, booted, make_card, no_exception, one, pipos_markers,
                           rom_markers, rom_one)

ROM = "PIPBOY/ROM1.ROM"
# kernel/robco_config.h: the default pins, BCM numbers, all active low.
PINS = {"up": 5, "down": 6, "left": 17, "right": 22, "select": 26, "back": 27,
        "dial_push": 16}
DIAL_A, DIAL_B = 23, 24
# sdk/include/pipos/abi.h: PIP_BTN_*, the bit positions of read_buttons.
BUTTON = {"up": 0, "down": 1, "left": 2, "right": 3, "select": 4, "dial_push": 5, "back": 6}
EV_BUTTON, EV_DIAL = 1, 2
DETENT_DEG = 15
# The button whose press event proves two samples: see Run.turn().
WITNESS = "up"


def events(vm):
    return [tuple(int(m[k]) for k in ("type", "code", "value", "buttons", "dial"))
            for m in rom_markers(vm, "event")]


class Run:
    """Drives the lines and keeps the events the ROM must have printed so far."""

    def __init__(self, vm, io):
        self.vm, self.io, self.want, self.dial = vm, io, [], 0

    def event(self, kind, code, value, buttons):
        """Waits for the ROM's next event. Every event so far must be the wanted one."""
        self.want.append((kind, code, value, buttons, self.dial % 360))
        deadline = time.monotonic() + TIMEOUT
        while len(events(self.vm)) < len(self.want):
            assert time.monotonic() < deadline, (self.want[-1], self.vm.serial)
            time.sleep(0.005)
        assert events(self.vm) == self.want, self.vm.serial

    def tap(self, name):
        """Holds the line until the ROM read the press, then until it read the release."""
        code = BUTTON[name]
        self.io.press(name)
        self.event(EV_BUTTON, code, 1, 1 << code)
        self.io.release(name)
        self.event(EV_BUTTON, code, 0, 0)

    def turn(self, detents):
        """Turns the dial from rest, one Gray state at a time.

        A state gives no event of its own. The kernel samples the buttons
        and the dial in the same tick callback, and a press needs two
        samples, so a tap after the write proves the state was sampled. The
        rest state ends a detent: its dial event is the proof.
        """
        step = 1 if detents > 0 else -1
        pos = 0
        for _ in range(4 * abs(detents)):
            pos = (pos + step) % 4
            self.io.set(DIAL_A, CW[pos][0])
            self.io.set(DIAL_B, CW[pos][1])
            if pos:
                self.tap(WITNESS)
            else:
                self.dial += step * DETENT_DEG
                self.event(EV_DIAL, 0, step, 0)


def test_gpio_buttons_and_dial(board, kernel_image, test_rom, tmp_path, need_mtools):
    sd = make_card(tmp_path / "sd.img", {ROM: test_rom("input_gpio")})
    lines = list(PINS.values()) + [DIAL_A, DIAL_B]
    with booted(kernel_image, board, sd=sd, wait="jump", qtest=True, gpio_high=lines) as vm:
        vm.wait_for("ev=gpio_ready", timeout=TIMEOUT, line=True)
        ready = rom_one(vm, "gpio_ready")
        assert (ready["buttons"], ready["dial"]) == ("0", "0"), ready
        run = Run(vm, PiGpio(vm, board, names=PINS))
        for name in ("up", "down", "left", "right", "select", "back"):
            run.tap(name)
        run.turn(2)
        run.turn(-3)    # through 0 to 345 degrees
        run.tap("dial_push")
        vm.wait_for("ev=ready", timeout=TIMEOUT)
        keys = pipos_markers(vm, "key")
        no_exception(vm)
    assert one(vm, "rom_exit")["status"] == "0", vm.serial
    # The kernel's own markers: the same events, all from the GPIO backend.
    assert keys and all(m["src"] == "gpio" for m in keys), keys
    names = {code: name for name, code in BUTTON.items()}
    want = [("key", names[c], str(v)) if kind == EV_BUTTON else ("dial", "0", str(v))
            for kind, c, v, _, _ in run.want]
    got = [(m["type"], m["code"], m["value"]) for m in keys
           if (m["type"], m["value"]) != ("key", "2")]
    assert got == want, vm.serial
