"""Unit tests for tests/hosttiming.py: the host timing judgement and the session rule.

They need no build and no QEMU. The stream cases feed judge_stream() the
markers of three runs: the failing GitHub CI run on raspi3b, and two runs
with normal timing. The session rule cases run pytest on small test files
in the pytest temp folder.
"""

import os
import subprocess
import sys
import zlib

import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TESTS = os.path.join(ROOT, "tests")
sys.path.insert(0, os.path.join(ROOT, "external", "robco-core", "tests"))
sys.path.insert(0, TESTS)
import hosttiming  # noqa: E402
from hosttiming import (CannotJudge, Timed, chain_underruns, judge_chain, judge_stream,  # noqa: E402
                        judged, split_checks)
from panelsends import (FULL_BYTES, H, W, both_sends, judged_sends, retried_bytes,  # noqa: E402
                        window_sends, without_retries)
from harness.env import minimal_env  # noqa: E402

# tests/roms/tft_stream.c
CHUNKS, CHUNK_LEN, RATE = 3, 64000, 8000
CHUNK_MS = CHUNK_LEN * 1000 // RATE
CRCS = [f"{zlib.crc32(bytes((k * 7 + i * 31) & 0xFF for k in range(CHUNK_LEN))):08x}"
        for i in range(CHUNKS)]


def start(i, t):
    return {"op": "pcm" if i == 0 else "pcm_next", "crc": CRCS[i], "t_ms": str(t)}


def restart(i, t):
    return {"op": "pcm", "crc": CRCS[i], "t_ms": str(t)}


def end(t):
    return {"op": "pcm_end", "t_ms": str(t)}


def good_trace(t0=1000):
    return [start(i, t0 + i * CHUNK_MS + 3) for i in range(CHUNKS)] + \
        [end(t0 + CHUNKS * CHUNK_MS + 5)]


# The CI run: each chunk ran dry before the ROM queued the next, so the
# stream restarted twice. The ROM measured a present of 8840 ms. That run
# had no max_gap_ms yet; a gap holds a whole present, so it was 8840 ms at
# least.
CI_STREAM = {"frames": "3", "max_ms": "8840", "max_gap_ms": "8840"}
CI_TRACE = [restart(0, 1000), end(9000), restart(1, 9850), end(17850),
            restart(2, 18700), end(26700)]
CI_OPS = [("pcm", CRCS[0]), ("pcm_end", None), ("pcm", CRCS[1]), ("pcm_end", None),
          ("pcm", CRCS[2]), ("pcm_end", None)]
NORMAL_STREAM = {"frames": "190", "max_ms": "128", "max_gap_ms": "140"}


def judge(stream, tr, full=CHUNKS):
    return judge_stream(stream, tr, full, CHUNKS, CHUNK_LEN, RATE, CRCS)


# ---- the stream judgement ------------------------------------------------------------

def test_ci_run_cannot_judge():
    with pytest.raises(CannotJudge) as e:
        judge(CI_STREAM, CI_TRACE)
    assert "8840 ms" in str(e.value) and f"{CHUNK_MS} ms" in str(e.value), e.value


def test_ci_run_without_the_judgement_fails_as_before(monkeypatch):
    monkeypatch.setattr(hosttiming, "gap_too_long", lambda gap, limit: False)
    with pytest.raises(AssertionError) as e:
        judge(CI_STREAM, CI_TRACE)
    assert str(e.value).startswith(str(CI_OPS)), e.value


def test_normal_timing_with_wrong_ops_fails():
    with pytest.raises(AssertionError) as e:
        judge(NORMAL_STREAM, CI_TRACE)
    assert str(e.value).startswith(str(CI_OPS)), e.value


def test_normal_timing_with_right_ops_passes():
    assert judge(NORMAL_STREAM, good_trace()) is None


def test_normal_timing_with_a_late_switch_fails():
    tr = good_trace()
    tr[1]["t_ms"] = str(1000 + CHUNK_MS + 600)
    with pytest.raises(AssertionError):
        judge(NORMAL_STREAM, tr)


