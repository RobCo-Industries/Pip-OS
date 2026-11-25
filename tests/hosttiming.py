"""Judging a run when the host may have stalled QEMU.

QEMU runs without icount here, so the guest clock follows the host. A host
that stalls QEMU stretches guest time, and a test that needs the guest to do
something within a time limit can fail with a correct kernel. The guest
measures the stretch itself: a ROM records the longest time between its
steps, and the kernel marks a retried SPI send.

A test runs judged(attempt). attempt() boots and checks. It raises
CannotJudge when the run's own numbers show the host was too slow and the
strict check failed. judged() then runs it again, up to TRIES runs in all,
and skips with a reason that starts with PREFIX. Any other failure fails at
once. tests/conftest.py counts those skips and fails the session when one
board has too many.

Host timing may excuse only the check that the run's evidence explains. A
test states its checks with split_checks() or judged_split(): strict checks
that fail in every run, and named timed checks, each with its own evidence.
"""

import pytest

PREFIX = "host timing:"
TRIES = 4
# ticks_ms counts whole 10 ms ticks, so a gap the ROM measures can read up to
# one tick short.
TICK_MS = 10


class CannotJudge(Exception):
    """The run's own numbers show the host was too slow to judge the claim."""


def judged(attempt, tries=TRIES):
    """Returns attempt()'s result; after `tries` runs that cannot judge, skips."""
    reasons = []
    for _ in range(tries):
        try:
            return attempt()
        except CannotJudge as e:
            reasons.append(str(e))
            print(f"\n{PREFIX} {e}")
    pytest.skip(f"{PREFIX} {tries} runs, " + "; ".join(reasons))


class Timed:
    """A named check that host timing can break, with the evidence that excuses it.

    check(state) asserts. evidence(state) returns the run's own reason to
    think the host broke this check, or None. The evidence names the one
    check it explains: it never excuses another one.
    """

    def __init__(self, name, check, evidence):
        self.name, self.check, self.evidence = name, check, evidence


def split_checks(strict, *timed):
    """One run of a test that host timing can break. The only way to judge one.

    Host timing may excuse only the check that the run's evidence explains.

    strict() runs every check a slow host cannot break and returns the run's
    state. Its failure fails, slow host or not. Each Timed in timed then
    runs on the state. A failed check whose own evidence is None fails the
    run, whatever the other checks' evidence says. When every failed check
    has its evidence, raises CannotJudge. Returns the state.
    """
    state = strict()
    excused = []
    for t in timed:
        try:
            t.check(state)
        except AssertionError as e:
            reason = t.evidence(state)
            if not reason:
                raise
            excused.append(f"{t.name}: {reason}; the check said: {e}")
    if excused:
        raise CannotJudge("; ".join(excused))
    return state


def judged_split(strict, *timed, tries=TRIES):
    """judged() over split_checks(): reruns a run that cannot judge, then skips."""
    return judged(lambda: split_checks(strict, *timed), tries)


def gap_too_long(gap_ms, limit_ms):
    """True when a measured gap may have reached limit_ms: the ROM ran too late."""
    return gap_ms + TICK_MS >= limit_ms


def chain_align(got, want, end, fresh):
    """Lines a chunk chain up with want: one (j, i, dry) per want item.

    got and want are marker keys. A slow host can make a chunk run dry
    before the next is queued: the kernel then ends the sound and starts the
    next chunk afresh. So a want item w may appear in got as end then
    fresh(w), where fresh(w) is the fresh start of the same chunk, or None
    when w cannot underrun. Nothing else may differ: every chunk appears
    once, in order, with its own length and CRC. j is the want index, i the
    got index of w, dry the got index of the end before a fresh start, or
    None. Raises AssertionError.
    """
    out = []
    i = 0
    for j, w in enumerate(want):
        if i < len(got) and got[i] == w:
            out.append((j, i, None))
            i += 1
            continue
        f = fresh(w)
        if f is not None and got[i:i + 2] == [end, f]:
            out.append((j, i + 1, i))
            i += 2
            continue
        raise AssertionError(f"marker {i} is {got[i:i + 2]}, want {w} (item {j}): {got}")
    assert i == len(got), f"{len(got) - i} markers after the chain: {got}"
    return out


