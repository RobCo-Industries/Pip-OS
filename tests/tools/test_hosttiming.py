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
from hosttiming import CannotJudge, judge_stream, judged, spi_retried  # noqa: E402
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
CI_OPS = ["pcm", "pcm_end", "pcm", "pcm_end", "pcm", "pcm_end"]
NORMAL_STREAM = {"frames": "190", "max_ms": "128", "max_gap_ms": "140"}


def judge(stream, tr):
    return judge_stream(stream, tr, CHUNKS, CHUNK_LEN, RATE, CRCS)


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


def test_slow_run_still_fails_without_an_end():
    with pytest.raises(AssertionError):
        judge(CI_STREAM, CI_TRACE[:-1])


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


# ---- spi_retried() ------------------------------------------------------------------------

def send(why):
    return {"stage": "pipos", "ev": "tft_send", "why": why}


def test_spi_retry_counts_only_after_a_present_failure():
    err = {"stage": "pipos", "ev": "display_error", "op": "present", "rc": "-4"}
    assert spi_retried([send("full"), err, send("retry")]) == [send("retry")]
    assert spi_retried([send("full"), send("retry")]) == []
    assert spi_retried([send("full"), err]) == []


# ---- the session rule ----------------------------------------------------------------------

SESSION_TESTS = '''
import pytest

@pytest.mark.parametrize("board", ["raspi0", "raspi2b"])
@pytest.mark.parametrize("n", range({timing}))
def test_timing(board, n):
    if board == "raspi0" or n == 0:
        pytest.skip("host timing: slow")

def test_other():
    pytest.skip("no mtools")
'''


def session(tmp_path, timing, *args):
    """Runs pytest with the plugin on SESSION_TESTS: (exit code, output)."""
    (tmp_path / "pytest.ini").write_text("[pytest]\n")
    (tmp_path / "test_s.py").write_text(SESSION_TESTS.format(timing=timing))
    out = subprocess.run([sys.executable, "-m", "pytest", "-p", "hosttiming", "-p",
                          "no:cacheprovider", "-q", *args, str(tmp_path)],
                         cwd=tmp_path, capture_output=True, text=True,
                         env=minimal_env(PYTHONPATH=TESTS))
    return out.returncode, out.stdout + out.stderr


def test_session_allows_one_skip_per_board(tmp_path):
    # n=0 on both boards: 2 skips, 2 boards.
    code, out = session(tmp_path, 1)
    assert code == 0, out
    assert "2 skips, at most 2 allowed: allowed" in out, out


def test_session_fails_past_one_skip_per_board(tmp_path):
    # raspi0 n=0,1 and raspi2b n=0: 3 skips, 2 boards. "no mtools" does not count.
    code, out = session(tmp_path, 2)
    assert code == 1, out
    assert "3 skips, at most 2 allowed: too many, the session fails" in out, out
    assert "test_s.py::test_timing[0-raspi0]: host timing: slow" in out, out
    assert "no mtools" not in out.split("host timing skips")[1], out


def test_session_limit_option(tmp_path):
    code, out = session(tmp_path, 2, "--timing-skips-max=3")
    assert code == 0, out
    code, out = session(tmp_path, 1, "--timing-skips-max=0")
    assert code == 1, out


def test_session_without_timing_skips_prints_nothing(tmp_path):
    code, out = session(tmp_path, 0)
    assert code == 0 and "host timing skips" not in out, out


# ---- park_args() ----------------------------------------------------------------------------

def test_park_args_only_on_raspi3b():
    assert hosttiming.park_args("raspi2b") == []
    args = hosttiming.park_args("raspi3b")
    assert "loader,addr=0x80020,data=0xd503207f,data-len=4" in args
    assert [a for a in args if "cpu-num" in a] == \
        [f"loader,addr=0x80020,cpu-num={n}" for n in (1, 2, 3)]
