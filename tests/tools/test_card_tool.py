"""Unit tests for tools/card.py, the card staging and bootdir tool.

They need no build. Each case runs on the file system of the pytest temp
folder. Cases that need two names equal when case is folded run only on a
case-sensitive one; cases that need FAT-like matching from the host run
only on a case-insensitive one. The `fs` fixture prints which branch ran.
"""

import contextlib
import importlib.util
import io
import os
import random
import stat
import subprocess
import sys

import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TOOL = os.path.join(ROOT, "tools", "card.py")
sys.path.insert(0, os.path.join(ROOT, "external", "robco-core", "tests"))
from harness.env import minimal_env  # noqa: E402

_spec = importlib.util.spec_from_file_location("card", TOOL)
card = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(card)

ROOT_USER = hasattr(os, "geteuid") and os.geteuid() == 0


# ---- helpers ---------------------------------------------------------------------

def run(*args):
    """card.main(args) in this process: (status, stdout, stderr)."""
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        status = card.main([str(a) for a in args])
    return status, out.getvalue(), err.getvalue()


def cli(*args):
    """The tool as the Makefile runs it."""
    r = subprocess.run([sys.executable, TOOL, *map(str, args)], env=minimal_env(),
                       capture_output=True, text=True)
    return r.returncode, r.stdout, r.stderr


def make(root, spec):
    """spec: {path: bytes | "dir" | ("link", target) | "fifo"}."""
    os.makedirs(root, exist_ok=True)
    for p, v in spec.items():
        path = os.path.join(root, p)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        if v == "dir":
            os.makedirs(path, exist_ok=True)
        elif v == "fifo":
            os.mkfifo(path)
        elif isinstance(v, tuple):
            os.symlink(v[1], path)
        else:
            with open(path, "wb") as f:
                f.write(v)


def snap(root):
    """Every path below root with its type and bytes or link target. Links
    are never followed."""
    out = {}
    if not os.path.lexists(root):
        return out
    for d, dirs, files in os.walk(root):
        for n in dirs + files:
            path = os.path.join(d, n)
            rel = os.path.relpath(path, root)
            st = os.lstat(path)
            if stat.S_ISLNK(st.st_mode):
                out[rel] = ("l", os.readlink(path))
            elif stat.S_ISDIR(st.st_mode):
                out[rel] = ("d",)
            elif stat.S_ISREG(st.st_mode):
                with open(path, "rb") as f:
                    out[rel] = ("f", f.read())
            else:
                out[rel] = ("s",)
    return out


def unlock(root):
    for d, dirs, _ in os.walk(root):
        for n in dirs:
            p = os.path.join(d, n)
            if not os.path.islink(p):
                os.chmod(p, 0o755)


@pytest.fixture(scope="module")
def fs(tmp_path_factory):
    t = tmp_path_factory.mktemp("case")
    (t / "a").write_bytes(b"")
    kind = "insensitive" if (t / "A").exists() else "sensitive"
    print(f"\ncard tool tests: temp folder is case-{kind} ({t})")
    return kind


@pytest.fixture
def canary(tmp_path):
    """A file and a folder outside every DIR, for links to point at."""
    c = tmp_path / "outside"
    make(str(c), {"CANARY.TXT": b"canary\n", "DIR/KEEP.TXT": b"keep\n"})
    before = snap(str(c))
    yield c
    assert snap(str(c)) == before, "a file outside DIR changed: a link was followed"


@pytest.fixture
def stage(tmp_path):
    s = str(tmp_path / "stage")
    make(s, {"PIPOS.IMG": b"kernel\n", "config.txt": b"cfg\n", "PIPBOY/PIPOS.CFG": b"settings\n",
             "PIPBOY/ROM1.ROM": b"rom\n", "PIPBOY/HOLO/ONE.HTP": b"tape\n"})
    return s


def stopped(status, err, dir_before, d, *lines):
    assert status == 1, err
    assert "Nothing was changed." in err, err
    for line in lines:
        assert line in err, err
    assert snap(d) == dir_before, "a stop changed DIR"


# ---- check-tapes -------------------------------------------------------------------

