#!/usr/bin/env python3
"""Writes and checks the pipos SDK package, pipos-<version>.zip.

    sdk_package.py build --out DIR --abi-header H --build N --gcc RELEASE
        --gcc-printed VERSION --qemu VERSION --firmware-script SCRIPT
        --built-from REPO=COMMIT... --file PATH=SOURCE...
    sdk_package.py check ZIP [--forbid TEXT]...

`build` writes sdk.json and the archive. The archive bytes depend only on
the files and the arguments: entries in byte order of their names with
sdk.json first, stored, dated 1980-01-01 00:00, mode 0644, no extra fields.

`check` applies the archive rules of RobCo-Core docs/spec/SDK_PACKAGE.md
and the pipos layout, and searches every file, sdk.json included, for each
--forbid text: the build folder, the user name and the host name. It prints
one FAIL line per broken rule and exits 1 when there is one.

Stdlib only.
"""

import argparse
import hashlib
import json
import os
import re
import stat
import sys
import zipfile

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
    if any(ord(c) < 0x20 or ord(c) == 0x7F for c in body):
        return "holds a control character"
    if any(c in ("", ".", "..") for c in body.split("/")):
        return "has an empty, . or .. component"
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
    if not isinstance(doc, dict):
        fail("sdk.json: the top value is not an object")
        return None
    want = ["format", "name", "version", "abi", "built_from", "formats", "requires",
            "tools", "firmware", "licences", "files"]
    for k in want:
        if k not in doc:
            fail(f"sdk.json: {k} is missing")
    if doc.get("format") != 1 or isinstance(doc.get("format"), bool):
        fail("sdk.json: format is not 1")
    if doc.get("name") != NAME:
        fail(f"sdk.json: name is not {NAME}")
    version = doc.get("version")
    if not isinstance(version, str) or not VERSION_RE.fullmatch(version) \
            or not all(part_ok(p) for p in version.split(".")):
        fail("sdk.json: version is not X.Y.Z with no leading zero")
    elif doc.get("abi") != ".".join(version.split(".")[:2]):
        fail("sdk.json: abi is not the first two parts of version")
    bf = doc.get("built_from")
    if not isinstance(bf, list) or not bf or not all(
            isinstance(e, dict) and isinstance(e.get("repo"), str) and e["repo"]
            and isinstance(e.get("commit"), str) and HEX40.fullmatch(e["commit"]) for e in bf):
        fail("sdk.json: built_from is not a list of repo and 40-digit commit")
    if doc.get("formats") != FORMATS:
        fail(f"sdk.json: formats is not {json.dumps(FORMATS)}")
    if doc.get("requires") != []:
        fail("sdk.json: requires is not empty")
    tools = doc.get("tools")
    if not isinstance(tools, list) or [t.get("name") if isinstance(t, dict) else None
                                       for t in tools] != ["gcc", "qemu"] or not all(
            isinstance(t.get("version"), str) and re.fullmatch(r"[\x21-\x7e]{1,64}", t["version"])
            and '"' not in t["version"] and "\\" not in t["version"] for t in tools):
        fail("sdk.json: tools is not gcc then qemu, each with a version")
    fw = doc.get("firmware")
    if not isinstance(fw, dict) or fw.get("repo") != FIRMWARE_REPO \
            or not isinstance(fw.get("tag"), str) or not isinstance(fw.get("base_url"), str) \
            or not isinstance(fw.get("commit"), str) or not HEX40.fullmatch(fw["commit"]) \
            or not isinstance(fw.get("files"), list) \
            or sorted(f.get("name", "") if isinstance(f, dict) else "" for f in fw["files"]) \
            != sorted(FIRMWARE_FILES) \
            or not all(isinstance(f.get("sha256"), str) and HEX64.fullmatch(f["sha256"])
                       for f in fw["files"]):
        fail("sdk.json: firmware is not the pins of bootcode.bin, start.elf and fixup.dat")
    lic = doc.get("licences")
    if not isinstance(lic, list) or not lic or not all(
            isinstance(e, dict) and isinstance(e.get("spdx"), str) and e["spdx"]
            and isinstance(e.get("paths"), list) and all(isinstance(p, str) for p in e["paths"])
            for e in lic):
        fail("sdk.json: licences is not a list of paths and spdx")
    files = doc.get("files")
    if not isinstance(files, list):
        fail("sdk.json: files is not an array")
        return None
    out, prev = {}, None
    for i, e in enumerate(files):
        if not isinstance(e, dict) or not isinstance(e.get("path"), str) \
                or not isinstance(e.get("size"), int) or isinstance(e.get("size"), bool) \
                or e["size"] < 0 or not isinstance(e.get("sha256"), str) \
                or not HEX64.fullmatch(e["sha256"]):
            fail(f"sdk.json: files[{i}] is not a path, a size and a lower case SHA-256")
            continue
        p = e["path"]
        if prev is not None and prev.encode("utf-8") >= p.encode("utf-8"):
            fail(f"sdk.json: files: {p} is not after {prev} in byte order")
        prev = p
        if same_but_case(p, MANIFEST):
            fail(f"sdk.json: files lists {p}, the manifest")
        out[p] = e
    return out


def check(args):
    problems = []
    fail = problems.append
    for f in args.forbid:
        if not f:
            fail("--forbid: an empty text matches everything")
    try:
        z = zipfile.ZipFile(args.zip)
    except (OSError, zipfile.BadZipFile) as e:
        print(f"FAIL: {args.zip}: not a zip archive: {e}")
        return 1
    with z:
        infos = z.infolist()
        names, seen = [], {}
        for info in infos:
            n = info.filename
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
            if kind == "file":
                names.append(n)
            base = key.rsplit("/", 1)[-1]
            if base in FIRMWARE_FILES:
                fail(f"{n}: a Pi firmware file is never part of a package")
        listed = None
        if MANIFEST not in names:
            fail("sdk.json: not at the archive root")
        else:
            try:
                doc = json.loads(z.read(MANIFEST).decode("utf-8"))
            except (UnicodeDecodeError, ValueError) as e:
                fail(f"sdk.json: not UTF-8 JSON: {e}")
            else:
                listed = check_manifest(doc, fail)
        if listed is not None:
            for n in names:
                if n == MANIFEST:
                    continue
                if n not in listed:
                    fail(f"{n}: in the archive and not listed in sdk.json files")
                    continue
                data = z.read(n)
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
                if not HEADER_RE.fullmatch(p):
                    fail(f"{p}: not a file of the pipos layout")
        for n in names:
            data = z.read(n)
            for f in args.forbid:
                if f and f.encode("utf-8") in data:
                    at = data.index(f.encode("utf-8"))
                    fail(f"{n}: holds {f!r} of the build machine at offset {at}")
    for p in problems:
        print(f"FAIL: {p}")
    if not problems:
        print(f"ok: {args.zip}: {len(names)} files, every rule holds")
    return 1 if problems else 0


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
    c.add_argument("--forbid", action="append", default=[])
    args = ap.parse_args(argv)
    try:
        return build(args) if args.cmd == "build" else check(args)
    except Error as e:
        print(f"sdk_package: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