def test_gap_one_tick_short_of_a_chunk_cannot_judge():
    # ticks_ms can read one tick short, so 7990 ms may have been 8000 ms.
    with pytest.raises(CannotJudge):
        judge(dict(CI_STREAM, max_gap_ms=str(CHUNK_MS - hosttiming.TICK_MS)), CI_TRACE)
    with pytest.raises(AssertionError):
        judge(dict(CI_STREAM, max_gap_ms=str(CHUNK_MS - hosttiming.TICK_MS - 1)), CI_TRACE)


def test_slow_run_still_fails_on_wrong_data():
    tr = [dict(m) for m in CI_TRACE]
    tr[2]["crc"] = CRCS[2]
    with pytest.raises(AssertionError):
        judge(CI_STREAM, tr)


def test_slow_run_still_fails_on_a_repeated_chunk():
    tr = CI_TRACE[:2] + [restart(0, 9850), end(17850)] + CI_TRACE[2:]
    with pytest.raises(AssertionError):
        judge(CI_STREAM, tr)


def test_slow_run_still_fails_without_an_end():
    with pytest.raises(AssertionError):
        judge(CI_STREAM, CI_TRACE[:-1])


def test_slow_run_with_too_few_full_sends_fails():
    # The underrun has its evidence, the counts do not depend on it.
    with pytest.raises(AssertionError) as e:
        judge(CI_STREAM, CI_TRACE, full=CHUNKS - 1)
    assert str(e.value).startswith(str((3, CHUNKS - 1))), e.value
    with pytest.raises(AssertionError):
        judge(dict(CI_STREAM, frames=str(CHUNKS - 1)), CI_TRACE)


def test_slow_run_that_streamed_well_passes():
    assert judge(CI_STREAM, good_trace()) is None


# ---- judged() --------------------------------------------------------------------------

def test_judged_returns_the_first_run_that_judges():
    runs = []

    def attempt():
        runs.append(1)
        if len(runs) < 3:
            raise CannotJudge(f"slow {len(runs)}")
        return "done"

    assert judged(attempt) == "done" and len(runs) == 3


def test_judged_skips_after_four_runs_with_the_prefix():
    runs = []

    def attempt():
        runs.append(1)
        raise CannotJudge(f"slow {len(runs)}")

    with pytest.raises(pytest.skip.Exception) as e:
        judged(attempt)
    assert len(runs) == 4
    assert str(e.value).startswith("host timing: 4 runs, slow 1; slow 2; slow 3; slow 4"), e.value


def test_judged_does_not_retry_a_failure():
    runs = []

    def attempt():
        runs.append(1)
        assert False, "wrong"

    with pytest.raises(AssertionError):
        judged(attempt)
    assert len(runs) == 1


# ---- split_checks() ---------------------------------------------------------------------

def fails(msg):
    def check(*_):
        assert False, msg
    return check


def test_split_checks_never_excuses_a_strict_failure():
    with pytest.raises(AssertionError):
        split_checks(fails("strict"), Timed("t", lambda _: None, lambda _: "slow"))


def test_split_checks_excuses_a_timed_failure_only_with_evidence():
    with pytest.raises(CannotJudge) as e:
        split_checks(lambda: 1, Timed("t", fails("timed"), lambda state: f"slow {state}"))
    assert str(e.value).startswith("t: slow 1; the check said: timed"), e.value
    with pytest.raises(AssertionError):
        split_checks(lambda: 1, Timed("t", fails("timed"), lambda _: None))
    assert split_checks(lambda: 2, Timed("t", lambda _: None, lambda _: "slow")) == 2


def test_evidence_of_one_check_does_not_excuse_another():
    explained = Timed("underrun", fails("underrun"), lambda _: "slow")
    other = Timed("late", fails("late"), lambda _: None)
    for order in ((explained, other), (other, explained)):
        with pytest.raises(AssertionError) as e:
            split_checks(lambda: 1, *order)
        assert str(e.value).startswith("late"), e.value
    passing = Timed("late", lambda _: None, lambda _: None)
    with pytest.raises(CannotJudge):
        split_checks(lambda: 1, explained, passing)


