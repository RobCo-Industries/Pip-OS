#!/usr/bin/env python3
"""Writes and checks the pipos SDK package, pipos-<version>.zip.

    sdk_package.py build --out DIR --abi-header H --build N --gcc RELEASE
        --gcc-printed VERSION --qemu VERSION --firmware-script SCRIPT
        --built-from REPO=COMMIT... --file PATH=SOURCE...
    sdk_package.py check ZIP [--forbid TEXT]... [--user NAME]...
    sdk_package.py plant ZIP OUT PATH TEXT

`build` writes sdk.json and the archive. The archive bytes depend only on
the files and the arguments: entries in byte order of their names with
sdk.json first, stored, dated 1980-01-01 00:00, mode 0644, no extra fields.

`check` applies the archive rules of RobCo-Core docs/spec/SDK_PACKAGE.md
and the pipos layout, and searches every file, sdk.json included, for each
--forbid text, such as the build folder or the host name, and for each
--user name in the forms a tool writes it: /home/NAME, /Users/NAME,
\\Users\\NAME and NAME@. It refuses a text under 4 characters. It prints
one FAIL line per broken rule, an unreadable entry included, and exits 1
when there is one.

`plant` writes a copy of ZIP with TEXT added to the end of one file and its
sdk.json entry updated, so CI can show that the search fails.

Stdlib only.
"""

import argparse
import hashlib
import json
import os
import re
import stat
import sys
import unicodedata
import zipfile
import zlib

NAME = "pipos"
MANIFEST = "sdk.json"
FORMATS = {"rimg": 1, "flags": 1, "boot": 1, "runner": 1}
FIRMWARE_FILES = ("bootcode.bin", "start.elf", "fixup.dat")
FIRMWARE_REPO = "raspberrypi/firmware"
# SDK_PACKAGE.md "Layout of pipos": every file but sdk.json and the headers.
LAYOUT = (
    "LICENSES/MIT.txt",
    "boot.json",
    "boot/PIPOS.CFG",
    "boot/PIPOS.IMG",
    "boot/RUNNER.ROM",
    "boot/config.txt",
    "boot/fwstub.bin",
    "boot/pipos.elf",
    "docs/API.md",
    "flags.json",
    "lib/crt0.S",
    "lib/holotape.ld",
    "lib/libpip.a",
    "lib/rom.ld",
)
HEADERS = ("abi.h", "crc32.h", "crt0.h", "heap.h", "ini.h", "pip.h",
           "printf.h", "selftest.h", "string.h", "syscall.h")
HEADER_RE = re.compile(r"include/pipos/[^/]+\.h")
# Every part of pipos is MIT, by SDK_PACKAGE.md "Licences".
LICENCES = [{"paths": [""], "spdx": "MIT"}]
ZIP_DATE = (1980, 1, 1, 0, 0, 0)
ZIP_MODE = 0o100644

# A forbidden text shorter than this would match by chance, so check refuses it.
FORBID_MIN = 4
# The forms a tool writes a user name in: a home folder on Linux, macOS and
# Windows, and user@host. The bare name is not searched: a common word such
# as runner is also a name of the package's own content.
USER_FORMS = ("/home/{}", "/Users/{}", "\\Users\\{}", "{}@")
# What ZipFile.read raises on an entry it cannot read: an unknown method,
# a bad CRC, encryption, a cut or corrupt stream.
READ_ERRORS = (zipfile.BadZipFile, NotImplementedError, RuntimeError, zlib.error,
               EOFError, OSError, ValueError)

# The bit of a zip entry that marks its name as UTF-8.
UTF8_FLAG = 0x800
# The file RobCo Studio adds to an installed package folder.
RECORD = ".robco-archive"
# The largest sdk.json RobCo Studio reads, 4 MiB.
MANIFEST_MAX = 4 << 20

HEX40 = re.compile(r"[0-9a-f]{40}")
HEX64 = re.compile(r"[0-9a-f]{64}")
PART = r"(0|[1-9][0-9]{0,4})"
VERSION_RE = re.compile(rf"{PART}\.{PART}\.{PART}")


class Error(Exception):
    """A refusal of build, with its message."""


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def part_ok(s):
    return int(s) <= 65535