def test_tapes_case_and_suffix(tmp_path, fs):
    a, b = tmp_path / "a", tmp_path / "b"
    make(str(a), {"ONE.HTP": b"1"})
    make(str(b), {"one.htp": b"1", "NOTES.TXT": b"n", "LONGNAME1.HTP": b"l", "ABCDEFGH.HTP": b"x"})
    status, out, err = run("check-tapes", a / "ONE.HTP", b / "one.htp", b / "NOTES.TXT",
                           b / "LONGNAME1.HTP", b / "ABCDEFGH.HTP")
    assert status == 0
    assert f"TAPE names {a}/ONE.HTP and {b}/one.htp, the same file" in out
    assert f"TAPE file {b}/NOTES.TXT does not end in .HTP" in out
    assert "; " in out
    assert f"warning: TAPE file {b}/LONGNAME1.HTP is not a plain 8.3" in err
    assert "ABCDEFGH" not in err


def test_tapes_clean(tmp_path):
    make(str(tmp_path), {"ONE.HTP": b"1", "two.htp": b"2"})
    assert run("check-tapes", "--rom", tmp_path / "ONE.HTP", "--",
               tmp_path / "ONE.HTP", tmp_path / "two.htp") == (0, "", "")


@pytest.mark.parametrize("what", ["link", "dangling", "loop", "fifo"])
def test_tapes_and_rom_refuse_special_files(tmp_path, canary, what):
    v = {"link": ("link", str(canary / "CANARY.TXT")), "dangling": ("link", "NOPE"),
         "loop": ("link", "X.HTP"), "fifo": "fifo"}[what]
    make(str(tmp_path / "t"), {"X.HTP": v})
    p = tmp_path / "t" / "X.HTP"
    _, out, _ = run("check-tapes", p)
    assert f"TAPE file {p} is a " in out, out
    _, out, _ = run("check-tapes", "--rom", p)
    assert f"ROM file {p} is a " in out, out


def test_tapes_and_rom_refuse_folders(tmp_path):
    # Review finding P2: sdimage copied a folder source, bootdir's cp failed.
    make(str(tmp_path), {"X.HTP/A": b"a", "R.ROM/B": b"b"})
    _, out, _ = run("check-tapes", "--rom", tmp_path / "R.ROM", "--", tmp_path / "X.HTP")
    assert f"ROM file {tmp_path}/R.ROM is a folder: use a regular file" in out, out
    assert f"TAPE file {tmp_path}/X.HTP is a folder: use a regular file" in out, out


def test_tapes_missing_file_left_to_make(tmp_path):
    assert run("check-tapes", "--rom", tmp_path / "NONE.ROM", tmp_path / "NONE.HTP") == (0, "", "")


# ---- stage-card-dir -----------------------------------------------------------------

def test_card_dir_clashes(tmp_path, stage):
    src = str(tmp_path / "cd")
    make(src, {"pipboy/holo/one.htp": b"x", "Pipboy/X": b"x"})
    before = snap(stage)
    status, _, err = run("stage-card-dir", stage, src)
    assert status == 1
    assert "pipboy/holo/one.htp clashes with the standard PIPBOY/HOLO/ONE.HTP" in err \
        or "pipboy differs only in case from the standard PIPBOY" in err, err
    assert "nothing copied" in err
    assert snap(stage) == before


def test_card_dir_file_on_folder(tmp_path, stage):
    src = str(tmp_path / "cd")
    make(src, {"PIPBOY/HOLO": b"x"})
    status, _, err = run("stage-card-dir", stage, src)
    assert status == 1 and "PIPBOY/HOLO clashes with the standard PIPBOY/HOLO" in err, err


def test_card_dir_merges(tmp_path, stage):
    src = str(tmp_path / "cd")
    make(src, {"PIPBOY/HOLO/TWO.HTP": b"2", "DATA/LEVEL1.BIN": b"l"})
    assert run("stage-card-dir", stage, src)[0] == 0
    s = snap(stage)
    assert s["PIPBOY/HOLO/TWO.HTP"] == ("f", b"2") and s["DATA/LEVEL1.BIN"] == ("f", b"l")


@pytest.mark.parametrize("where,what", [("FILE.BIN", "file"), ("SUB", "folder"), ("D.BIN", "dangling"),
                                        ("L.BIN", "loop"), ("F.BIN", "fifo")])
