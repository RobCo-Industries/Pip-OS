"""Unit tests for the templates kind of tools/sdk_package.py check.

Each case writes a small templates package in the pytest temp folder, in
the layout of RobCo-Core's docs/spec/SDK_PACKAGE.md "The templates
package", checks that it passes, then breaks one rule and checks that the
checker names it.
"""

import contextlib
import copy
import hashlib
import importlib.util
import io
import json
import os

import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TOOL = os.path.join(ROOT, "tools", "sdk_package.py")

_spec = importlib.util.spec_from_file_location("sdk_package", TOOL)
pkg = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(pkg)

COMMIT = "3" * 40


def good_files():
    """The files of a good package but sdk.json, by path."""
    files = {
        "LICENSES/MIT.txt": b"MIT License\n",
        "holotape-c/icon.pbm": b"P1\n32 32\n" + b"0" * 1024 + b"\n",
        "holotape-c/robco.toml": pkg.HOLOTAPE_C_BYTES["robco.toml"],
        "holotape-c/src/main.c": b"/* SPDX-License-Identifier: MIT */\n#include <pipos/pip.h>\n",
        "holotape-c/tests/golden/start/ready.png": b"\x89PNG stand-in",
        "holotape-c/tests/start.toml": pkg.HOLOTAPE_C_BYTES["tests/start.toml"],
    }
    return files


def good_index():
    return {"format": 1, "templates": [copy.deepcopy(pkg.HOLOTAPE_C)]}


def good_doc():
    return {
        "format": 1,
        "name": "templates",
        "version": "0.1.0",
        "built_from": [{"repo": "RobCo-Reel", "commit": COMMIT}],
        "formats": {"templates": 1, "project": 1, "scenario": 1},
        "requires": [],
        "licences": [{"paths": [""], "spdx": "MIT"}],
    }


def write(tmp, files=None, index=None, doc=None, name="templates-0.1.0.zip", raw_index=None):
    """A package of `files` plus templates.json from `index` (or the bytes
    raw_index) and sdk.json from `doc`, with `files` listed. Returns its path."""
    files = dict(good_files() if files is None else files)
    if raw_index is not None:
        files["templates.json"] = raw_index
    elif index is not False:
        files["templates.json"] = json.dumps(good_index() if index is None else index).encode()
    doc = good_doc() if doc is None else doc
    order = sorted(files, key=lambda p: p.encode())
    doc["files"] = [{"path": p, "size": len(files[p]),
                     "sha256": hashlib.sha256(files[p]).hexdigest()} for p in order]
    out = tmp / name
    pkg.write_zip(str(out), [("sdk.json", json.dumps(doc).encode())]
                  + [(p, files[p]) for p in order])
    return out


def check(path, *extra):
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        status = pkg.main(["check", str(path), *extra])
    return status, out.getvalue()


def assert_fails(path, text, *extra):
    status, out = check(path, *extra)
    assert status == 1, out
    assert f"FAIL: {text}" in out, out
    return out


# ---- the package as a whole ----------------------------------------------------------

def test_good_package_passes(tmp_path):
    status, out = check(write(tmp_path))
    assert status == 0, out
    assert out.startswith("ok:")


def test_requires_may_be_absent(tmp_path):
    doc = good_doc()
    del doc["requires"]
    status, out = check(write(tmp_path, doc=doc))
    assert status == 0, out


def test_unknown_name_names_the_kinds(tmp_path):
    doc = good_doc()
    doc["name"] = "deitrix"
    out = assert_fails(write(tmp_path, doc=doc, name="deitrix-0.1.0.zip"),
                       "sdk.json: name is not pipos or templates, the kinds this checker knows")
    assert "templates.json" not in out


def test_kind_forced_against_the_name(tmp_path):
    path = write(tmp_path)
    assert_fails(path, "sdk.json: name is not pipos", "--kind", "pipos")
    status, out = check(path, "--kind", "templates")
    assert status == 0, out