# JSON types as SDK_PACKAGE.md names them. In Python a JSON true is an int to
# isinstance and 1.0 == 1, so an integer is an int that is not a bool.
def is_int(v):
    return isinstance(v, int) and not isinstance(v, bool)


def is_str(v):
    return isinstance(v, str)


def is_list(v):
    return isinstance(v, list)


def is_dict(v):
    return isinstance(v, dict)


# ---- build -------------------------------------------------------------------------

def abi_of(header):
    """MAJOR.MINOR from PIP_ABI_VERSION in abi.h."""
    with open(header, encoding="utf-8") as f:
        m = re.search(r"^#define\s+PIP_ABI_VERSION\s+0x([0-9a-fA-F]{8})\b", f.read(), re.M)
    if not m:
        raise Error(f"{header}: no #define PIP_ABI_VERSION 0x<8 hex digits>")
    v = int(m.group(1), 16)
    return f"{v >> 16}.{v & 0xFFFF}"


def firmware_of(script):
    """The firmware pins of RobCo-Core tools/fetch_firmware.sh."""
    with open(script, encoding="utf-8") as f:
        text = f.read()

    def one(pattern, what):
        found = re.findall(pattern, text, re.M)
        if len(found) != 1:
            raise Error(f"{script}: expected one {what}, found {len(found)}")
        return found[0]

    tag = one(r'^FIRMWARE_TAG="([^"$]+)"$', "FIRMWARE_TAG")
    commit = one(r'^FIRMWARE_COMMIT="([0-9a-f]{40})"$', "FIRMWARE_COMMIT")
    base = one(r'^FIRMWARE_URL="\$\{FIRMWARE_URL:-([^"]+)\}"$', "FIRMWARE_URL default")
    base = base.replace("${FIRMWARE_COMMIT}", commit)
    if "$" in base:
        raise Error(f"{script}: FIRMWARE_URL holds a variable other than FIRMWARE_COMMIT")
    files = [{"name": n, "sha256": one(rf"^\s*{re.escape(n)}\)\s+echo ([0-9a-f]{{64}}) ;;$", n)}
             for n in FIRMWARE_FILES]
    return {"repo": FIRMWARE_REPO, "tag": tag, "commit": commit, "base_url": base, "files": files}


def manifest_bytes(version, abi, built_from, tools, firmware, files):
    """sdk.json, two-space indent, keys in the order of the spec's table."""
    doc = {
        "format": 1,
        "name": NAME,
        "version": version,
        "abi": abi,
        "built_from": built_from,
        "formats": FORMATS,
        "requires": [],
        "tools": tools,
        "firmware": firmware,
        "licences": LICENCES,
        "files": files,
    }
    return (json.dumps(doc, indent=2, ensure_ascii=False) + "\n").encode("utf-8")


def zip_info(name):
    info = zipfile.ZipInfo(name, ZIP_DATE)
    info.compress_type = zipfile.ZIP_STORED
    info.create_system = 3
    info.external_attr = ZIP_MODE << 16
    return info


def write_zip(path, entries):
    """entries: (name, bytes) in archive order. Written under a temporary
    name and renamed, so a failure leaves no archive."""
    tmp = path + ".tmp"
    with zipfile.ZipFile(tmp, "w") as z:
        for name, data in entries:
            z.writestr(zip_info(name), data)
    os.replace(tmp, path)


def build(args):
    abi = abi_of(args.abi_header)
    version = f"{abi}.{args.build}"
    if not VERSION_RE.fullmatch(version) or not all(part_ok(p) for p in version.split(".")):
        raise Error(f"version {version} is not X.Y.Z, each 0 to 65535 with no leading zero")
    # SDK_PACKAGE.md "Tools a package names", rule 3.
    if args.gcc_printed != args.gcc.split("-", 1)[0]:
        raise Error(f"the compiler prints {args.gcc_printed}, and gcc release {args.gcc} "
                    f"needs {args.gcc.split('-', 1)[0]}")
    built_from = []
    for item in args.built_from:
        repo, _, commit = item.partition("=")
        if not repo or not HEX40.fullmatch(commit):
            raise Error(f"--built-from {item}: want REPO=<40 lower case hex digits>")
        built_from.append({"repo": repo, "commit": commit})
    sources = {}
    for item in args.file:
        path, _, src = item.partition("=")
        if not path or not src:
            raise Error(f"--file {item}: want PATH=SOURCE")
        if path in sources:
            raise Error(f"--file {path} is given twice")
        if os.path.islink(src) or not os.path.isfile(src):
            raise Error(f"--file {path}: {src} is not a regular file")
        sources[path] = src
    files, entries = [], []
    for path in sorted(sources, key=lambda p: p.encode("utf-8")):
        with open(sources[path], "rb") as f:
            data = f.read()
        files.append({"path": path, "size": len(data), "sha256": sha256(data)})
        entries.append((path, data))
    tools = [{"name": "gcc", "version": args.gcc}, {"name": "qemu", "version": args.qemu}]
    manifest = manifest_bytes(version, abi, built_from, tools,
                              firmware_of(args.firmware_script), files)
    os.makedirs(args.out, exist_ok=True)
    out = os.path.join(args.out, f"{NAME}-{version}.zip")
    write_zip(out, [(MANIFEST, manifest)] + entries)
    print(out)
    return 0