def test_card_dir_refuses_links(tmp_path, stage, canary, where, what):
    src = str(tmp_path / "cd")
    v = {"file": ("link", str(canary / "CANARY.TXT")), "folder": ("link", str(canary / "DIR")),
         "dangling": ("link", "NOPE"), "loop": ("link", where), "fifo": "fifo"}[what]
    make(src, {"OK.TXT": b"ok", "DATA/" + where: v})
    before = snap(stage)
    status, _, err = run("stage-card-dir", stage, src)
    assert status == 1 and f"DATA/{where} (a " in err, err
    assert snap(stage) == before


def test_card_dir_root_may_be_a_link(tmp_path, stage):
    make(str(tmp_path / "real"), {"DATA/A.BIN": b"a"})
    os.symlink(tmp_path / "real", tmp_path / "cd")
    assert run("stage-card-dir", stage, tmp_path / "cd")[0] == 0
    assert snap(stage)["DATA/A.BIN"] == ("f", b"a")


@pytest.mark.parametrize("name", ["BOOTDIR.LST", "bootdir.lst", "BootDir.Lst"])
def test_card_dir_refuses_manifest_name(tmp_path, stage, fs, name):
    # Review finding P2: make bootdir replaced a supplied BOOTDIR.LST with
    # its manifest while make sdimage put it on the card.
    src = str(tmp_path / "cd")
    make(src, {name: b"mine\n", "DATA/BOOTDIR.LST": b"deeper is fine\n"})
    before = snap(stage)
    status, _, err = run("stage-card-dir", stage, src)
    assert status == 1, err
    assert f"{name}: the name BOOTDIR.LST is reserved for the boot folder manifest" in err, err
    assert "DATA/BOOTDIR.LST" not in err, err
    assert snap(stage) == before


def test_card_dir_supplied_settings_clash(tmp_path, stage):
    # PIPBOY/PIPOS.CFG is a standard file: a CARD_DIR copy stops sdimage
    # and bootdir alike, instead of being kept or dropped by P2.
    src = str(tmp_path / "cd")
    make(src, {"PIPBOY/PIPOS.CFG": b"mine\n"})
    status, _, err = run("stage-card-dir", stage, src)
    assert status == 1 and "PIPBOY/PIPOS.CFG clashes with the standard PIPBOY/PIPOS.CFG" in err, err


# ---- check-stage ---------------------------------------------------------------------

@pytest.mark.parametrize("spec", [{"bootdir.lst": b"x"}, {"BOOTDIR.LST/X": b"x"}])
def test_stage_refuses_manifest_name(tmp_path, spec):
    s = str(tmp_path / "s")
    make(s, dict(spec, **{"PIPOS.IMG": b"k", "DATA/BOOTDIR.LST": b"ok"}))
    status, _, err = run("check-stage", s)
    assert status == 1, err
    assert "the name BOOTDIR.LST is reserved for the boot folder manifest" in err, err
    assert "DATA/BOOTDIR.LST" not in err, err
    d = str(tmp_path / "card")
    status, _, err = run("sync-bootdir", s, d)
    assert status == 1 and "reserved for the boot folder manifest" in err, err
    assert not os.path.exists(d)


def test_stage_case_pairs(tmp_path, fs):
    if fs != "sensitive":
        pytest.skip("case-insensitive temp folder: it cannot hold NAME.TXT and name.txt")
    s = str(tmp_path / "s")
    make(s, {"NAME.TXT": b"1", "name.txt": b"2", "DATA/X": b"x", "Data/X": b"y",
             "LEVELS/ONE.LVL": b"l", "levels": b"f"})
    status, _, err = run("check-stage", s)
    assert status == 1
    assert "file NAME.TXT and file name.txt" in err
    assert "folder DATA and folder Data" in err
    assert "folder LEVELS and file levels" in err
    assert "DATA/X" not in err, "listed a path below a listed folder"
    assert "differ only in case" in err and "Nothing was written" in err


def test_stage_warns_long_names(tmp_path):
    s, cd = str(tmp_path / "s"), str(tmp_path / "cd")
    make(cd, {"data/level1.bin": b"", "data/LongLevelName.bin": b""})
    make(s, {"PIPBOY/ROM1.ROM": b"", "data/level1.bin": b"", "data/LongLevelName.bin": b""})
    status, _, err = run("check-stage", s, cd)
    assert status == 0, err
    assert "warning: CARD_DIR path data/LongLevelName.bin is not made of 8.3 names" in err
    assert "level1.bin" not in err


