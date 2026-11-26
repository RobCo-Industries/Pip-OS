#!/bin/sh
# Builds sdk/examples/hello the way a ROM author does: from its own folder,
# and from a copy outside the tree with an absolute and a relative
# PIPOS_SDK. Also builds it as a holotape, and builds sdk/examples/tape
# in the tree and from a copy. Every result must pass rimg.py verify with
# the right magic, subtype and load address. Last, the SUBTYPE setting:
# utility tapes, a reseal when it changes, and the values it refuses.
set -eu
# Build settings from the caller's environment or an outer make
# (make CI=1 passes CI=1 down in MAKEFLAGS) must not change the builds here.
unset CI MAKEFLAGS MFLAGS MAKELEVEL BUILD ROM BOARD PIPOS_SDK ROBCO_CORE CFLAGS_EXTRA KIND WINDOW TAPE SUBTYPE
root=$(cd "$(dirname "$0")/../.." && pwd)
rimg="$root/external/robco-core/tools/rimg.py"
tmp=$(cd "$(mktemp -d)" && pwd -P)
trap 'rm -rf "$tmp"' EXIT

fail() {
    echo "sdk_example_test: $*" >&2
    exit 1
}

# expect FILE MAGIC LOAD_ADDR: verify, then check the header fields.
expect() {
    python3 "$rimg" verify "$1" --magic "$2" >/dev/null || fail "$1 does not verify as $2"
    info=$(python3 "$rimg" info "$1")
    for want in "magic=$2" "load_addr=$3" "subtype=0x0" "name=HELLO" \
                "image_version=0x10000" "title=Hello from the SDK" "crc_ok=1"; do
        printf '%s\n' "$info" | grep -qx "$want" || fail "$1: no $want in: $info"
    done
}

# 1. From its own folder, PIPOS_SDK as the example sets it.
make -s -C "$root/sdk/examples/hello" BUILD="$tmp/intree" >/dev/null
expect "$tmp/intree/ROM1.ROM" ROM1 0x400000

# 2. A copy outside the tree, absolute PIPOS_SDK, default BUILD.
cp -R "$root/sdk/examples/hello" "$tmp/proj"
rm -rf "$tmp/proj/build"
make -s -C "$tmp/proj" PIPOS_SDK="$root/sdk" >/dev/null
expect "$tmp/proj/build/ROM1.ROM" ROM1 0x400000

# 3. The same copy with a relative PIPOS_SDK.
rel=$(python3 -c 'import os,sys; print(os.path.relpath(sys.argv[1], sys.argv[2]))' "$root/sdk" "$tmp/proj")
make -s -C "$tmp/proj" PIPOS_SDK="$rel" BUILD=relbuild >/dev/null
expect "$tmp/proj/relbuild/ROM1.ROM" ROM1 0x400000

# 4. A holotape: ROBCO78, linked at 0x00800000.
make -s -C "$tmp/proj" PIPOS_SDK="$root/sdk" KIND=holotape IMAGE=HELLO.HTP BUILD=holo >/dev/null
expect "$tmp/proj/holo/HELLO.HTP" ROBCO78 0x800000
if python3 "$rimg" verify "$tmp/proj/holo/HELLO.HTP" --magic ROM1 >/dev/null 2>&1; then
    fail "the holotape verifies as ROM1"
fi

# 5. A bad KIND stops the build.
if make -s -C "$tmp/proj" PIPOS_SDK="$root/sdk" KIND=tape BUILD=bad >/dev/null 2>&1; then
    fail "KIND=tape did not fail"
fi
# 6. The tape example: a holotape with a title and an icon, in its own
#    folder and from a copy outside the tree.
# tape FILE [SUBTYPE]: the subtype defaults to 0, game.
tape() {
    python3 "$rimg" verify "$1" --magic ROBCO78 >/dev/null || fail "$1 does not verify as ROBCO78"
    info=$(python3 "$rimg" info "$1")
    for want in magic=ROBCO78 load_addr=0x800000 subtype=${2:-0x0} name=BEACON \
                image_version=0x10000 "title=Beacon sample tape" icon=set crc_ok=1; do
        printf '%s\n' "$info" | grep -qx "$want" || fail "$1: no $want in: $info"
    done
}
make -s -C "$root/sdk/examples/tape" BUILD="$tmp/tape" >/dev/null
tape "$tmp/tape/BEACON.HTP"
cp -R "$root/sdk/examples/tape" "$tmp/tapeproj"
rm -rf "$tmp/tapeproj/build"
make -s -C "$tmp/tapeproj" PIPOS_SDK="$root/sdk" >/dev/null
tape "$tmp/tapeproj/build/BEACON.HTP"

# 7. SUBTYPE=1 and SUBTYPE=utility seal a utility tape. Going back to the
#    default in the same BUILD reseals it as a game.
make -s -C "$tmp/tapeproj" PIPOS_SDK="$root/sdk" SUBTYPE=1 BUILD=util >/dev/null
tape "$tmp/tapeproj/util/BEACON.HTP" 0x1
make -s -C "$tmp/tapeproj" PIPOS_SDK="$root/sdk" BUILD=util >/dev/null
tape "$tmp/tapeproj/util/BEACON.HTP" 0x0
make -s -C "$tmp/tapeproj" PIPOS_SDK="$root/sdk" SUBTYPE=utility BUILD=util >/dev/null
tape "$tmp/tapeproj/util/BEACON.HTP" 0x1
make -s -C "$tmp/tapeproj" PIPOS_SDK="$root/sdk" SUBTYPE=game BUILD=util >/dev/null
tape "$tmp/tapeproj/util/BEACON.HTP" 0x0

# 8. Refused SUBTYPE values stop the build with a message and build nothing.
# refused WANT ARGS...: make ARGS fails and prints WANT.
refused() {
    want=$1
    shift
    if out=$(make -s -C "$tmp/tapeproj" PIPOS_SDK="$root/sdk" BUILD=refused "$@" 2>&1); then
        fail "$* did not fail"
    fi
    printf '%s\n' "$out" | grep -q "$want" || fail "$*: no '$want' in: $out"
    [ ! -e "$tmp/tapeproj/refused" ] || fail "$*: the refused build left $tmp/tapeproj/refused"
}
refused "SUBTYPE=2 is a data holotape" SUBTYPE=2
refused "SUBTYPE=data is a data holotape" SUBTYPE=data
refused "SUBTYPE=x1 is not valid for KIND=holotape" SUBTYPE=x1
refused "SUBTYPE=1 is not valid for KIND=rom" KIND=rom IMAGE=BEACON.ROM SUBTYPE=1
refused "SUBTYPE=0 game must be one word" "SUBTYPE=0 game"
echo "sdk_example_test: passed"