# ---- check -------------------------------------------------------------------------

def same_but_case(a, b):
    return a.lower() == b.lower() or a.upper() == b.upper()


def name_problem(name):
    """Why an entry name breaks the name rules, or None."""
    body = name[:-1] if name.endswith("/") else name
    if not body:
        return "is empty"
    if "\\" in body:
        return "uses \\ as a separator"
    if body.startswith("/"):
        return "is absolute"
    if len(body) >= 2 and body[0].isascii() and body[0].isalpha() and body[1] == ":":
        return "starts with a drive letter"
    if any(unicodedata.category(c) == "Cc" for c in body):
        return "holds a control character"
    if any(c in ("", ".", "..") for c in body.split("/")):
        return "has an empty, . or .. component"
    for part in body.split("/"):
        why = portable_problem(part)
        if why:
            return f"has the part {part!r}, which {why}"
    return None


# The spec does not state the portable name rule yet. RobCo Studio refuses a
# package whose entry name breaks it, so check refuses it too: each part
# must be one plain name on Linux, macOS and Windows alike.
NOT_PORTABLE = set('<>:"/\\|?*')
DEVICES = {"con", "prn", "aux", "nul"} | {f"{d}{n}" for d in ("com", "lpt") for n in range(1, 10)}


def portable_problem(part):
    """Why one part of a name is not a plain name on every system, or None."""
    if part.endswith((".", " ")):
        return "ends with a dot or a space"
    if any(c in NOT_PORTABLE for c in part):
        return 'holds one of < > : " / \\ | ? *'
    stem = part.split(".", 1)[0].rstrip(" ").lower()
    if stem in DEVICES:
        return "is a device name of Windows"
    return None


def entry_kind(info):
    """'file', 'dir', or why the entry is neither."""
    mode = info.external_attr >> 16
    if info.create_system == 3 and stat.S_IFMT(mode):
        if stat.S_ISLNK(mode):
            return "is a symbolic link"
        if stat.S_ISDIR(mode):
            return "dir" if info.filename.endswith("/") else "is a folder whose name lacks /"
        if not stat.S_ISREG(mode):
            return "is not a regular file or a folder"
    if info.filename.endswith("/"):
        return "dir"
    return "file"