# ---- the panel tests: the kernel's retries ----------------------------------------------

FULL = (0, 0, W, H)
ERR = {"stage": "pipos", "ev": "display_error", "op": "present", "rc": "-4"}


def send(why, r, n):
    x, y, w, h = r
    return {"stage": "pipos", "ev": "tft_send", "why": why, "x": str(x), "y": str(y),
            "w": str(w), "h": str(h), "bytes": str(n), "crc": "0"}


def rom(ev, n=None):
    m = {"stage": "testrom", "ev": ev}
    if n is not None:
        m["n"] = str(n)
    return m


def retry(panel=(W, H)):
    return [ERR, send("retry", (0, 0) + panel, 11 + panel[0] * panel[1] * 2)]


def both_run(small=((101, 57, 3, 2), 23), boot_retry=False, step2_retry=False):
    """The markers of test_both_panel_matches_hdmi; small is step 2's (rect, bytes)."""
    full = send("full", FULL, 11 + FULL_BYTES)
    out = [send("full", FULL, 4000)] + retry() if boot_retry else [full]
    out += [rom("shown"), rom("step", 1), send("diff", (0, 0, 0, 0), 0), rom("shown"),
            rom("step", 2), send("diff", *small)]
    if step2_retry:
        out += retry()
    out += [rom("shown"), rom("step", 3), full, rom("shown"), full,
            rom("step", 4), send("diff", (0, 0, 0, 0), 0), rom("shown")]
    return out


def judge_sends(check, markers):
    return split_checks(lambda: check(markers), retried_bytes())


def test_panel_runs_without_or_with_a_boot_retry_pass():
    judge_sends(both_sends, both_run())
    judge_sends(both_sends, both_run(boot_retry=True))


def test_boot_retry_with_a_wrong_rectangle_later_fails():
    with pytest.raises(AssertionError) as e:
        judge_sends(both_sends, both_run(((101, 57, 4, 2), 27), boot_retry=True))
    assert "'w': '4'" in str(e.value), e.value


def test_boot_retry_with_a_wrong_byte_count_later_fails():
    with pytest.raises(AssertionError) as e:
        judge_sends(both_sends, both_run(((101, 57, 3, 2), 24), boot_retry=True))
    assert "the small change" in str(e.value), e.value
    with pytest.raises(AssertionError):
        judged_sends(lambda: both_sends(both_run(((101, 57, 3, 2), 24), boot_retry=True)))


def test_retry_in_the_step_with_the_right_contents_cannot_judge():
    # The deadline cut the small change short, the kernel sent the whole frame again.
    run = both_run(((101, 57, 3, 2), 11), step2_retry=True)
    with pytest.raises(CannotJudge) as e:
        judge_sends(both_sends, run)
    assert str(e.value).startswith("bytes of a retried present: retry 1 after (101, 57, 3, 2)"), \
        e.value
    with pytest.raises(pytest.skip.Exception) as e:
        judged_sends(lambda: both_sends(run))
    assert str(e.value).startswith("host timing: 4 runs, bytes of a retried present"), e.value
    # An exact count needs no excuse.
    judge_sends(both_sends, both_run(step2_retry=True))


def test_retry_in_the_step_with_a_wrong_rectangle_fails():
    with pytest.raises(AssertionError):
        judge_sends(both_sends, both_run(((101, 57, 4, 2), 11), step2_retry=True))


def test_retry_in_the_window_step():
    def run(n):
        return [rom("step", 0), send("full", (0, 0, H, H), 11 + H * H * 2), rom("shown"),
                rom("step", 1), send("diff", (0, 0, 0, 0), 0), rom("shown"),
                rom("step", 2), send("diff", (10, 20, 1, 1), n)] + retry((H, H)) + [rom("shown")]

    with pytest.raises(CannotJudge):
        judge_sends(window_sends, run(5))
    judge_sends(window_sends, run(13))
    with pytest.raises(AssertionError):
        judge_sends(window_sends, run(5)[:-3] + [rom("shown")])


