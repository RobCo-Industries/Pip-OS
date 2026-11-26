"""The exact panel sends of tests/test_tft.py, with the kernel's retries taken out.

The library fails an SPI send when no byte moves for 20 ms plus four
byte times. Its driver reads the controller once more before it gives up,
so a host pause alone should not fail a send any more, but a stalled QEMU
still could. The kernel then retries the present once, with the whole
frame (kernel/display.c, pipos_display_send()). It prints the failed send's
ev=tft_send marker, with the rectangle it tried and the bytes that went out
before the stall, then ev=display_error op=present, but only for the
first failure of the run, then one ev=tft_send why=retry marker.

So a retry changes one present only: one more send, a whole-frame one, and
fewer bytes in the failed send. without_retries() takes out exactly that
extra send and marks the failed one. Every other send, every rectangle and
every byte count stays strict in every run. Only the byte count of a send
whose present the kernel retried is excused, by that retry.
"""

from hosttiming import Timed, judged_split

W, H = 320, 240
FULL_BYTES = W * H * 2


def rect(m):
    return tuple(int(m[k]) for k in ("x", "y", "w", "h"))


def is_pipos(m, ev):
    return m.get("stage") == "pipos" and m.get("ev") == ev


def without_retries(markers, panel=(W, H)):
    """The markers without the retry sends; a send whose present failed gets "retried".

    panel is the panel's (w, h): a retry sends (0, 0, w, h), all of it. A
    retry must follow a send, with ev=display_error op=present after that
    send for the first retry of the run: the kernel prints the error once.
    A retry that breaks these rules fails: no slow host explains it.
    """
    out, last, err, retries = [], None, False, 0
    for m in markers:
        if is_pipos(m, "display_error") and m.get("op") == "present":
            err = True
        if not is_pipos(m, "tft_send"):
            out.append(m)
            continue
        if m.get("why") != "retry":
            out.append(m)
            last, err = len(out) - 1, False
            continue
        assert last is not None and "retried" not in out[last], f"a retry without a send: {m}"
        assert err or retries, f"a retry without ev=display_error op=present before it: {m}"
        assert rect(m) == (0, 0) + tuple(panel) and \
            int(m["bytes"]) >= panel[0] * panel[1] * 2, f"a retry short of the panel: {m}"
        retries += 1
        out[last] = dict(out[last], retried=f"retry {retries} after {rect(out[last])}")
    return out


class Sends:
    """The panel sends of one run, the kernel's retries taken out.

    The checks on the send list run at once, strict. bytes_are() checks a
    send's byte count at once too, unless the kernel retried that send's
    present: then retried_bytes() checks it, excused by that retry alone.
    """

    def __init__(self, markers, panel=(W, H)):
        self.markers = without_retries(markers, panel)
        self.held = []

    def sends(self):
        return [m for m in self.markers if is_pipos(m, "tft_send")]

    def ordered(self):
        """(kind, marker) of the ROM's step and shown lines and the kernel's sends, in order."""
        out = []
        for m in self.markers:
            if m.get("stage") == "testrom" and m.get("ev") in ("step", "shown"):
                out.append((m["ev"], m))
            elif is_pipos(m, "tft_send"):
                out.append(("send", m))
        return out

    def of_step(self, n):
        """The sends from ev=step n=<n> to its ev=shown."""
        seq = self.ordered()
        i = next(k for k, (kind, m) in enumerate(seq) if kind == "step" and m["n"] == str(n))
        j = next(k for k in range(i, len(seq)) if seq[k][0] == "shown")
        return [m for kind, m in seq[i + 1:j] if kind == "send"]

    def after_panic(self):
        """The sends after the panic marker."""
        out, seen = [], False
        for m in self.markers:
            if is_pipos(m, "panic"):
                seen = True
            elif seen and is_pipos(m, "tft_send"):
                out.append(m)
        return out

    def bytes_are(self, m, ok, what):
        if "retried" in m:
            self.held.append((m, ok, what))
        else:
            assert ok(int(m["bytes"])), (what, m)


def retried_bytes():
    """The one timed check of a panel test: the byte counts of retried presents."""
    def check(s):
        for m, ok, what in s.held:
            assert ok(int(m["bytes"])), (what, m)

    def evidence(s):
        return "; ".join(m["retried"] for m, _, _ in s.held) or None

    return Timed("bytes of a retried present", check, evidence)


def judged_sends(strict):
    """Judges a panel test: strict() boots, checks everything else and returns Sends."""
    judged_split(strict, retried_bytes())


# ---- the send checks of each test, on a run's markers -----------------------------