def check_manifest(doc, fail):
    """The fields of sdk.json a pipos package must carry. Returns files or None."""
    if not is_dict(doc):
        fail("sdk.json: the top value is not an object")
        return None
    want = ["format", "name", "version", "abi", "built_from", "formats", "requires",
            "tools", "firmware", "licences", "files"]
    for k in want:
        if k not in doc:
            fail(f"sdk.json: {k} is missing")
    if not is_int(doc.get("format")) or doc["format"] != 1:
        fail("sdk.json: format is not 1")
    if not is_str(doc.get("name")) or doc["name"] != NAME:
        fail(f"sdk.json: name is not {NAME}")
    version = doc.get("version")
    if not is_str(version) or not VERSION_RE.fullmatch(version) \
            or not all(part_ok(p) for p in version.split(".")):
        fail("sdk.json: version is not X.Y.Z with no leading zero")
    elif not is_str(doc.get("abi")) or doc["abi"] != ".".join(version.split(".")[:2]):
        fail("sdk.json: abi is not the first two parts of version")
    bf = doc.get("built_from")
    if not is_list(bf) or not bf or not all(
            is_dict(e) and is_str(e.get("repo")) and e["repo"]
            and is_str(e.get("commit")) and HEX40.fullmatch(e["commit"]) for e in bf):
        fail("sdk.json: built_from is not a list of repo and 40-digit commit")
    formats = doc.get("formats")
    if not is_dict(formats) or sorted(formats) != sorted(FORMATS) or not all(
            is_int(formats[k]) and formats[k] == v for k, v in FORMATS.items()):
        fail(f"sdk.json: formats is not {json.dumps(FORMATS)}")
    if not is_list(doc.get("requires")) or doc["requires"]:
        fail("sdk.json: requires is not empty")
    tools = doc.get("tools")
    if not is_list(tools) or [t.get("name") if is_dict(t) else None
                              for t in tools] != ["gcc", "qemu"] or not all(
            is_str(t.get("version")) and re.fullmatch(r"[\x21-\x7e]{1,64}", t["version"])
            and '"' not in t["version"] and "\\" not in t["version"] for t in tools):
        fail("sdk.json: tools is not gcc then qemu, each with a version")
    fw = doc.get("firmware")
    if not is_dict(fw) or not is_str(fw.get("repo")) or fw["repo"] != FIRMWARE_REPO \
            or not is_str(fw.get("tag")) or not is_str(fw.get("base_url")) \
            or not is_str(fw.get("commit")) or not HEX40.fullmatch(fw["commit"]) \
            or not is_list(fw.get("files")) \
            or sorted(f.get("name") if is_dict(f) and is_str(f.get("name")) else ""
                      for f in fw["files"]) != sorted(FIRMWARE_FILES) \
            or not all(is_str(f.get("sha256")) and HEX64.fullmatch(f["sha256"])
                       for f in fw["files"]):
        fail("sdk.json: firmware is not the pins of bootcode.bin, start.elf and fixup.dat")
    lic = doc.get("licences")
    if not is_list(lic) or not lic or not all(
            is_dict(e) and is_str(e.get("spdx")) and e["spdx"]
            and is_list(e.get("paths")) and all(is_str(p) for p in e["paths"])
            for e in lic):
        fail("sdk.json: licences is not a list of paths and spdx")
    elif any("GPL" in e["spdx"].upper() for e in lic):
        # "What a package must never contain": no file under the GPL in
        # pipos. The check reads what sdk.json declares; it cannot read the
        # licence of a file's bytes.
        fail("sdk.json: licences names a GPL licence, and pipos holds no file under the GPL")
    files = doc.get("files")
    if not is_list(files):
        fail("sdk.json: files is not an array")
        return None
    out, prev = {}, None
    for i, e in enumerate(files):
        if not is_dict(e) or not is_str(e.get("path")) \
                or not is_int(e.get("size")) \
                or e["size"] < 0 or not is_str(e.get("sha256")) \
                or not HEX64.fullmatch(e["sha256"]):
            fail(f"sdk.json: files[{i}] is not a path, a size and a lower case SHA-256")
            continue
        p = e["path"]
        why = name_problem(p) if not p.endswith("/") else "ends with /"
        if why:
            fail(f"sdk.json: files: {p!r}: the path {why}")
        twin = next((q for q in out if q != p and same_but_case(q, p)), None)
        if twin is not None:
            fail(f"sdk.json: files: {p} differs only in case from {twin}")
        if prev is not None and prev.encode("utf-8") >= p.encode("utf-8"):
            fail(f"sdk.json: files: {p} is not after {prev} in byte order")
        prev = p
        if same_but_case(p, MANIFEST):
            fail(f"sdk.json: files lists {p}, the manifest")
        out[p] = e
    return out


def check_file_name(path, doc, fail):
    """SDK_PACKAGE.md "The archive": the file is named <name>-<version>.zip,
    with the name and the version of its sdk.json."""
    if not is_dict(doc):
        return
    name, version = doc.get("name"), doc.get("version")
    want = f"{name}-{version}.zip" if is_str(name) and is_str(version) else None
    got = os.path.basename(path)
    if got != want:
        fail(f"{got}: the file name is not <name>-<version>.zip of its sdk.json"
             + (f", {want}" if want else ""))


