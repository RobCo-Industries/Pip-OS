"""Unit tests for tools/sdk_package.py, the SDK package writer and checker.

They need no build: each case writes a package of small stand-in files in
the pytest temp folder, checks that it passes, then breaks one rule and
checks that the checker names it.
"""

import contextlib
import hashlib
import importlib.util
import io
import json
import os
import stat
import zipfile

import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TOOL = os.path.join(ROOT, "tools", "sdk_package.py")
FIRMWARE_SCRIPT = os.path.join(ROOT, "external", "robco-core", "tools", "fetch_firmware.sh")

_spec = importlib.util.spec_from_file_location("sdk_package", TOOL)
pkg = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(pkg)

FORBID = "/home/builder-x7/work"


def run(*args):
    """pkg.main(args) in this process: (status, stdout, stderr)."""
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        status = pkg.main([str(a) for a in args])
    return status, out.getvalue(), err.getvalue()


def layout_paths():
    return list(pkg.LAYOUT) + [f"include/pipos/{h}" for h in pkg.HEADERS]


def build_args(tmp, gcc="15.2.1-1.1", printed="15.2.1", extra=()):
    src = tmp / "src"
    src.mkdir(exist_ok=True)
    abi = src / "abi.h"
    abi.write_text("#define PIP_ABI_VERSION     0x00010004  /* x */\n")
    args = ["build", "--out", tmp / "out", "--abi-header", abi, "--build", "0",
            "--gcc", gcc, "--gcc-printed", printed, "--qemu", "10.1.2",
            "--firmware-script", FIRMWARE_SCRIPT,
            "--built-from", "Pip-OS=" + "1" * 40, "--built-from", "RobCo-Core=" + "2" * 40]
    for i, p in enumerate(layout_paths()):
        f = src / f"f{i}"
        f.write_bytes(f"content of {p}\n".encode())
        args += ["--file", f"{p}={f}"]
    return args + list(extra)


@pytest.fixture
def package(tmp_path):
    status, out, err = run(*build_args(tmp_path))
    assert status == 0, err
    path = out.strip()
    assert path == str(tmp_path / "out" / "pipos-1.4.0.zip")
    return path


def entries(path):
    with zipfile.ZipFile(path) as z:
        return [(i, z.read(i)) for i in z.infolist()]


def rewrite(path, items):
    with zipfile.ZipFile(path, "w") as z:
        for info, data in items:
            z.writestr(info, data)


def manifest(items):
    return json.loads(dict((i.filename, d) for i, d in items)["sdk.json"])


def set_manifest(items, doc):
    return [(i, json.dumps(doc).encode() if i.filename == "sdk.json" else d) for i, d in items]


def add(items, name, data, mode=0o100644, listed=True):
    info = pkg.zip_info(name)
    info.external_attr = mode << 16
    items = items + [(info, data)]
    if listed:
        doc = manifest(items)
        doc["files"].append({"path": name, "size": len(data),
                             "sha256": hashlib.sha256(data).hexdigest()})
        doc["files"].sort(key=lambda e: e["path"].encode())
        items = set_manifest(items, doc)
    return items


def check(path, *forbid):
    args = ["check", path]
    for f in forbid:
        args += ["--forbid", f]
    status, out, _ = run(*args)
    return status, out


def assert_fails(path, text, *forbid):
    status, out = check(path, *forbid)
    assert status == 1, out
    assert text in out, out


# ---- build -------------------------------------------------------------------------

def test_built_package_passes(package):
    status, out = check(package, FORBID)
    assert status == 0, out
    assert out.startswith("ok:")


def test_two_builds_give_the_same_bytes(tmp_path):
    a, b = tmp_path / "a", tmp_path / "b"
    a.mkdir(), b.mkdir()
    pa = run(*build_args(a))[1].strip()
    pb = run(*build_args(b))[1].strip()
    os.utime(pb, (0, 0))
    with open(pa, "rb") as fa, open(pb, "rb") as fb:
        assert fa.read() == fb.read()


def test_entries_are_fixed_and_ordered(package):
    items = entries(package)
    names = [i.filename for i, _ in items]
    assert names[0] == "sdk.json"
    assert names[1:] == sorted(names[1:], key=str.encode)
    for info, _ in items:
        assert info.date_time == (1980, 1, 1, 0, 0, 0)
        assert info.external_attr >> 16 == 0o100644
        assert info.compress_type == zipfile.ZIP_STORED
        assert info.extra == b""


