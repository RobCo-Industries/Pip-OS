#!/bin/sh
# Checks the ROM on the card from make sdimage: the hello example by
# default, the file ROM names, and none with ROM=none. Reads the card
# with mtools.
set -eu
# Build settings from the caller's environment or an outer make
# (make CI=1 passes CI=1 down in MAKEFLAGS) must not change the builds here.
unset CI MAKEFLAGS MFLAGS MAKELEVEL BUILD ROM BOARD PIPOS_SDK ROBCO_CORE CFLAGS_EXTRA KIND WINDOW
root=$(cd "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
b="$tmp/build"
card="$b/sd.img@@1M"

fail() {
    echo "sdimage_rom_test: $*" >&2
    exit 1
}

cd "$root"
make -s sdimage BUILD="$b" >/dev/null
mdir -b -i "$card" ::/PIPBOY | grep -q 'ROM1\.ROM$' || fail "default card has no PIPBOY/ROM1.ROM"
mcopy -n -i "$card" ::/PIPBOY/ROM1.ROM "$tmp/got.rom"
cmp -s "$tmp/got.rom" "$b/examples/hello/ROM1.ROM" || fail "PIPBOY/ROM1.ROM is not the hello example"

printf 'not a rom\n' > "$tmp/other.rom"
make -s sdimage BUILD="$b" ROM="$tmp/other.rom" >/dev/null
mcopy -n -i "$card" ::/PIPBOY/ROM1.ROM "$tmp/got2.rom"
cmp -s "$tmp/got2.rom" "$tmp/other.rom" || fail "ROM=<file> did not put that file on the card"

make -s sdimage BUILD="$b" ROM=none >/dev/null
mdir -b -i "$card" ::/PIPBOY >"$tmp/list"
grep -q 'PIPOS\.CFG$' "$tmp/list" || fail "ROM=none card lost PIPBOY/PIPOS.CFG"
if grep -q 'ROM1\.ROM$' "$tmp/list"; then
    fail "ROM=none card still has PIPBOY/ROM1.ROM"
fi
echo "sdimage_rom_test: passed"
