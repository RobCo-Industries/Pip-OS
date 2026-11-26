#!/usr/bin/env python3
"""Build a tape or ROM the way a tool reads flags.json.

Follows the steps of RobCo-Core's SDK_PACKAGE.md, "flags.json", "Build
order": compile crt0, compile each source, link, objcopy, seal. It reads
only flags.json and the package folder; it never reads pipos-app.mk.
Each command is an argument list run with no shell, in the project folder,
with the three path maps of "Path maps".
"""
import argparse
import json
import os
import subprocess
import sys

KEYS = ("format", "cflags", "include_dirs", "ldflags", "linker_scripts",
        "crt0", "crt0_defines", "libs", "system_libs", "abi_define")


def run(args, cwd):
    subprocess.run(args, cwd=cwd, check=True)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--pkg", required=True, help="the package folder")
    p.add_argument("--project", required=True, help="the project folder")
    p.add_argument("--toolchain", required=True, help="the folder that holds bin/")
    p.add_argument("--rimg", required=True)
    p.add_argument("--kind", required=True, choices=("holotape", "rom"))
    p.add_argument("--subtype", default="game", choices=("game", "utility"))
    p.add_argument("--abi", required=True, help="MAJOR.MINOR")
    p.add_argument("--name", required=True)
    p.add_argument("--out", required=True, help="the sealed file, relative to the project")
    p.add_argument("--seal-arg", action="append", default=[])
    p.add_argument("sources", nargs="+", help="relative to the project, in order")
    a = p.parse_args()

    with open(os.path.join(a.pkg, "flags.json"), "rb") as f:
        flags = json.loads(f.read().decode("utf-8"))
    assert isinstance(flags, dict), "flags.json: the top is not an object"
    assert flags.get("format") == 1, "flags.json: format"
    for k in KEYS:
        assert k in flags, "flags.json: %s missing" % k

    pkg = os.path.abspath(a.pkg)
    proj = os.path.abspath(a.project)
    tc = os.path.abspath(a.toolchain)
    gcc = os.path.join(tc, "bin", "arm-none-eabi-gcc")
    objcopy = os.path.join(tc, "bin", "arm-none-eabi-objcopy")
    major, minor = (int(x) for x in a.abi.split("."))
    abi = "-D%s=0x%08x" % (flags["abi_define"], (major << 16) | minor)
    incs = ["-I" + os.path.join(pkg, d) for d in flags["include_dirs"]]
    maps = ["-ffile-prefix-map=%s=." % proj,
            "-ffile-prefix-map=%s=toolchain" % tc,
            "-ffile-prefix-map=%s=sdk" % pkg]
    key = a.kind if a.kind == "rom" else "holotape-" + a.subtype
    os.makedirs(os.path.join(proj, "build", "obj"), exist_ok=True)

    # 1. The startup code.
    crt0_o = "build/obj/crt0.o"
    defs = ["-D" + d for d in flags["crt0_defines"][key]]
    run([gcc] + flags["cflags"] + [abi] + defs + incs + maps +
        ["-c", os.path.join(pkg, flags["crt0"]), "-o", crt0_o], proj)
    # 2. Each source, in order.
    objs = []
    for s in a.sources:
        o = "build/obj/%s.o" % s
        os.makedirs(os.path.join(proj, os.path.dirname(o)), exist_ok=True)
        run([gcc] + flags["cflags"] + [abi] + incs + maps + ["-c", s, "-o", o], proj)
        objs.append(o)
    # 3. The link.
    elf = "build/%s.elf" % a.name
    run([gcc] + flags["ldflags"] +
        ["-T", os.path.join(pkg, flags["linker_scripts"][a.kind]), crt0_o] + objs +
        [os.path.join(pkg, l) for l in flags["libs"]] + flags["system_libs"] +
        ["-o", elf], proj)
    # 4. objcopy.
    raw = "build/%s.bin" % a.name
    run([objcopy, "-O", "binary", elf, raw], proj)
    # 5. Seal.
    run([sys.executable, a.rimg, "seal", raw, "--name", a.name] + a.seal_arg, proj)
    os.replace(os.path.join(proj, raw), os.path.join(proj, a.out))


if __name__ == "__main__":
    main()