@pytest.mark.parametrize("field,value", [
    ("abi", "0.1"),
    ("tools", [{"name": "gcc", "version": "1"}, {"name": "qemu", "version": "1"}]),
    ("firmware", {}),
])
def test_pipos_field_must_be_absent(tmp_path, field, value):
    doc = good_doc()
    doc[field] = value
    assert_fails(write(tmp_path, doc=doc), f"sdk.json: {field} must be absent in a templates package")


@pytest.mark.parametrize("formats", [
    {"templates": 1, "project": 1},
    {"templates": 1, "project": 1, "scenario": 2},
    {"templates": 1, "project": 1, "scenario": 1, "rimg": 1},
])
def test_formats(tmp_path, formats):
    doc = good_doc()
    doc["formats"] = formats
    assert_fails(write(tmp_path, doc=doc), "sdk.json: formats is not")


def test_requires_not_empty(tmp_path):
    doc = good_doc()
    doc["requires"] = [{"name": "pipos", "abi": "1.4"}]
    assert_fails(write(tmp_path, doc=doc), "sdk.json: requires is not empty")


@pytest.mark.parametrize("built_from", [
    [{"repo": "Pip-OS", "commit": COMMIT}],
    [{"repo": "RobCo-Reel", "commit": COMMIT}, {"repo": "RobCo-Core", "commit": COMMIT}],
])
def test_built_from_is_reel(tmp_path, built_from):
    doc = good_doc()
    doc["built_from"] = built_from
    assert_fails(write(tmp_path, doc=doc), "sdk.json: built_from is not RobCo-Reel and its commit")


def test_licence_not_mit(tmp_path):
    doc = good_doc()
    doc["licences"] = [{"paths": [""], "spdx": "Apache-2.0"}]
    assert_fails(write(tmp_path, doc=doc),
                 "sdk.json: licences is not MIT for every path of a templates package")


def test_licence_leaves_a_path_out(tmp_path):
    doc = good_doc()
    doc["licences"] = [{"paths": ["holotape-c/", "LICENSES/"], "spdx": "MIT"}]
    assert_fails(write(tmp_path, doc=doc),
                 "sdk.json: licences covers no prefix of templates.json")


def test_licence_per_prefix_passes(tmp_path):
    doc = good_doc()
    doc["licences"] = [{"paths": ["holotape-c/", "LICENSES/", "templates.json"], "spdx": "MIT"}]
    status, out = check(write(tmp_path, doc=doc))
    assert status == 0, out


def test_rules_of_every_package_still_apply(tmp_path):
    path = write(tmp_path, name="templates-0.2.0.zip")
    assert_fails(path, "templates-0.2.0.zip: the file name is not <name>-<version>.zip")


# ---- layout of templates -------------------------------------------------------------

@pytest.mark.parametrize("path", ["LICENSES/MIT.txt"])
def test_licence_file_missing(tmp_path, path):
    files = good_files()
    del files[path]
    assert_fails(write(tmp_path, files=files),
                 f"{path}: the templates layout needs it and sdk.json does not list it")


def test_index_missing(tmp_path):
    assert_fails(write(tmp_path, index=False),
                 "templates.json: the templates layout needs it and sdk.json does not list it")


@pytest.mark.parametrize("path", ["README.md", "LICENSES/GPL.txt", "Holotape-c/x.c", "other/x.c"])
def test_file_outside_layout(tmp_path, path):
    files = good_files()
    files[path] = b"x"
    assert_fails(write(tmp_path, files=files), f"{path}: not a file of the templates layout")


def test_unlisted_file_under_a_template(tmp_path):
    files = good_files()
    files["holotape-c/src/extra.c"] = b"int x;\n"
    assert_fails(write(tmp_path, files=files),
                 "holotape-c/src/extra.c: under holotape-c/ and not listed in its files")


def test_listed_file_missing(tmp_path):
    files = good_files()
    del files["holotape-c/icon.pbm"]
    assert_fails(write(tmp_path, files=files),
                 "templates.json: holotape-c/icon.pbm: listed and the package does not hold it")


# ---- templates.json ------------------------------------------------------------------