def test_stage_refuses_links(tmp_path, canary):
    s = str(tmp_path / "s")
    make(s, {"PIPBOY/ROM1.ROM": ("link", str(canary / "CANARY.TXT"))})
    status, _, err = run("check-stage", s)
    assert status == 1 and "PIPBOY/ROM1.ROM (a symbolic link)" in err, err


# ---- sync-bootdir: basics ----------------------------------------------------------

def test_first_run_and_again(tmp_path, stage):
    d = str(tmp_path / "card")
    assert run("sync-bootdir", stage, d)[0] == 0
    s = snap(d)
    assert s["BOOTDIR.LST"] == ("f", b"PIPBOY/HOLO/ONE.HTP\nPIPBOY/ROM1.ROM\nPIPOS.IMG\nconfig.txt\n")
    assert s["PIPBOY/PIPOS.CFG"] == ("f", b"settings\n")
    with open(os.path.join(d, "PIPBOY/PIPOS.CFG"), "wb") as f:
        f.write(b"edited\n")
    make(d, {"NOTES.TXT": b"mine"})
    before = snap(d)
    status, _, err = run("sync-bootdir", stage, d)
    assert status == 0 and "NOTES.TXT" in err
    assert snap(d) == before
    assert not [n for n in os.listdir(os.path.join(d, "PIPBOY")) if n.startswith(".bootdir-")]


def test_temporary_names_never_clobber(tmp_path, stage):
    # put() picks each temporary name with mkstemp (O_EXCL), so a file
    # named like one, staged or already in DIR, is never overwritten, and
    # no temporary file is left behind.
    make(stage, {"PIPBOY/.bootdir-staged": b"staged\n"})
    d = str(tmp_path / "card")
    make(d, {"PIPBOY/.bootdir-mine": b"mine\n", ".bootdir-top": b"top\n"})
    # Force the random part to the names already there, to make mkstemp
    # meet each of them.
    names = iter(["mine", "top", "staged"] * 20)
    real = card.tempfile._RandomNameSequence.__next__
    card.tempfile._RandomNameSequence.__next__ = lambda self: next(names, None) or real(self)
    try:
        status, _, err = run("sync-bootdir", stage, d)
    finally:
        card.tempfile._RandomNameSequence.__next__ = real
    assert status == 0, err
    s = snap(d)
    assert s["PIPBOY/.bootdir-mine"] == ("f", b"mine\n") and s[".bootdir-top"] == ("f", b"top\n")
    assert s["PIPBOY/.bootdir-staged"] == ("f", b"staged\n")
    assert sorted(p for p in s if ".bootdir-" in p) == [".bootdir-top", "PIPBOY/.bootdir-mine",
                                                        "PIPBOY/.bootdir-staged"]


def test_cli_wiring(tmp_path, stage):
    d = str(tmp_path / "card")
    status, _, err = cli("sync-bootdir", stage, d, "--clean")
    assert status == 0, err
    assert cli("nope")[0] == 1


def test_stale_unknown_and_clean(tmp_path, stage):
    d = str(tmp_path / "card")
    make(d, {"PIPBOY/HOLO/TWO.HTP": b"old", "BOOTDIR.LST": b"PIPBOY/HOLO/TWO.HTP\n",
             "PIPBOY/HOLO/X.HTP": b"unknown"})
    before = snap(d)
    for c in ([], ["--clean"]):
        status, _, err = run("sync-bootdir", stage, d, *c)
        stopped(status, err, before, d, "  PIPBOY/HOLO/X.HTP", "no BOOTDIR.LST names them")
    status, _, err = run("sync-bootdir", stage, d)
    assert "CLEAN=1" in err and "  PIPBOY/HOLO/TWO.HTP" in err
    os.unlink(os.path.join(d, "PIPBOY/HOLO/X.HTP"))
    status, out, err = run("sync-bootdir", stage, d, "--clean")
    assert status == 0, err
    assert "removed PIPBOY/HOLO/TWO.HTP" in out
    assert not os.path.exists(os.path.join(d, "PIPBOY/HOLO/TWO.HTP"))


def test_case_mismatch(tmp_path, stage):
    d = str(tmp_path / "card")
    make(d, {"pipboy/ROM1.ROM": b"old"})
    before = snap(d)
    for c in ([], ["--clean"]):
        status, _, err = run("sync-bootdir", stage, d, *c)
        stopped(status, err, before, d, "pipboy (this run writes PIPBOY)", "differ only in case")


