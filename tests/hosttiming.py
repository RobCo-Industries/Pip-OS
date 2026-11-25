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
once. tests/conftest.py counts those skips and fails the session when there
are too many.
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


def strict_unless(slow, reason, check):
    """Runs check(). When slow is true and check() fails, raises CannotJudge.

    When slow is false the check stays as strict as it is: its failure fails.
    """
    try:
        return check()
    except AssertionError as e:
        if slow:
            raise CannotJudge(f"{reason}; the check said: {e}") from e
        raise


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


def judge_stream(stream, tr, chunks, chunk_len, rate, crcs):
    """Judges test_stream_while_redrawing from its markers.

    stream is the ROM's ev=stream marker, tr the kernel's ev=audio markers,
    crcs the CRC-32 of each chunk. Raises AssertionError when the stream is
    wrong, CannotJudge when it is wrong but the ROM queued too late for the
    claim to hold, and returns None when it passes.
    """
    chunk_ms = chunk_len * 1000 // rate
    gap = int(stream["max_gap_ms"])
    # Whatever the host did, every chunk the kernel started is the right one,
    # in order, and the stream ends.
    started = [m for m in tr if m["op"] != "pcm_end"]
    assert [m["crc"] for m in started] == crcs[:len(started)], tr
    assert tr and tr[-1]["op"] == "pcm_end", tr
    slow = gap_too_long(gap, chunk_ms)
    reason = (f"the ROM queued {gap} ms apart at most, a chunk lasts {chunk_ms} ms, "
              f"longest present {stream['max_ms']} ms")

    def check():
        ops = [m["op"] for m in tr]
        assert ops == ["pcm"] + ["pcm_next"] * (chunks - 1) + ["pcm_end"], ops
        # Each chunk plays in full, the next right after it: no gap, no restart.
        t = [int(m["t_ms"]) for m in tr]
        for i in range(1, chunks + 1):
            assert chunk_ms * i <= t[i] - t[0] <= chunk_ms * i + 500, (i, t)

    strict_unless(slow, reason, check)


# ---- the session rule ----------------------------------------------------------
#
# A skip must not hide a failure. The session counts skips whose reason starts
# with PREFIX and fails when there are more than --timing-skips-max, by default
# one per board in the session. tests/conftest.py loads this module as a plugin.

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


def timing_limit(option, boards):
    """--timing-skips-max when given, else one per board."""
    return option if option is not None else len(boards)


def pytest_addoption(parser):
    parser.addoption("--timing-skips-max", type=int, default=None, metavar="N",
                     help=f"fail the session after more than N '{PREFIX}' skips "
                          "(default: one per board in the session)")


def pytest_configure(config):
    config.pluginmanager.register(TimingSkips(config), "hosttiming-skips")


class TimingSkips:
    """Counts the host timing skips of one session and fails it past the limit."""

    def __init__(self, config):
        self.config = config
        self.skips = []
        self.boards = set()

    def limit(self):
        return timing_limit(self.config.getoption("--timing-skips-max"), self.boards)

    def pytest_collection_finish(self, session):
        for item in session.items:
            spec = getattr(item, "callspec", None)
            if spec is not None and "board" in spec.params:
                self.boards.add(spec.params["board"])

    def pytest_runtest_logreport(self, report):
        reason = skip_reason(report)
        if is_timing_skip(reason):
            self.skips.append((report.nodeid, reason))

    def pytest_sessionfinish(self, session):
        if len(self.skips) > self.limit() and session.exitstatus == 0:
            session.exitstatus = pytest.ExitCode.TESTS_FAILED

    def pytest_terminal_summary(self, terminalreporter):
        if not self.skips:
            return
        limit = self.limit()
        terminalreporter.section("host timing skips")
        for nodeid, reason in self.skips:
            terminalreporter.write_line(f"{nodeid}: {reason}")
        verdict = "too many, the session fails" if len(self.skips) > limit else "allowed"
        terminalreporter.write_line(
            f"{len(self.skips)} skips, at most {limit} allowed: {verdict}")


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