@pytest.mark.parametrize("raw,text", [
    (b"{not json", "templates.json: not UTF-8 JSON"),
    (b'{"format": "\xff"}', "templates.json: not UTF-8 JSON"),
    (b"[]", "templates.json: the top value is not an object"),
    (b'{"format": 1, "templates": [], "x": "\\ud800"}', "templates.json: the string"),
])
def test_index_that_does_not_parse(tmp_path, raw, text):
    assert_fails(write(tmp_path, raw_index=raw), text)


def second(tid="other"):
    return {"id": tid, "title": "Other", "description": "Another one.", "kind": "holotape",
            "files": [{"path": "a.c", "substitute": False}]}


def files_with_second(tid="other"):
    files = good_files()
    files[f"{tid}/a.c"] = b"int a;\n"
    return files


INDEX_ROWS = [
    ("extra top field", lambda d: d.update(x=1), "templates.json: field 'x' is not one the spec defines"),
    ("format 2", lambda d: d.update(format=2), "templates.json: format is not 1"),
    ("format true", lambda d: d.update(format=True), "templates.json: format is not 1"),
    ("no format", lambda d: d.pop("format"), "templates.json: format is not 1"),
    ("no templates", lambda d: d.update(templates=[]),
     "templates.json: templates is not an array of one or more templates"),
    ("template not object", lambda d: d["templates"].append(3),
     "templates.json: templates[1] is not an object"),
    ("extra template field", lambda d: d["templates"][0].update(x=1),
     "templates.json: templates[0]: field 'x' is not one the spec defines"),
    ("id upper case", lambda d: d["templates"][0].update(id="Holotape-c"),
     "templates.json: templates[0]: id is not 1 to 32"),
    ("id digit first", lambda d: d["templates"][0].update(id="1tape"),
     "templates.json: templates[0]: id is not 1 to 32"),
    ("id too long", lambda d: d["templates"][0].update(id="a" * 33),
     "templates.json: templates[0]: id is not 1 to 32"),
    ("title empty", lambda d: d["templates"][0].update(title=""),
     "templates.json: holotape-c: title is not ASCII of 1 to 64 bytes"),
    ("title long", lambda d: d["templates"][0].update(title="t" * 65),
     "templates.json: holotape-c: title is not ASCII of 1 to 64 bytes"),
    ("title not ascii", lambda d: d["templates"][0].update(title="Holotape é"),
     "templates.json: holotape-c: title is not ASCII of 1 to 64 bytes"),
    ("description long", lambda d: d["templates"][0].update(description="d" * 201),
     "templates.json: holotape-c: description is not ASCII of 1 to 200 bytes"),
    ("no description", lambda d: d["templates"][0].pop("description"),
     "templates.json: holotape-c: description is not ASCII of 1 to 200 bytes"),
    ("kind not string", lambda d: d["templates"][0].update(kind=1),
     "templates.json: holotape-c: kind is not holotape"),
    ("kind rom", lambda d: d["templates"][0].update(kind="rom"),
     "templates.json: holotape-c: kind is not holotape"),
    ("files not array", lambda d: d["templates"][0].update(files={}),
     "templates.json: holotape-c: files is not an array"),
    ("file without substitute", lambda d: d["templates"][0]["files"][0].pop("substitute"),
     "templates.json: holotape-c: files[0] is not a path and a boolean substitute"),
    ("substitute not boolean", lambda d: d["templates"][0]["files"][0].update(substitute=0),
     "templates.json: holotape-c: files[0] is not a path and a boolean substitute"),
    ("extra file field", lambda d: d["templates"][0]["files"][0].update(mode=1),
     "templates.json: holotape-c: files[0]: field 'mode' is not one the spec defines"),
    ("files out of order", lambda d: d["templates"][0]["files"].reverse(),
     "templates.json: holotape-c: files: robco.toml is not after src/main.c"),
    ("holotape-c entry differs", lambda d: d["templates"][0].update(title="Other title"),
     "templates.json: holotape-c: the entry is not the one SDK_PACKAGE.md gives"),
]


@pytest.mark.parametrize("change,text", [(c, t) for _, c, t in INDEX_ROWS],
                         ids=[n for n, _, _ in INDEX_ROWS])