@pytest.mark.parametrize("spec,line", [
    ({"PIPOS.IMG/X": b"x"}, "PIPOS.IMG (a folder: this run writes a file there)"),
    ({"PIPBOY": b"file"}, "PIPBOY (a file: this run needs a folder there)"),
    ({"PIPBOY/HOLO": b"file"}, "PIPBOY/HOLO (a file: this run needs a folder there)"),
    ({"PIPBOY/HOLO/ONE.HTP/X": b"x"}, "PIPBOY/HOLO/ONE.HTP (a folder: this run writes a file there)"),
    ({"BOOTDIR.LST/X": b"x"}, "BOOTDIR.LST (a folder"),
    ({"PIPOS.IMG": "fifo"}, "PIPOS.IMG (not a regular file"),
])
def test_type_conflicts(tmp_path, stage, spec, line):
    d = str(tmp_path / "card")
    make(d, spec)
    before = snap(d)
    for c in ([], ["--clean"]):
        status, _, err = run("sync-bootdir", stage, d, *c)
        stopped(status, err, before, d, line)


def test_clean_replaces_stale_file_with_folder(tmp_path, stage):
    d = str(tmp_path / "card")
    make(d, {"PIPBOY": b"old", "BOOTDIR.LST": b"PIPBOY\n"})
    status, _, err = run("sync-bootdir", stage, d, "--clean")
    assert status == 0, err
    assert snap(d)["PIPBOY/ROM1.ROM"] == ("f", b"rom\n")


# ---- sync-bootdir: links ---------------------------------------------------------------

LINK_CASES = {
    # name: (DIR spec, the path the stop names, clean)
    "planned file": ({"PIPOS.IMG": ("link", "{c}/CANARY.TXT")}, "PIPOS.IMG", False),
    "planned folder": ({"PIPBOY": ("link", "{c}/DIR")}, "PIPBOY", False),
    "parent component": ({"PIPBOY/HOLO": ("link", "{c}/DIR")}, "PIPBOY/HOLO", False),
    "stale file": ({"OLD.BIN": ("link", "{c}/CANARY.TXT"), "BOOTDIR.LST": b"OLD.BIN\n"}, "OLD.BIN", True),
    "manifest": ({"BOOTDIR.LST": ("link", "{c}/CANARY.TXT")}, "BOOTDIR.LST", False),
    "settings": ({"PIPBOY/PIPOS.CFG": ("link", "{c}/NONE.CFG"), "PIPBOY/X": b""}, None, False),
    "dangling": ({"PIPOS.IMG": ("link", "{c}/NEW.TXT")}, "PIPOS.IMG", False),
    "loop": ({"PIPOS.IMG": ("link", "PIPOS.IMG")}, "PIPOS.IMG", False),
}


@pytest.mark.parametrize("name", sorted(LINK_CASES))
def test_links_in_dir_stop(tmp_path, stage, canary, name):
    spec, line, clean = LINK_CASES[name]
    d = str(tmp_path / "card")
    make(d, {p: (v[0], v[1].format(c=canary)) if isinstance(v, tuple) else v for p, v in spec.items()})
    before = snap(d)
    status, _, err = run("sync-bootdir", stage, d, *(["--clean"] if clean else []))
    if line is None:
        # A PIPOS.CFG that exists, even as a link, is kept and never touched.
        assert status == 0, err
        assert snap(d)["PIPBOY/PIPOS.CFG"] == before["PIPBOY/PIPOS.CFG"]
        assert not os.path.exists(canary / "NONE.CFG")
        return
    stopped(status, err, before, d, f"  {line} (a symbolic link)", "never follows a link")
    assert not os.path.exists(canary / "NEW.TXT")


def test_dir_itself_may_be_a_link(tmp_path, stage):
    real = tmp_path / "real"
    real.mkdir()
    os.symlink(real, tmp_path / "card")
    status, out, err = run("sync-bootdir", stage, tmp_path / "card")
    assert status == 0, err
    assert "is a link" in out
    assert os.path.islink(tmp_path / "card")
    assert (real / "PIPOS.IMG").read_bytes() == b"kernel\n"


def test_dir_dangling_link(tmp_path, stage):
    os.symlink(tmp_path / "none", tmp_path / "card")
    status, _, err = run("sync-bootdir", stage, tmp_path / "card")
    assert status == 1 and "does not lead to a folder" in err
    assert not (tmp_path / "none").exists()