def test_manifest_fields(package):
    doc = manifest(entries(package))
    assert list(doc) == ["format", "name", "version", "abi", "built_from", "formats",
                         "requires", "tools", "firmware", "licences", "files"]
    assert doc["version"] == "1.4.0" and doc["abi"] == "1.4"
    assert doc["tools"] == [{"name": "gcc", "version": "15.2.1-1.1"},
                            {"name": "qemu", "version": "10.1.2"}]
    assert doc["formats"] == {"rimg": 1, "flags": 1, "boot": 1, "runner": 1}
    assert [f["path"] for f in doc["files"]] == sorted(layout_paths(), key=str.encode)


def test_firmware_pins_come_from_the_script():
    fw = pkg.firmware_of(FIRMWARE_SCRIPT)
    assert fw["repo"] == "raspberrypi/firmware"
    assert fw["commit"] in fw["base_url"] and "$" not in fw["base_url"]
    assert [f["name"] for f in fw["files"]] == ["bootcode.bin", "start.elf", "fixup.dat"]


def test_build_refuses_another_compiler(tmp_path):
    status, _, err = run(*build_args(tmp_path, printed="15.2.0"))
    assert status == 1
    assert "prints 15.2.0" in err
    assert not (tmp_path / "out" / "pipos-1.4.0.zip").exists()


def test_build_names_a_local_compiler_when_told(tmp_path):
    status, out, _ = run(*build_args(tmp_path, gcc="15.2.0", printed="15.2.0"))
    assert status == 0
    assert manifest(entries(out.strip()))["tools"][0] == {"name": "gcc", "version": "15.2.0"}


def test_build_refuses_a_link_as_source(tmp_path):
    args = build_args(tmp_path)
    target = args[args.index("--file") + 1].split("=", 1)[1]
    os.rename(target, target + ".real")
    os.symlink(target + ".real", target)
    status, _, err = run(*args)
    assert status == 1 and "not a regular file" in err


def test_build_refuses_a_short_commit(tmp_path):
    status, _, err = run(*build_args(tmp_path, extra=["--built-from", "X=abc"]))
    assert status == 1 and "40 lower case hex" in err


# ---- check: one break per rule ---------------------------------------------------------

def test_unlisted_file(package):
    rewrite(package, add(entries(package), "include/pipos/extra.h", b"x", listed=False))
    assert_fails(package, "include/pipos/extra.h: in the archive and not listed")


def test_listed_file_missing(package):
    items = [(i, d) for i, d in entries(package) if i.filename != "docs/API.md"]
    rewrite(package, items)
    assert_fails(package, "docs/API.md: listed in sdk.json files and not in the archive")


def test_files_out_of_order(package):
    items = entries(package)
    doc = manifest(items)
    doc["files"][0], doc["files"][1] = doc["files"][1], doc["files"][0]
    rewrite(package, set_manifest(items, doc))
    assert_fails(package, "is not after")


def test_file_listed_twice(package):
    items = entries(package)
    doc = manifest(items)
    doc["files"].insert(1, doc["files"][0])
    rewrite(package, set_manifest(items, doc))
    assert_fails(package, "is not after")


def test_wrong_hash(package):
    items = [(i, b"changed\n" if i.filename == "lib/rom.ld" else d) for i, d in entries(package)]
    rewrite(package, items)
    assert_fails(package, "lib/rom.ld: SHA-256")


def test_wrong_size(package):
    items = entries(package)
    doc = manifest(items)
    doc["files"][0]["size"] += 1
    rewrite(package, set_manifest(items, doc))
    assert_fails(package, "sdk.json says")


def test_upper_case_hash(package):
    items = entries(package)
    doc = manifest(items)
    doc["files"][0]["sha256"] = doc["files"][0]["sha256"].upper()
    rewrite(package, set_manifest(items, doc))
    assert_fails(package, "lower case SHA-256")


def test_symbolic_link(package):
    rewrite(package, add(entries(package), "include/pipos/link.h", b"abi.h",
                         mode=stat.S_IFLNK | 0o777))
    assert_fails(package, "include/pipos/link.h: the entry is a symbolic link")


def test_device_entry(package):
    rewrite(package, add(entries(package), "include/pipos/dev.h", b"",
                         mode=stat.S_IFCHR | 0o644))
    assert_fails(package, "not a regular file or a folder")


def test_names_that_differ_only_in_case(package):
    rewrite(package, add(entries(package), "include/pipos/PIP.h", b"x"))
    assert_fails(package, "differs only in case from include/pipos/pip.h")


def test_same_name_twice(package):
    items = entries(package)
    dup = [(pkg.zip_info("lib/rom.ld"), b"x")]
    with pytest.warns(UserWarning):
        rewrite(package, items + dup)
    assert_fails(package, "lib/rom.ld: the name comes twice")