def chain_underruns(got, want, end, fresh):
    """How many underruns a chunk chain had. Raises AssertionError: see chain_align()."""
    return sum(dry is not None for _, _, dry in chain_align(got, want, end, fresh))


def chain_late(times, align, chunk_ms):
    """Checks that each chunk plays in full and returns how late each switch came.

    times holds the got markers' t_ms. A chunk's start, or the end of a
    chunk that ran dry, comes k * chunk_ms after the time base at least,
    where k counts the chunks since the base. That holds on any host. The
    base is the first start, and a fresh start after an underrun starts a
    new one: the wait for the late queue call is the underrun's, not a late
    switch. Returns (got index, ms past k * chunk_ms) for each switch.
    """
    base_j, base_t = align[0][0], times[align[0][1]]
    late = []
    for j, i, dry in align[1:]:
        k = j - base_j
        at = i if dry is None else dry
        dt = times[at] - base_t
        assert dt >= chunk_ms * k, f"marker {at} came {dt} ms after the base, a chunk plays " \
                                   f"{chunk_ms} ms: {k} chunks need {chunk_ms * k} ms: {times}"
        late.append((at, dt - chunk_ms * k))
        if dry is not None:
            base_j, base_t = j, times[i]
    return late


class Chain:
    """What chain_align() and chain_late() found in a chunk chain."""

    def __init__(self, got, align, late):
        self.got, self.align, self.late = got, align, late
        self.underruns = sum(dry is not None for _, _, dry in align)
        self.worst = max((ms for _, ms in late), default=0)


def judge_chain(got, times, want, end, fresh, chunk_ms, slack_ms, gap_ms, where):
    """Judges a chunk chain from its marker keys and times.

    Strict in every run: chain_align() and chain_late(), so every chunk
    appears once, in order, with its length and CRC, and plays in full.
    Timed, each with its own evidence:
    - no underrun: excused when the ROM's longest gap between queue calls,
      gap_ms, may have reached a chunk. A chunk runs dry only when the next
      queue call comes later than the chunk lasts.
    - no switch more than slack_ms late: excused when the gap reached a
      chunk and also the switch's own lateness. A host stall stops the
      whole guest, so a stall that made a switch late by L ms made the ROM's
      gap L ms long at least.
    where names the chain in the reasons.
    """
    def strict():
        align = chain_align(got, want, end, fresh)
        return Chain(got, align, chain_late(times, align, chunk_ms))

    slow = gap_too_long(gap_ms, chunk_ms)

    def no_underrun(c):
        assert c.underruns == 0, got

    def underrun_evidence(_):
        if not slow:
            return None
        return f"the ROM queued {gap_ms} ms apart at most {where}, a chunk lasts {chunk_ms} ms"

    def on_time(c):
        assert c.worst <= slack_ms, f"a switch came {c.worst} ms late, {slack_ms} ms allowed: " \
                                    f"{c.late}"

    def late_evidence(c):
        if not slow or not gap_too_long(gap_ms, c.worst):
            return None
        return f"the ROM queued {gap_ms} ms apart at most {where}, the latest switch came " \
               f"{c.worst} ms late"

    return split_checks(strict, Timed("underrun", no_underrun, underrun_evidence),
                        Timed("late switch", on_time, late_evidence))