def test_stage_link_refused(tmp_path, stage, canary):
    os.unlink(os.path.join(stage, "PIPOS.IMG"))
    os.symlink(canary / "CANARY.TXT", os.path.join(stage, "PIPOS.IMG"))
    d = str(tmp_path / "card")
    status, _, err = run("sync-bootdir", stage, d)
    assert status == 1 and "PIPOS.IMG (a symbolic link)" in err, err
    assert not os.path.exists(d)


# ---- sync-bootdir: the manifest -------------------------------------------------------

@pytest.mark.parametrize("text", [b"../OUT.TXT\n", b"PIPBOY/../../OUT.TXT\n", b"/etc/hosts\n",
                                  b"PIPBOY/HOLO/\n", b"A//B\n", b"./X\n"])
def test_manifest_bad_lines_stop(tmp_path, stage, text):
    d = str(tmp_path / "card")
    make(d, {"BOOTDIR.LST": b"PIPOS.IMG\n" + text})
    make(str(tmp_path), {"OUT.TXT": b"outside"})
    before = snap(d)
    for c in ([], ["--clean"]):
        status, _, err = run("sync-bootdir", stage, d, *c)
        stopped(status, err, before, d, "line 2: ", "not a plain relative path")
    assert (tmp_path / "OUT.TXT").read_bytes() == b"outside"


def test_manifest_blank_lines_and_crlf(tmp_path, stage):
    d = str(tmp_path / "card")
    make(d, {"OLD.BIN": b"old", "MINE.TXT": b"mine",
             "BOOTDIR.LST": b"\r\n\nPIPOS.IMG\r\n   \nOLD.BIN\r\n"})
    status, _, err = run("sync-bootdir", stage, d)
    assert status == 1 and "  OLD.BIN" in err and "earlier bootdir run" in err, err
    status, out, err = run("sync-bootdir", stage, d, "--clean")
    assert status == 0, err
    assert "removed OLD.BIN" in out
    assert snap(d)["MINE.TXT"] == ("f", b"mine")


# ---- sync-bootdir: permissions --------------------------------------------------------------

@pytest.mark.skipif(ROOT_USER, reason="runs as root (the CI container): root can write a 0555 folder")
def test_read_only_folder_with_stale_files(tmp_path, stage):
    # Review finding P2: the old shell check of a stale file's folder used
    # a GNU-only sed expression, so on macOS it was skipped and A.BIN
    # was removed before the removal in OLD/ failed.
    d = str(tmp_path / "card")
    make(d, {"OLD/X.BIN": b"x", "A.BIN": b"o", "BOOTDIR.LST": b"OLD/X.BIN\nA.BIN\n"})
    os.chmod(os.path.join(d, "OLD"), 0o555)
    try:
        before = snap(d)
        status, _, err = run("sync-bootdir", stage, d, "--clean")
        stopped(status, err, before, d, "  OLD (folder)", "not writable")
    finally:
        unlock(d)


@pytest.mark.skipif(ROOT_USER, reason="runs as root (the CI container): root can write a read-only file")
def test_read_only_file_and_missing_dir_parent(tmp_path, stage):
    d = str(tmp_path / "card")
    make(d, {"PIPOS.IMG": b"old", "BOOTDIR.LST": b"PIPOS.IMG\n"})
    os.chmod(os.path.join(d, "PIPOS.IMG"), 0o444)
    before = snap(d)
    status, _, err = run("sync-bootdir", stage, d)
    stopped(status, err, before, d, "  PIPOS.IMG\n")
    top = tmp_path / "locked"
    top.mkdir()
    os.chmod(top, 0o555)
    try:
        status, _, err = run("sync-bootdir", stage, top / "a" / "card")
        assert status == 1 and "cannot create it" in err, err
    finally:
        os.chmod(top, 0o755)


# ---- the property sweep -----------------------------------------------------------------------

STAGE_POOL = ["A", "B", "C.TXT", "D", "PIPBOY", "ROM1.ROM"]
DIR_POOL = ["A", "a", "b", "C.TXT", "c.txt", "E", "PIPBOY", "pipboy", "ROM1.ROM", "G.TXT"]


def rand_path(r, pool):
    return "/".join(r.choice(pool) for _ in range(r.randint(1, 3)))