def test_retry_rules():
    full = send("full", FULL, 11 + FULL_BYTES)
    # A retry needs the error first, then a send before it, and covers the panel.
    with pytest.raises(AssertionError):
        without_retries([full, retry()[1]])
    with pytest.raises(AssertionError):
        without_retries(retry())
    with pytest.raises(AssertionError):
        without_retries([full, ERR, send("retry", (0, 0, 10, 10), 211)])
    with pytest.raises(AssertionError):
        without_retries([full, ERR, send("retry", FULL, 100)])
    with pytest.raises(AssertionError):
        without_retries([full] + retry() + [retry()[1]])
    # The kernel prints the error once: a later retry has none of its own.
    out = without_retries([full] + retry() + [full, retry()[1]])
    assert [m.get("retried") for m in out if m["ev"] == "tft_send"] == \
        [f"retry 1 after {FULL}", f"retry 2 after {FULL}"], out


# ---- the long sample_queue chain ---------------------------------------------------------
#
# Keys as tests/test_audio_queue.py builds them: pcm (len, vol, crc), pcm_next (len, crc).

LONG = [("pcm", 2000, 70, "c0"), ("pcm_next", 2000, "c1"), ("pcm_next", 2000, "c2"),
        ("pcm_end",)]
END = ("pcm_end",)


def fresh(k):
    return ("pcm", k[1], 70, k[2]) if k[0] == "pcm_next" else None


def underrun_trace():
    return [LONG[0], END, ("pcm", 2000, 70, "c1"), LONG[2], END]


CHUNK = 250
SLOW, FAST = CHUNK, 40


def times(got, late=()):
    """On-time t_ms for got: a chunk every CHUNK ms, a fresh start 100 ms after its end."""
    t, out = 1000, []
    for i, k in enumerate(got):
        if i:
            t += 100 if got[i - 1] == END else CHUNK
        out.append(t)
    for i, ms in late:
        out[i] += ms
    return out


def judge_long(got, gap, tr=None):
    judge_chain(got, tr or times(got), LONG, END, fresh, CHUNK, 500, gap, "in the chain")


def test_chain_counts_underruns():
    assert chain_underruns(LONG, LONG, END, fresh) == 0
    assert chain_underruns(underrun_trace(), LONG, END, fresh) == 1


def test_slow_chain_with_only_an_underrun_cannot_judge():
    with pytest.raises(CannotJudge):
        judge_long(underrun_trace(), SLOW)


def test_chain_with_an_underrun_and_no_evidence_fails():
    with pytest.raises(AssertionError):
        judge_long(underrun_trace(), FAST)


def test_slow_chain_with_a_wrong_crc_fails():
    tr = underrun_trace()
    tr[3] = ("pcm_next", 2000, "bad")
    with pytest.raises(AssertionError):
        judge_long(tr, SLOW)


def test_slow_chain_with_a_wrong_length_fails():
    tr = list(LONG)
    tr[1] = ("pcm_next", 1999, "c1")
    with pytest.raises(AssertionError):
        judge_long(tr, SLOW)


def test_slow_chain_out_of_order_or_repeated_fails():
    for tr in ([LONG[0], LONG[2], LONG[1], END],
               [LONG[0], LONG[1], LONG[1], LONG[2], END],
               [LONG[0], LONG[2], END],
               [LONG[0], END, ("pcm", 2000, 70, "c2"), END],
               LONG + [END]):
        with pytest.raises(AssertionError):
            judge_long(tr, SLOW)


def test_on_time_chains_pass():
    judge_long(LONG, FAST)
    assert times(LONG) == [1000, 1250, 1500, 1750]
    assert times(underrun_trace()) == [1000, 1250, 1350, 1600, 1850]


