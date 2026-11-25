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

Host timing may excuse only the checks that host timing can break. A test
states them apart with split_checks() or judged_split(): strict checks that
fail in every run, timed checks, and the run's evidence of a slow host.
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


def split_checks(strict, timed, evidence):
    """One run of a test that host timing can break. The only way to judge one.

    Host timing may excuse only the checks that host timing can break.

    strict() runs every check a slow host cannot break and returns the run's
    state. Its failure fails, slow host or not. timed(state) runs the checks
    a slow host can break. evidence(state) returns the run's own reason to
    think the host was too slow, or None. A timed failure with evidence
    raises CannotJudge; without evidence it fails. Returns the state.
    """
    state = strict()
    try:
        timed(state)
    except AssertionError as e:
        reason = evidence(state)
        if not reason:
            raise
        raise CannotJudge(f"{reason}; the check said: {e}") from e
    return state


def judged_split(strict, timed, evidence, tries=TRIES):
    """judged() over split_checks(): reruns a run that cannot judge, then skips."""
    return judged(lambda: split_checks(strict, timed, evidence), tries)


def gap_too_long(gap_ms, limit_ms):
    """True when a measured gap may have reached limit_ms: the ROM ran too late."""
    return gap_ms + TICK_MS >= limit_ms


def spi_retried(markers):
    """The retried sends that follow a present failure, as the kernel marks them.

    The kernel retries a failed present once, with the whole frame, and prints
    ev=display_error op=present before the retry. In QEMU the only way a
    present fails is the SPI deadline, which a stalled host expires.
    """
    failed = any(m.get("ev") == "display_error" and m.get("op") == "present" for m in markers)
    retries = [m for m in markers if m.get("ev") == "tft_send" and m.get("why") == "retry"]
    return retries if failed else []


def chain_underruns(got, want, end, fresh):
    """Checks a chunk chain against want and returns how many underruns it had.

    got and want are marker keys. A slow host can make a chunk run dry
    before the next is queued: the kernel then ends the sound and starts the
    next chunk afresh. So a want item w may appear in got as end then
    fresh(w), where fresh(w) is the fresh start of the same chunk, or None
    when w cannot underrun. Nothing else may differ: every chunk appears
    once, in order, with its own length and CRC. Raises AssertionError.
    """
    i = n = 0
    for j, w in enumerate(want):
        if i < len(got) and got[i] == w:
            i += 1
            continue
        f = fresh(w)
        if f is not None and got[i:i + 2] == [end, f]:
            i += 2
            n += 1
            continue
        raise AssertionError(f"marker {i} is {got[i:i + 2]}, want {w} (item {j}): {got}")
    assert i == len(got), f"{len(got) - i} markers after the chain: {got}"
    return n


def judge_chain(got, want, end, fresh, spans, evidence):
    """Judges a chunk chain from its marker keys.

    Strict in every run: chain_underruns(). Timed: no underrun, so got is
    want, and spans(), the chain's time checks. evidence is the run's slow
    host reason, or None.
    """
    def timed(_):
        assert got == want, got
        spans()

    split_checks(lambda: chain_underruns(got, want, end, fresh), timed, lambda _: evidence)


def judge_stream(stream, tr, chunks, chunk_len, rate, crcs):
    """Judges test_stream_while_redrawing from its markers.

    stream is the ROM's ev=stream marker, tr the kernel's ev=audio markers,
    crcs the CRC-32 of each chunk. Raises AssertionError when the stream is
    wrong, CannotJudge when it is wrong in a way a slow host explains and the
    ROM queued too late for the claim to hold, and returns None when it passes.
    """
    chunk_ms = chunk_len * 1000 // rate
    gap = int(stream["max_gap_ms"])
    got = [(m["op"], m.get("crc")) for m in tr]
    want = [("pcm", crcs[0])] + [("pcm_next", c) for c in crcs[1:chunks]] + [("pcm_end", None)]
    evidence = None
    if gap_too_long(gap, chunk_ms):
        evidence = (f"the ROM queued {gap} ms apart at most, a chunk lasts {chunk_ms} ms, "
                    f"longest present {stream['max_ms']} ms")

    def spans():
        # Each chunk plays in full, the next right after it.
        t = [int(m["t_ms"]) for m in tr]
        for i in range(1, chunks + 1):
            assert chunk_ms * i <= t[i] - t[0] <= chunk_ms * i + 500, (i, t)

    judge_chain(got, want, ("pcm_end", None), restart, spans, evidence)


def restart(key):
    """The fresh start of a chained chunk's key, for chain_underruns()."""
    return ("pcm",) + key[1:] if key[0] == "pcm_next" else None


def retry_evidence(vms):
    """The slow host reason of a panel run: SPI sends the kernel retried, or None."""
    retried = [m for vm in vms for m in spi_retried(vm.markers())]
    if not retried:
        return None
    return f"{len(retried)} SPI sends retried after the deadline"


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


# ---- raspi3b's spare cores ------------------------------------------------------------
#
# QEMU's raspi3b starts all four cores in AArch64 EL3. The harness points only
# CPU0 at its AArch64 shim. Cores 1 to 3 start at 0x0, run the AArch32 firmware
# stub as AArch64 code and loop through the EL3 exception vector at 0x200
# forever: three host threads busy, each exception taken under QEMU's big lock.
# The kernel's polled SPI takes that lock on every register access, so a full
# present ran 3 s in guest time on a fast Mac, 8.8 s on a GitHub runner, for
# about 128 ms on the wire. These words park cores 1 to 3 in WFI at reset.
PARK_ADDR = 0x80020
PARK_WORDS = (0xD503207F, 0x17FFFFFF)  # wfi; b .-4


def park_args(board):
    """QEMU arguments that park raspi3b's cores 1 to 3. None on other boards."""
    if board != "raspi3b":
        return []
    args = []
    for i, w in enumerate(PARK_WORDS):
        args += ["-device", f"loader,addr={PARK_ADDR + 4 * i:#x},data={w:#010x},data-len=4"]
    for cpu in (1, 2, 3):
        args += ["-device", f"loader,addr={PARK_ADDR:#x},cpu-num={cpu}"]
    return args