def forbidden_texts(args):
    """The texts the search looks for: each --forbid, and each --user in the
    forms of USER_FORMS. Raises Error on a text shorter than FORBID_MIN."""
    texts = list(args.forbid)
    for user in args.user:
        texts += [form.format(user) for form in USER_FORMS]
    for t in texts:
        if len(t) < FORBID_MIN:
            raise Error(f"forbidden text {t!r} is shorter than {FORBID_MIN} characters, "
                        "so the search would not mean anything")
    # The same text twice, such as --forbid $HOME and the home folder of
    # --user, would report each hit twice.
    return list(dict.fromkeys(texts))


# A hit is shown with the printable run of bytes around it, at most this
# many bytes on each side, so a log names the whole path that got in.
RUN_SIDE = 80


def printable_run(data, at, length):
    """The run of printable ASCII bytes that holds data[at:at + length],
    cut to RUN_SIDE bytes on each side, as text. A cut end gets '...'."""
    start = at
    while start > 0 and at - start < RUN_SIDE and 0x20 <= data[start - 1] < 0x7f:
        start -= 1
    end = at + length
    while end < len(data) and end - at - length < RUN_SIDE and 0x20 <= data[end] < 0x7f:
        end += 1
    text = data[start:end].decode("utf-8", "replace")
    if start > 0 and 0x20 <= data[start - 1] < 0x7f:
        text = "..." + text
    if end < len(data) and 0x20 <= data[end] < 0x7f:
        text += "..."
    return text


def check(args):
    forbid = forbidden_texts(args)
    problems = []
    fail = problems.append
    try:
        z = zipfile.ZipFile(args.zip)
    except UnicodeDecodeError as e:
        print(f"FAIL: {args.zip}: an entry marked UTF-8 has a name that is not UTF-8: {e}")
        return 1
    except (OSError, zipfile.BadZipFile) as e:
        print(f"FAIL: {args.zip}: not a zip archive: {e}")
        return 1
    with z:
        # Each entry with its name as RobCo Studio reads it: the raw bytes as
        # UTF-8, whether or not the entry is marked UTF-8.
        entries = []
        for info in z.infolist():
            n = info.filename
            if not info.flag_bits & UTF8_FLAG:
                try:
                    n = n.encode("cp437").decode("utf-8")
                except UnicodeDecodeError:
                    fail(f"{n!r}: the name is not UTF-8")
            entries.append((info, n))
        names, seen = [], {}
        for info, n in entries:
            why = name_problem(n)
            if why:
                fail(f"{n!r}: the name {why}")
            if info.compress_type not in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED):
                fail(f"{n}: compression {info.compress_type} is not stored or deflate")
            kind = entry_kind(info)
            if kind not in ("file", "dir"):
                fail(f"{n}: the entry {kind}")
            key = n.rstrip("/")
            for other in seen:
                if other == key:
                    fail(f"{n}: the name comes twice")
                elif same_but_case(other, key):
                    fail(f"{n}: differs only in case from {seen[other]}")
            seen[key] = n
            # The spec does not state this yet: RobCo Studio writes RECORD
            # into the installed folder, so it refuses an entry of that name.
            if same_but_case(key, RECORD):
                fail(f"{n}: an entry named {RECORD} would clash with the install record")
            if kind == "file":
                names.append(n)
            base = key.rsplit("/", 1)[-1]
            if base in FIRMWARE_FILES:
                fail(f"{n}: a Pi firmware file is never part of a package")
        # A file cannot also be the folder of another entry.
        files_of = {k for k in seen if not seen[k].endswith("/") and seen[k] in names}
        for key in seen:
            parts = key.split("/")
            for i in range(1, len(parts)):
                parent = "/".join(parts[:i])
                if any(same_but_case(f, parent) for f in files_of):
                    fail(f"{seen[key]}: {parent} is a file of the archive")
                    break
        # Each file entry is read once. One that cannot be read is a failure
        # with its name, and its bytes are neither hashed nor searched.
        data_of = {}
        for info, n in entries:
            if n not in names or n in data_of:
                continue
            # The spec does not state this limit yet. RobCo Studio refuses an
            # sdk.json over it.
            if n == MANIFEST and info.file_size > MANIFEST_MAX:
                fail(f"sdk.json: states {info.file_size} bytes, over the limit of "
                     f"{MANIFEST_MAX} bytes")
                continue
            # ZipFile.read reads the stated size and checks the CRC, so a
            # stated size that lies fails here.
            try:
                data_of[n] = z.read(info)
            except READ_ERRORS as e:
                fail(f"{n}: cannot be read: {type(e).__name__}: {e}")
        listed = None
        if MANIFEST not in names:
            fail("sdk.json: not at the archive root")
        elif MANIFEST in data_of:
            try:
                doc = json.loads(data_of[MANIFEST].decode("utf-8"))
            except (UnicodeDecodeError, ValueError) as e:
                fail(f"sdk.json: not UTF-8 JSON: {e}")
            else:
                listed = check_manifest(doc, fail)
                check_file_name(args.zip, doc, fail)
        if listed is not None:
            for n in names:
                if n == MANIFEST:
                    continue
                if n not in listed:
                    fail(f"{n}: in the archive and not listed in sdk.json files")
                    continue
                if n not in data_of:
                    continue
                data = data_of[n]
                if len(data) != listed[n]["size"]:
                    fail(f"{n}: size {len(data)}, sdk.json says {listed[n]['size']}")
                if sha256(data) != listed[n]["sha256"]:
                    fail(f"{n}: SHA-256 {sha256(data)}, sdk.json says {listed[n]['sha256']}")
            for p in listed:
                if p not in names:
                    fail(f"{p}: listed in sdk.json files and not in the archive")
            want = set(LAYOUT) | {f"include/pipos/{h}" for h in HEADERS}
            for p in sorted(want - set(listed)):
                fail(f"{p}: the pipos layout needs it and sdk.json does not list it")
            for p in sorted(set(listed) - want):
                if p == "include/pipos/pipos.h":
                    fail(f"{p}: the pipos layout has no file of this name")
                elif not HEADER_RE.fullmatch(p):
                    fail(f"{p}: not a file of the pipos layout")
        for n, data in data_of.items():
            for f in forbid:
                at = data.find(f.encode("utf-8"))
                if at >= 0:
                    fail(f"{n}: holds {f!r} of the build machine at offset {at}, "
                         f"in {printable_run(data, at, len(f.encode('utf-8')))!r}")
    for p in problems:
        print(f"FAIL: {p}")
    if not problems:
        print(f"ok: {args.zip}: {len(names)} files, every rule holds")
    return 1 if problems else 0