def test_index_breaks(tmp_path, change, text):
    index = good_index()
    change(index)
    assert_fails(write(tmp_path, index=index), text)


@pytest.mark.parametrize("path,why", [
    ("../a.c", "has an empty, . or .. component"),
    ("src//a.c", "has an empty, . or .. component"),
    ("/a.c", "is absolute"),
    ("C:a.c", "starts with a drive letter"),
    ("src\\a.c", "uses \\ as a separator"),
    ("@NAME@.c", "holds a token"),
])
def test_template_path_rules(tmp_path, path, why):
    index = good_index()
    index["templates"].append(second())
    index["templates"][1]["files"] = [{"path": path, "substitute": False}]
    assert_fails(write(tmp_path, files=files_with_second(), index=index),
                 f"templates.json: other: files: {path!r}: the path {why}")


def test_template_paths_differ_only_in_case(tmp_path):
    index = good_index()
    index["templates"].append(second())
    index["templates"][1]["files"] = [{"path": "A.c", "substitute": False},
                                      {"path": "a.c", "substitute": False}]
    files = files_with_second()
    files["other/A.c"] = b"x"
    out = assert_fails(write(tmp_path, files=files, index=index),
                       "templates.json: other: files: a.c differs only in case from A.c")
    assert "sdk.json: files: other/a.c differs only in case" in out


def test_second_template_passes(tmp_path):
    index = good_index()
    index["templates"].append(second())
    status, out = check(write(tmp_path, files=files_with_second(), index=index))
    assert status == 0, out


def test_templates_out_of_order(tmp_path):
    index = good_index()
    index["templates"].insert(0, second("zeta"))
    assert_fails(write(tmp_path, files=files_with_second("zeta"), index=index),
                 "templates.json: holotape-c is not after zeta in byte order of id")


def test_template_id_twice(tmp_path):
    index = good_index()
    index["templates"].append(copy.deepcopy(pkg.HOLOTAPE_C))
    assert_fails(write(tmp_path, index=index),
                 "templates.json: holotape-c is not after holotape-c in byte order of id")


# ---- tokens and the files of holotape-c ----------------------------------------------

def test_other_token_in_a_substituted_file(tmp_path):
    files = good_files()
    files["holotape-c/robco.toml"] += b"# @TITLE@\n"
    out = assert_fails(write(tmp_path, files=files),
                       "holotape-c/robco.toml: holds the token @TITLE@, and only @NAME@ is replaced")
    assert out.count("@TITLE@") == 1


def test_token_in_a_copied_file_passes(tmp_path):
    files = good_files()
    files["holotape-c/src/main.c"] += b"/* @TITLE@ */\n"
    status, out = check(write(tmp_path, files=files))
    assert status == 0, out


@pytest.mark.parametrize("path", ["robco.toml", "tests/start.toml"])
def test_holotape_c_file_not_byte_for_byte(tmp_path, path):
    files = good_files()
    files[f"holotape-c/{path}"] = files[f"holotape-c/{path}"].replace(b"\n", b"\r\n")
    assert_fails(write(tmp_path, files=files),
                 f"holotape-c/{path}: not the file SDK_PACKAGE.md gives byte for byte")


# ---- folder entries --------------------------------------------------------------------

def add_dirs(path, *dirs):
    """Append empty folder entries to the archive at `path`."""
    import zipfile
    with zipfile.ZipFile(path, "a") as z:
        for d in dirs:
            z.writestr(zipfile.ZipInfo(d), b"")
    return path


def test_folders_of_the_layout_pass(tmp_path):
    path = add_dirs(write(tmp_path), "LICENSES/", "holotape-c/", "holotape-c/src/",
                    "holotape-c/tests/golden/")
    status, out = check(path)
    assert status == 0, out


@pytest.mark.parametrize("folder", ["unlisted/", "holotape-c/docs/", "LICENSES/x/", "other/"])
def test_folder_outside_the_layout(tmp_path, folder):
    assert_fails(add_dirs(write(tmp_path), folder),
                 f"{folder}: a folder outside the templates layout")