def judge_stream(stream, tr, full, chunks, chunk_len, rate, crcs):
    """Judges test_stream_while_redrawing from its markers.

    stream is the ROM's ev=stream marker, tr the kernel's ev=audio markers,
    full the number of full panel sends, crcs the CRC-32 of each chunk.
    Raises AssertionError when the stream is wrong, CannotJudge when it is
    wrong in a way the run's evidence explains, and returns None when it
    passes.

    The counts are strict, checked first. The ROM presents one frame before
    each of its chunks - 1 queue calls, then at least one more while the
    last chunk plays, so chunks frames at least, on any host. Each frame
    changes every pixel, so its present sends a full frame; a retried present
    sends one more. Only an 8 s stall between the last queue call and the
    next status read could cut the last frame, and nothing measures that, so
    no evidence can excuse it.
    """
    frames = int(stream["frames"])
    assert frames >= chunks and full >= chunks, (frames, full)
    chunk_ms = chunk_len * 1000 // rate
    got = [(m["op"], m.get("crc")) for m in tr]
    want = [("pcm", crcs[0])] + [("pcm_next", c) for c in crcs[1:chunks]] + [("pcm_end", None)]
    judge_chain(got, [int(m["t_ms"]) for m in tr], want, ("pcm_end", None), restart, chunk_ms,
                500, int(stream["max_gap_ms"]),
                f"while streaming, longest present {stream['max_ms']} ms")


def restart(key):
    """The fresh start of a chained chunk's key, for chain_underruns()."""
    return ("pcm",) + key[1:] if key[0] == "pcm_next" else None


# ---- the session rule ----------------------------------------------------------
#
# A skip must not hide a failure. The session counts skips whose reason starts
# with PREFIX per board and fails when any board has more than
# --timing-skips-max, by default one. A skip belongs to the board parameter of
# its test. A test without one counts under NO_BOARD. tests/conftest.py loads
# this module as a plugin.

NO_BOARD = "no board"
DEFAULT_PER_BOARD = 1


def skip_reason(report):
    """The reason of a skipped test report, or None."""
    if not report.skipped:
        return None
    longrepr = report.longrepr
    if isinstance(longrepr, tuple) and len(longrepr) == 3:
        reason = longrepr[2]
    else:
        reason = str(longrepr)
    return reason[len("Skipped: "):] if reason.startswith("Skipped: ") else reason


def is_timing_skip(reason):
    return reason is not None and reason.startswith(PREFIX)


def timing_limit(option):
    """The most host timing skips one board may have: --timing-skips-max, else one."""
    return DEFAULT_PER_BOARD if option is None else option


def over_limit(skips, limit):
    """The boards with more than limit skips, from (board, nodeid, reason) skips."""
    counts = {}
    for board, _, _ in skips:
        counts[board] = counts.get(board, 0) + 1
    return {b: n for b, n in counts.items() if n > limit}


def pytest_addoption(parser):
    parser.addoption("--timing-skips-max", type=int, default=None, metavar="N",
                     help=f"fail the session when one board has more than N '{PREFIX}' "
                          f"skips (default: {DEFAULT_PER_BOARD})")


def pytest_configure(config):
    config.pluginmanager.register(TimingSkips(config), "hosttiming-skips")


class TimingSkips:
    """Counts the host timing skips of one session per board, and fails it past the limit."""

    def __init__(self, config):
        self.config = config
        self.skips = []
        self.boards = {}

    def limit(self):
        return timing_limit(self.config.getoption("--timing-skips-max"))

    def pytest_collection_finish(self, session):
        for item in session.items:
            spec = getattr(item, "callspec", None)
            if spec is not None and "board" in spec.params:
                self.boards[item.nodeid] = str(spec.params["board"])

    def pytest_runtest_logreport(self, report):
        reason = skip_reason(report)
        if is_timing_skip(reason):
            self.skips.append((self.boards.get(report.nodeid, NO_BOARD), report.nodeid, reason))

    def pytest_sessionfinish(self, session):
        if over_limit(self.skips, self.limit()) and session.exitstatus == 0:
            session.exitstatus = pytest.ExitCode.TESTS_FAILED

    def pytest_terminal_summary(self, terminalreporter):
        if not self.skips:
            return
        limit = self.limit()
        over = over_limit(self.skips, limit)
        terminalreporter.section("host timing skips")
        for board, nodeid, reason in self.skips:
            terminalreporter.write_line(f"{board}: {nodeid}: {reason}")
        if over:
            boards = ", ".join(f"{b} has {n}" for b, n in sorted(over.items()))
            verdict = f"too many on {boards}, the session fails"
        else:
            verdict = "allowed"
        terminalreporter.write_line(
            f"{len(self.skips)} skips, at most {limit} per board allowed: {verdict}")