def plant(args):
    """A copy of ZIP whose PATH ends with TEXT, its sdk.json entry updated,
    so CI can show that the search fails on a real package."""
    with zipfile.ZipFile(args.zip) as z:
        items = [(i.filename, z.read(i)) for i in z.infolist()]
    data = dict(items)
    if args.path not in data or args.path == MANIFEST:
        raise Error(f"{args.path}: not a listed file of {args.zip}")
    data[args.path] += args.text.encode("utf-8")
    doc = json.loads(data[MANIFEST])
    for e in doc["files"]:
        if e["path"] == args.path:
            e["size"], e["sha256"] = len(data[args.path]), sha256(data[args.path])
    data[MANIFEST] = (json.dumps(doc, indent=2, ensure_ascii=False) + "\n").encode("utf-8")
    write_zip(args.out, [(n, data[n]) for n, _ in items])
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build")
    b.add_argument("--out", required=True)
    b.add_argument("--abi-header", required=True)
    b.add_argument("--build", required=True, help="the third part of the version")
    b.add_argument("--gcc", required=True, help="the xPack release, such as 15.2.1-1.1")
    b.add_argument("--gcc-printed", required=True, help="what gcc -dumpfullversion prints")
    b.add_argument("--qemu", required=True)
    b.add_argument("--firmware-script", required=True)
    b.add_argument("--built-from", action="append", default=[])
    b.add_argument("--file", action="append", default=[])
    c = sub.add_parser("check")
    c.add_argument("zip")
    c.add_argument("--forbid", action="append", default=[],
                   help="a text no file may hold, such as a folder or the host name")
    c.add_argument("--user", action="append", default=[],
                   help="a user name, searched as a home folder and as name@")
    p = sub.add_parser("plant")
    p.add_argument("zip")
    p.add_argument("out")
    p.add_argument("path")
    p.add_argument("text")
    args = ap.parse_args(argv)
    try:
        return {"build": build, "check": check, "plant": plant}[args.cmd](args)
    except Error as e:
        print(f"sdk_package: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