def test_slow_chain_with_a_late_switch_cannot_judge_only_with_a_gap_that_long():
    tr = times(LONG, [(2, 600), (3, 600)])
    with pytest.raises(CannotJudge):
        judge_long(LONG, 600, tr)
    with pytest.raises(AssertionError):
        judge_long(LONG, 500, tr)
    with pytest.raises(AssertionError):
        judge_long(LONG, FAST, tr)


def test_slow_chain_with_a_short_chunk_fails():
    # A chunk that stops early is never the host's doing.
    with pytest.raises(AssertionError):
        judge_long(LONG, SLOW, times(LONG, [(1, -20)]))
    got = underrun_trace()
    with pytest.raises(AssertionError):
        judge_long(got, SLOW, times(got, [(1, -20)]))


def test_slow_chain_with_an_underrun_and_a_late_switch_after_it():
    # The fresh start resets the time base: the wait for the queue is the underrun's.
    got = underrun_trace()
    with pytest.raises(CannotJudge):
        judge_long(got, SLOW, times(got, [(2, 5000), (3, 5000), (4, 5000)]))
    with pytest.raises(AssertionError):
        judge_long(got, SLOW, times(got, [(3, 600), (4, 600)]))


# ---- the session rule ----------------------------------------------------------------------

SESSION_TESTS = '''
import pytest

@pytest.mark.parametrize("board", ["raspi0", "raspi2b"])
@pytest.mark.parametrize("n", range(2))
def test_timing(board, n):
    if (board, n) in {skips}:
        pytest.skip("host timing: slow")

def test_noboard():
    if {noboard}:
        pytest.skip("host timing: no board")

def test_other():
    pytest.skip("no mtools")
'''


def session(tmp_path, skips, *args, noboard=False):
    """Runs pytest with the plugin on SESSION_TESTS: (exit code, output).

    skips: the (board, n) runs that skip on host timing.
    """
    (tmp_path / "pytest.ini").write_text("[pytest]\n")
    (tmp_path / "test_s.py").write_text(SESSION_TESTS.format(skips=set(skips), noboard=noboard))
    out = subprocess.run([sys.executable, "-m", "pytest", "-p", "hosttiming", "-p",
                          "no:cacheprovider", "-q", *args, str(tmp_path)],
                         cwd=tmp_path, capture_output=True, text=True,
                         env=minimal_env(PYTHONPATH=TESTS))
    return out.returncode, out.stdout + out.stderr


def test_session_allows_one_skip_on_each_board(tmp_path):
    code, out = session(tmp_path, [("raspi0", 0), ("raspi2b", 0)])
    assert code == 0, out
    assert "2 skips, at most 1 per board allowed: allowed" in out, out


def test_session_fails_on_two_skips_on_one_board(tmp_path):
    # Two boards in the session, but both skips are raspi0's.
    code, out = session(tmp_path, [("raspi0", 0), ("raspi0", 1)])
    assert code == 1, out
    assert "2 skips, at most 1 per board allowed: too many on raspi0 has 2" in out, out
    assert "raspi0: test_s.py::test_timing[0-raspi0]: host timing: slow" in out, out
    assert "no mtools" not in out.split("host timing skips")[1], out


def test_session_counts_a_skip_without_board_apart(tmp_path):
    code, out = session(tmp_path, [("raspi0", 0)], noboard=True)
    assert code == 0, out
    assert "no board: test_s.py::test_noboard: host timing: no board" in out, out


def test_session_limit_option_is_per_board(tmp_path):
    two = [("raspi0", 0), ("raspi0", 1)]
    code, out = session(tmp_path, two, "--timing-skips-max=2")
    assert code == 0, out
    assert "at most 2 per board allowed: allowed" in out, out
    # Three skips in all, two per board at most: a session total of 2 would fail it.
    code, out = session(tmp_path, two + [("raspi2b", 0)], "--timing-skips-max=2")
    assert code == 0, out
    code, out = session(tmp_path, [("raspi0", 0)], "--timing-skips-max=0")
    assert code == 1, out


def test_session_without_timing_skips_prints_nothing(tmp_path):
    code, out = session(tmp_path, [])
    assert code == 0 and "host timing skips" not in out, out