def put_file(root, p, data):
    try:
        os.makedirs(os.path.join(root, os.path.dirname(p)), exist_ok=True)
        if os.path.lexists(os.path.join(root, p)):
            return False
        with open(os.path.join(root, p), "wb") as f:
            f.write(data)
        return True
    except OSError:
        return False


def build_case(r, base, canary):
    stage, d = os.path.join(base, "stage"), os.path.join(base, "dir")
    os.makedirs(stage)
    os.makedirs(d)
    for _ in range(r.randint(1, 6)):
        p = rand_path(r, STAGE_POOL)
        put_file(stage, p, f"stage {p}\n".encode())
    if r.random() < 0.3:
        put_file(stage, "PIPBOY/PIPOS.CFG", b"cfg\n")
    files = []
    for _ in range(r.randint(0, 4)):
        p = rand_path(r, DIR_POOL)
        if put_file(d, p, f"dir {p}\n".encode()):
            files.append(p)
    if r.random() < 0.15:
        put_file(d, "PIPBOY/PIPOS.CFG", b"mine\n")
    for _ in range(r.choice([0, 0, 1, 2])):
        p = rand_path(r, DIR_POOL)
        target = r.choice([str(canary / "CANARY.TXT"), str(canary / "DIR"), str(canary / "NEW.TXT"),
                           os.path.basename(p)])
        try:
            os.makedirs(os.path.join(d, os.path.dirname(p)), exist_ok=True)
            os.symlink(target, os.path.join(d, p))
            files.append(p)
        except OSError:
            pass
    if r.random() < 0.6:
        names = [p for p in files if r.random() < 0.8]
        if r.random() < 0.3:
            names.append(rand_path(r, STAGE_POOL))
        if r.random() < 0.05:
            names.append("../" + r.choice(DIR_POOL))
        put_file(d, "BOOTDIR.LST", "".join(n + "\n" for n in names).encode())
    if r.random() < 0.25:
        dirs = [x for x, k in card.walk(d)[0].items() if k == "folder"]
        if dirs:
            os.chmod(os.path.join(d, r.choice(dirs)), 0o555)
    return stage, d


def expected(stage, before, clean):
    """DIR after a successful run, from DIR before and the stage."""
    st = snap(stage)
    files = {p: v for p, v in st.items() if v[0] == "f"}
    special = {card.fold(card.MANIFEST), card.fold(card.CFG)}
    written = {card.fold(p) for p in files if card.fold(p) not in special}
    old = set()
    if before.get("BOOTDIR.LST", ("",))[0] == "f":
        old = {card.fold(p) for p in card.parse_manifest(before["BOOTDIR.LST"][1])[0]}
    want = {}
    for p, v in before.items():
        f = card.fold(p)
        if v[0] == "d" or f in special:
            want[p] = v
        elif f in written or (clean and f in old):
            continue
        else:
            want[p] = v
    for p, v in st.items():
        if v[0] == "d":
            want[p] = v
        elif card.fold(p) == card.fold(card.CFG):
            want.setdefault(p, v)
        else:
            want[p] = v
    want["BOOTDIR.LST"] = ("f", "".join(p + "\n" for p in sorted(files) if card.fold(p) not in special).encode())
    return want


def folded_dups(s):
    seen, dups = {}, set()
    for p in s:
        f = card.fold(p)
        if f in seen and seen[f] != p:
            dups.add(f)
        seen.setdefault(f, p)
    return dups


def test_plan_check_apply_sweep(tmp_path, fs, canary):
    r = random.Random(20261123)
    outcomes = {"ok": 0, "stop": 0}
    for i in range(300):
        base = str(tmp_path / f"case{i}")
        stage, d = build_case(r, base, canary)
        clean = r.random() < 0.5
        before = snap(d)
        try:
            status, _, err = run("sync-bootdir", stage, d, *(["--clean"] if clean else []))
            after = snap(d)
        finally:
            unlock(base)
        if status == 0:
            outcomes["ok"] += 1
            assert after == expected(stage, before, clean), f"case {i} ({fs}): wrong DIR after a run"
            assert folded_dups(after) <= folded_dups(before), f"case {i}: added folded duplicates"
        else:
            outcomes["stop"] += 1
            assert "Nothing was changed." in err, f"case {i}: failed without a stop: {err}"
            assert after == before, f"case {i} ({fs}): a stop changed DIR"
    print(f"\nsweep ({fs}): {outcomes}")
    assert outcomes["ok"] > 20 and outcomes["stop"] > 20, outcomes
