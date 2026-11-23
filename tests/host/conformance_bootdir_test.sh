#!/bin/sh
# Checks make conformance-bootdir: the folder holds the same files as the
# QEMU conformance card, under the same names. FIRMWARE=0 keeps it
# offline, so the firmware download is not tested here. BOOTDIR.LST,
# bootdir's list of what it wrote, is the one file the card lacks.
set -eu
# Build settings from the caller's environment or an outer make must not
# change the builds here.
unset CI MAKEFLAGS MFLAGS MAKELEVEL BUILD ROM BOARD PIPOS_SDK ROBCO_CORE CFLAGS_EXTRA KIND WINDOW TAPE DIR FIRMWARE
root=$(cd "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
b="$tmp/build"
dir="$tmp/card"

fail() {
    echo "conformance_bootdir_test: $*" >&2
    exit 1
}

cd "$root"
make -s conformance-bootdir BUILD="$b" DIR="$dir" FIRMWARE=0 >/dev/null
make -s conformance-card BUILD="$b" >/dev/null

# Both lists: one path per file, upper case, sorted.
(cd "$dir" && find . -type f | sed 's|^\./||' | grep -v -x 'BOOTDIR.LST' | tr '[:lower:]' '[:upper:]' | sort) >"$tmp/dir.list"
mdir -/ -b -i "$b/conformance.img@@1M" :: | grep -v '/$' | sed 's|^::/||' |
    tr '[:lower:]' '[:upper:]' | sort >"$tmp/card.list"
cmp -s "$tmp/dir.list" "$tmp/card.list" ||
    fail "files differ from build/conformance.img: $(diff "$tmp/card.list" "$tmp/dir.list" | tr '\n' ' ')"

for f in PIPBOY/ROM1.ROM PIPBOY/HOLO/CONFTAPE.HTP PIPBOY/HOLO/CONFDATA.HTP PIPBOY/PIPOS.CFG PIPOS.IMG; do
    mcopy -n -i "$b/conformance.img@@1M" "::/$f" "$tmp/got"
    cmp -s "$tmp/got" "$dir/$f" || fail "$f differs from the one on build/conformance.img"
done
cmp -s "$dir/PIPBOY/ROM1.ROM" "$b/conformance/rom/CONFORM.ROM" || fail "PIPBOY/ROM1.ROM is not the conformance ROM"
for f in bootcode.bin start.elf fixup.dat; do
    [ ! -e "$dir/$f" ] || fail "FIRMWARE=0 still wrote $f"
done
grep -qx 'PIPBOY/ROM1.ROM' "$dir/BOOTDIR.LST" || fail "BOOTDIR.LST does not name PIPBOY/ROM1.ROM"

# Run again with CLEAN=1 after the person drops a tape in by hand: the
# rules of bootdir apply, so the unknown tape stops it.
printf 'x\n' >"$dir/PIPBOY/HOLO/EXTRA.HTP"
if make -s conformance-bootdir BUILD="$b" DIR="$dir" FIRMWARE=0 CLEAN=1 >"$tmp/out" 2>&1; then
    fail "conformance-bootdir did not stop on PIPBOY/HOLO/EXTRA.HTP"
fi
grep -q 'PIPBOY/HOLO/EXTRA.HTP' "$tmp/out" || fail "conformance-bootdir stop does not list PIPBOY/HOLO/EXTRA.HTP"
echo "conformance_bootdir_test: passed"
