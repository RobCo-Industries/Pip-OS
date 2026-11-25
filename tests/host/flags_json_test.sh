#!/bin/sh
# flags.json of pipos-app.mk: the example tape and the example ROM built
# by make and by tests/host/flags_json_build.py, which reads only the
# written flags.json, give the same bytes. No flag holds a path.
set -eu
unset CI MAKEFLAGS MFLAGS MAKELEVEL BUILD ROM BOARD PIPOS_SDK ROBCO_CORE CFLAGS_EXTRA KIND WINDOW TAPE SUBTYPE ABI FLAGS_JSON
root=$(cd "$(dirname "$0")/../.." && pwd)
rimg="$root/external/robco-core/tools/rimg.py"
tmp=$(cd "$(mktemp -d)" && pwd -P)
trap 'rm -rf "$tmp"' EXIT

fail() {
    echo "flags_json_test: $*" >&2
    exit 1
}

sum() {
    python3 -c 'import hashlib,sys; print(hashlib.sha256(open(sys.argv[1],"rb").read()).hexdigest())' "$1"
}

gcc_path=$(command -v arm-none-eabi-gcc) || fail "arm-none-eabi-gcc not found"
toolchain=$(cd "$(dirname "$gcc_path")/.." && pwd -P)

# 1. make writes flags.json. Every field of the spec is there, in order,
# with its type, and no flag holds a path.
make -s -C "$root/sdk/examples/tape" PIPOS_SDK="$root/sdk" BUILD="$tmp/mk" FLAGS_JSON="$tmp/flags.json" flags-json >/dev/null
python3 - "$tmp/flags.json" "$root" "$tmp" <<'EOF' || fail "flags.json does not follow the spec"
import json, sys
f = json.load(open(sys.argv[1], encoding="utf-8"))
want = [("format", int), ("cflags", list), ("include_dirs", list), ("ldflags", list),
        ("linker_scripts", dict), ("crt0", str), ("crt0_defines", dict), ("libs", list),
        ("system_libs", list), ("abi_define", str)]
assert [k for k, _ in want] == list(f), list(f)
for k, t in want:
    assert isinstance(f[k], t), k
assert f["format"] == 1
assert sorted(f["linker_scripts"]) == ["holotape", "rom"]
assert sorted(f["crt0_defines"]) == ["holotape-game", "holotape-utility", "rom"]
for k in ("cflags", "ldflags", "system_libs"):
    for flag in f[k]:
        assert isinstance(flag, str) and "/" not in flag and "\\" not in flag, (k, flag)
text = open(sys.argv[1], encoding="utf-8").read()
for p in sys.argv[2:]:
    assert p not in text, "a build path is in flags.json"
for k in ("include_dirs", "libs"):
    for p in f[k]:
        assert not p.startswith("/"), (k, p)
EOF

# 2. A package folder as SDK_PACKAGE.md lays it out: only the files the
# script may read.
pkg="$tmp/pkg"
mkdir -p "$pkg/lib"
cp -R "$root/sdk/include" "$pkg/include"
cp "$root/sdk/crt0.S" "$root/sdk/holotape.ld" "$root/sdk/rom.ld" "$pkg/lib/"
cp "$tmp/flags.json" "$pkg/flags.json"

# build EXAMPLE IMAGE NAME KIND SEAL-ARGS...: make, then the script, into
# two copies of the example; both images must be equal.
build() {
    ex=$1 image=$2 name=$3 kind=$4
    shift 4
    cp -R "$root/sdk/examples/$ex" "$tmp/make-$ex"
    rm -rf "$tmp/make-$ex/build"
    make -s -C "$tmp/make-$ex" PIPOS_SDK="$root/sdk" ABI=1.3 >/dev/null
    cp "$tmp/make-$ex/build/libpip/libpip.a" "$pkg/lib/libpip.a"
    cp -R "$root/sdk/examples/$ex" "$tmp/tool-$ex"
    rm -rf "$tmp/tool-$ex/build"
    python3 "$root/tests/host/flags_json_build.py" --pkg "$pkg" --project "$tmp/tool-$ex" \
        --toolchain "$toolchain" --rimg "$rimg" --kind "$kind" --abi 1.3 --name "$name" \
        --out "build/$image" "$@" main.c >/dev/null || fail "$ex: the script's build failed"
    a=$(sum "$tmp/make-$ex/build/$image")
    b=$(sum "$tmp/tool-$ex/build/$image")
    [ "$a" = "$b" ] || fail "$ex: make gives $a, flags.json gives $b"
    python3 "$rimg" info "$tmp/tool-$ex/build/$image" | grep -qx 'abi_version=0x10003' ||
        fail "$ex: abi_version is not 0x10003"
}

build tape BEACON.HTP BEACON holotape \
    --seal-arg=--image-version --seal-arg=1.0.0 \
    --seal-arg=--title --seal-arg="Beacon sample tape" \
    --seal-arg=--icon --seal-arg=icon.pbm
build hello ROM1.ROM HELLO rom \
    --seal-arg=--image-version --seal-arg=1.0.0 \
    --seal-arg=--title --seal-arg="Hello from the SDK"

echo "flags_json_test: ok"
