"""No Pip-OS test value may hold a copy of the host environment.

pytest prints arguments and locals of failing frames, so a full copy of
os.environ would print every secret of the host. Tests build subprocess
environments with RobCo-Core's harness.env.minimal_env instead.
"""

import os
import re
import subprocess
import sys
import textwrap

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CORE_TESTS = os.path.join(ROOT, "external", "robco-core", "tests")
THIS = os.path.abspath(__file__)

FULL_COPY = re.compile(
    r"dict\(\s*os\.environ"
    r"|os\.environ\.copy\("
    r"|\*\*\s*os\.environ\b"
    r"|env\s*=\s*os\.environ\b"
    r"|=\s*os\.environ\s*$"
    r"|copy\.(deep)?copy\(\s*os\.environ"
    r"|(dict|Env)\(\s*os\.environ\.items\("
    r"|for\s+\w+\s*,\s*\w+\s+in\s+os\.environ\.items\(\)",
    re.M)

CANARY = "canary-12345"
SKIP_DIRS = {"__pycache__", "build", ".venv", ".git", "external"}


def sources():
    for top in ("tests", "tools", "conformance", "sdk"):
        for d, dirs, files in os.walk(os.path.join(ROOT, top)):
            dirs[:] = [x for x in dirs if x not in SKIP_DIRS]
            for f in files:
                if f.endswith((".py", ".sh")):
                    yield os.path.join(d, f)


def test_no_full_environment_copy():
    bad = []
    for path in sources():
        if path == THIS:
            continue
        with open(path, encoding="utf-8", errors="replace") as f:
            for n, line in enumerate(f, 1):
                if FULL_COPY.search(line):
                    bad.append(f"{os.path.relpath(path, ROOT)}:{n}")
    assert not bad, "copy of os.environ, use minimal_env(): " + ", ".join(bad)


def test_pattern_catches_the_old_forms():
    for line in ('e = dict(os.environ, A="1")', "e = os.environ.copy()",
                 "e = {**os.environ}", "run(x, env=os.environ)", "e = os.environ",
                 "e = {k: v for k, v in os.environ.items() if k not in drop}"):
        assert FULL_COPY.search(line), line
    assert not FULL_COPY.search('os.environ.get("PIPOS_UPDATE_GOLDEN")')


FAILING = """
import os
import sys
sys.path.insert(0, {tests!r})
from harness.env import minimal_env


def launch(cmd, env):
    assert cmd == "never", "fails on purpose"


def test_fails():
    env = {build}
    launch("echo", env)
"""


def leaks(tmp_path, build, *args):
    """Canary count in the output of a throwaway failing pytest run."""
    (tmp_path / "test_throwaway.py").write_text(
        textwrap.dedent(FAILING.format(tests=CORE_TESTS, build=build)))
    # Set only for the child. A name that sorts first shows even in a cut repr.
    env = {"A_PIPOS_LEAK_CANARY": CANARY, "PATH": os.environ.get("PATH", os.defpath)}
    r = subprocess.run([sys.executable, "-m", "pytest", "-p", "no:cacheprovider",
                        *args, str(tmp_path)],
                       cwd=tmp_path, env=env, capture_output=True, text=True)
    out = r.stdout + r.stderr
    assert r.returncode == 1 and "fails on purpose" in out, "the throwaway test must fail"
    return out.count(CANARY)


def test_minimal_env_does_not_leak(tmp_path):
    for args in ((), ("-vv",), ("--showlocals", "-vv")):
        n = leaks(tmp_path, 'minimal_env(MTOOLS_SKIP_CHECK="1")', *args)
        assert n == 0, f"canary leaked {n} times with {args}"


def test_full_copy_leaks(tmp_path):
    # Proves the check above can see a leak.
    assert leaks(tmp_path, "dict(os." + "environ)", "--showlocals", "-vv") > 0