@pytest.mark.parametrize("name,why", [
    ("../evil.h", "has an empty, . or .. component"),
    ("include//x.h", "has an empty, . or .. component"),
    ("include/./x.h", "has an empty, . or .. component"),
    ("C:x.h", "starts with a drive letter"),
    ("include\\x.h", "uses \\ as a separator"),
])
def test_bad_names(package, name, why):
    rewrite(package, add(entries(package), name, b"x", listed=False))
    assert_fails(package, why)


def test_absolute_name(package):
    info = pkg.zip_info("x.h")
    info.filename = "/x.h"
    rewrite(package, entries(package) + [(info, b"x")])
    assert_fails(package, "the name is absolute")


def test_other_compression(package):
    info = pkg.zip_info("include/pipos/bz.h")
    info.compress_type = zipfile.ZIP_BZIP2
    rewrite(package, add(entries(package), "include/pipos/bz.h", b"x"))
    items = [(info if i.filename == "include/pipos/bz.h" else i, d) for i, d in entries(package)]
    rewrite(package, items)
    assert_fails(package, "is not stored or deflate")


def test_firmware_file(package):
    rewrite(package, add(entries(package), "boot/start.elf", b"x"))
    assert_fails(package, "boot/start.elf: a Pi firmware file is never part of a package")


def test_no_manifest(package):
    rewrite(package, [(i, d) for i, d in entries(package) if i.filename != "sdk.json"])
    assert_fails(package, "sdk.json: not at the archive root")


def test_manifest_listed_in_files(package):
    items = entries(package)
    doc = manifest(items)
    doc["files"].insert(0, {"path": "SDK.JSON", "size": 0, "sha256": "0" * 64})
    rewrite(package, set_manifest(items, doc))
    assert_fails(package, "files lists SDK.JSON, the manifest")


def test_layout_file_missing(package):
    items = [(i, d) for i, d in entries(package) if i.filename != "boot/fwstub.bin"]
    doc = manifest(items)
    doc["files"] = [f for f in doc["files"] if f["path"] != "boot/fwstub.bin"]
    rewrite(package, set_manifest(items, doc))
    assert_fails(package, "boot/fwstub.bin: the pipos layout needs it")


def test_file_outside_layout(package):
    rewrite(package, add(entries(package), "boot/extra.bin", b"x"))
    assert_fails(package, "boot/extra.bin: not a file of the pipos layout")


def test_new_header_is_allowed(package):
    rewrite(package, add(entries(package), "include/pipos/later.h", b"x"))
    assert check(package)[0] == 0


@pytest.mark.parametrize("field,value,text", [
    ("abi", "1.3", "abi is not the first two parts"),
    ("version", "1.04.0", "version is not X.Y.Z"),
    ("formats", {"rimg": 1, "flags": 1, "boot": 1}, "formats is not"),
    ("tools", [{"name": "qemu", "version": "1"}, {"name": "gcc", "version": "1"}],
     "tools is not gcc then qemu"),
    ("built_from", [{"repo": "Pip-OS", "commit": "ABC"}], "built_from is not"),
    ("requires", [{"name": "x", "abi": "1.0"}], "requires is not empty"),
    ("licences", [], "licences is not"),
    ("firmware", {"repo": "raspberrypi/firmware"}, "firmware is not"),
])
def test_manifest_breaks(package, field, value, text):
    items = entries(package)
    doc = manifest(items)
    doc[field] = value
    rewrite(package, set_manifest(items, doc))
    assert_fails(package, text)


def test_missing_field(package):
    items = entries(package)
    doc = manifest(items)
    del doc["firmware"]
    rewrite(package, set_manifest(items, doc))
    assert_fails(package, "sdk.json: firmware is missing")


def test_build_folder_in_a_file(package):
    items = [(i, d + FORBID.encode() if i.filename == "lib/libpip.a" else d)
             for i, d in entries(package)]
    doc = manifest(items)
    for f in doc["files"]:
        if f["path"] == "lib/libpip.a":
            data = dict((i.filename, d) for i, d in items)["lib/libpip.a"]
            f["size"], f["sha256"] = len(data), hashlib.sha256(data).hexdigest()
    rewrite(package, set_manifest(items, doc))
    assert check(package)[0] == 0
    assert_fails(package, f"lib/libpip.a: holds {FORBID!r}", FORBID)


def test_host_name_in_the_manifest(package):
    items = entries(package)
    doc = manifest(items)
    doc["licences"][0]["note"] = "built on pkghost-42"
    rewrite(package, set_manifest(items, doc))
    assert_fails(package, "sdk.json: holds 'pkghost-42'", "pkghost-42")


def test_empty_forbid_text_is_refused(package):
    assert_fails(package, "an empty text matches everything", "")


def test_not_a_zip(tmp_path):
    p = tmp_path / "x.zip"
    p.write_bytes(b"no")
    assert_fails(str(p), "not a zip archive")