def both_sends(markers):
    """test_both_panel_matches_hdmi: tests/roms/tft_frames.c's five steps."""
    s = Sends(markers)
    first = s.sends()[0]
    assert first["why"] == "full" and rect(first) == (0, 0, W, H), first
    unchanged = s.of_step(1)
    assert [(rect(m), m["bytes"]) for m in unchanged] == [((0, 0, 0, 0), "0")], unchanged
    small = s.of_step(2)
    assert [rect(m) for m in small] == [(101, 57, 3, 2)], small
    # CASET, PASET and RAMWR with their parameters, then 2 bytes per pixel.
    s.bytes_are(small[0], lambda n: n == 11 + 3 * 2 * 2, "the small change")
    holo = s.of_step(3)
    assert [(m["why"], rect(m)) for m in holo] == [("full", (0, 0, W, H))], holo
    # The wake, then the whole frame, so the present after it sends nothing.
    seq = s.ordered()
    shown3 = [k for k, (kind, m) in enumerate(seq) if kind == "shown"][3]
    step4 = next(k for k, (kind, m) in enumerate(seq) if kind == "step" and m["n"] == "4")
    woke = [m for kind, m in seq[shown3 + 1:step4] if kind == "send"]
    assert [(m["why"], rect(m)) for m in woke] == \
        [("wake", (0, 0, 0, 0)), ("full", (0, 0, W, H))], woke
    s.bytes_are(woke[1], lambda n: n >= FULL_BYTES, "the frame after the wake")
    assert [rect(m) for m in s.of_step(4)] == [(0, 0, 0, 0)]
    return s


def window_sends(markers):
    """test_rotation_0_sends_only_the_visible_window: tests/roms/tft_window.c at 0 degrees."""
    s = Sends(markers, (H, H))
    first = s.of_step(0)
    # The ROM's scene covers the whole window, and no more.
    assert [rect(m) for m in first] == [(0, 0, H, H)], first
    hidden = s.of_step(1)
    assert [(rect(m), m["bytes"]) for m in hidden] == [((0, 0, 0, 0), "0")], hidden
    pixel = s.of_step(2)
    assert [(m["why"], rect(m)) for m in pixel] == [("diff", (10, 20, 1, 1))], pixel
    # CASET, PASET and RAMWR with their parameters, then one pixel.
    s.bytes_are(pixel[0], lambda n: n == 11 + 2, "the pixel")
    return s


def panic_sends(markers, crc):
    """test_panic_wakes_a_blanked_panel: a wake, then the whole console.

    crc is the CRC-32 of SLPOUT then DISPON, each with DC low.
    """
    s = Sends(markers)
    seq = s.after_panic()
    assert [m["why"] for m in seq] == ["wake", "full"], seq
    assert (rect(seq[0]), seq[0]["bytes"], seq[0]["crc"]) == \
        ((0, 0, 0, 0), "2", f"{crc:08x}"), seq[0]
    assert rect(seq[1]) == (0, 0, W, H), seq[1]
    s.bytes_are(seq[1], lambda n: n >= FULL_BYTES, "the console")
    return s


def restore_sends(markers, crc):
    """test_boot_text_wakes_a_blanked_panel: after the ROM went dark, a wake, then the console.

    crc is the CRC-32 of SLPOUT then DISPON, each with DC low.
    """
    s = Sends(markers)
    seq, seen = [], False
    for m in s.markers:
        if m.get("stage") == "testrom" and m.get("ev") == "dark":
            seen = True
        elif seen and is_pipos(m, "tft_send"):
            seq.append(m)
    assert [m["why"] for m in seq] == ["wake", "full"], seq
    assert (rect(seq[0]), seq[0]["bytes"], seq[0]["crc"]) == \
        ((0, 0, 0, 0), "2", f"{crc:08x}"), seq[0]
    assert rect(seq[1]) == (0, 0, W, H), seq[1]
    s.bytes_are(seq[1], lambda n: n >= FULL_BYTES, "the boot text")
    return s


def end_sends(markers, shown):
    """test_boot_text_after_a_rom_is_one_frame: after the ROM's last frame, one whole frame.

    shown is how many frames the ROM showed. No wake, no blank frame before the text.
    The frame differs from the ROM's everywhere, so its diff is the whole panel.
    """
    s = Sends(markers)
    seq, n = [], 0
    for m in s.markers:
        if m.get("stage") == "testrom" and m.get("ev") == "shown":
            n += 1
        elif n >= shown and is_pipos(m, "tft_send"):
            seq.append(m)
    assert len(seq) == 1 and seq[0]["why"] in ("diff", "full"), seq
    assert rect(seq[0]) == (0, 0, W, H), seq[0]
    s.bytes_are(seq[0], lambda n: n >= FULL_BYTES, "the boot text")
    return s
