#!/bin/sh
# Checks the ROM and the holotape on the card from make sdimage: the
# hello and tape examples by default, the files ROM and TAPE name, and
# none with ROM=none or TAPE=none. The tape does not depend on ROM.
# Reads the card with mtools.
set -eu
# Build settings from the caller's environment or an outer make
# (make CI=1 passes CI=1 down in MAKEFLAGS) must not change the builds here.
unset CI MAKEFLAGS MFLAGS MAKELEVEL BUILD ROM BOARD PIPOS_SDK ROBCO_CORE CFLAGS_EXTRA KIND WINDOW TAPE DIR FIRMWARE CLEAN CARD_DIR
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
mcopy -n -i "$card" ::/PIPBOY/HOLO/BEACON.HTP "$tmp/got.htp" || fail "default card has no PIPBOY/HOLO/BEACON.HTP"
cmp -s "$tmp/got.htp" "$b/examples/tape/BEACON.HTP" || fail "PIPBOY/HOLO/BEACON.HTP is not the tape example"

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
mdir -b -i "$card" ::/PIPBOY/HOLO | grep -q 'BEACON\.HTP$' || fail "ROM=none card lost the tape"

printf 'not a tape\n' > "$tmp/OTHER.HTP"
make -s sdimage BUILD="$b" TAPE="$tmp/OTHER.HTP" >/dev/null
mdir -b -i "$card" ::/PIPBOY/HOLO >"$tmp/holo"
grep -q 'OTHER\.HTP$' "$tmp/holo" || fail "TAPE=<file> did not put that file in PIPBOY/HOLO"
if grep -q 'BEACON\.HTP$' "$tmp/holo"; then
    fail "TAPE=<file> card still has BEACON.HTP"
fi

make -s sdimage BUILD="$b" TAPE=none >/dev/null
mdir -b -i "$card" ::/PIPBOY >"$tmp/list"
grep -q 'ROM1\.ROM$' "$tmp/list" || fail "TAPE=none card lost PIPBOY/ROM1.ROM"
if mdir -b -i "$card" ::/PIPBOY/HOLO 2>/dev/null | grep -q 'HTP$'; then
    fail "TAPE=none card still has a holotape"
fi

# Tape names follow FAT: two names that differ only in case are one file,
# and a name that is not *.HTP is never listed. Both stop the build and
# name the files. A CARD_DIR file with a tape's name stops it too.
mkdir -p "$tmp/a" "$tmp/b"
printf 'one\n' >"$tmp/a/ONE.HTP"
printf 'two\n' >"$tmp/b/one.htp"
if make -s sdimage BUILD="$b" TAPE="$tmp/a/ONE.HTP $tmp/b/one.htp" >"$tmp/out" 2>&1; then
    fail "TAPE with ONE.HTP and one.htp did not stop"
fi
grep -q "$tmp/a/ONE.HTP and $tmp/b/one.htp" "$tmp/out" || fail "the case clash message does not name both files"

printf 'notes\n' >"$tmp/NOTES.TXT"
if make -s sdimage BUILD="$b" TAPE="$tmp/NOTES.TXT" >"$tmp/out" 2>&1; then
    fail "TAPE=NOTES.TXT did not stop"
fi
grep -q "$tmp/NOTES.TXT does not end in .HTP" "$tmp/out" || fail "the .HTP message does not name the file"

# A folder given as TAPE or ROM stops sdimage and bootdir alike.
mkdir -p "$tmp/X.HTP/sub"
for args in "TAPE=$tmp/X.HTP" "ROM=$tmp/X.HTP"; do
    for target in sdimage "bootdir DIR=$tmp/bootdir FIRMWARE=0"; do
        # shellcheck disable=SC2086
        if make -s $target BUILD="$b" "$args" >"$tmp/out" 2>&1; then
            fail "$target $args with a folder did not stop"
        fi
        grep -q "$tmp/X.HTP is a folder: use a regular file" "$tmp/out" || fail "$target $args: the folder message does not name it"
    done
done
[ ! -e "$tmp/bootdir" ] || fail "bootdir with a folder TAPE or ROM wrote DIR"

mkdir -p "$tmp/content/pipboy/holo"
printf 'clash\n' >"$tmp/content/pipboy/holo/other.htp"
if make -s sdimage BUILD="$b" TAPE="$tmp/OTHER.HTP" CARD_DIR="$tmp/content" >"$tmp/out" 2>&1; then
    fail "CARD_DIR pipboy/holo/other.htp next to TAPE OTHER.HTP did not stop"
fi
grep -q 'pipboy/holo/other.htp clashes with the standard PIPBOY/HOLO/OTHER.HTP' "$tmp/out" ||
    fail "the CARD_DIR tape clash message does not name both files"

# A long *.HTP name is listed under a FAT short name: a warning only.
printf 'long\n' >"$tmp/LONGNAME1.HTP"
printf 'eight\n' >"$tmp/ABCDEFGH.HTP"
make -s sdimage BUILD="$b" TAPE="$tmp/LONGNAME1.HTP $tmp/ABCDEFGH.HTP" >"$tmp/out" 2>&1 ||
    fail "a long tape name stopped the build: $(cat "$tmp/out")"
grep -q "warning: TAPE file $tmp/LONGNAME1.HTP is not a plain 8.3" "$tmp/out" || fail "no warning for LONGNAME1.HTP"
if grep 'warning' "$tmp/out" | grep -q 'ABCDEFGH'; then
    fail "ABCDEFGH.HTP, a plain 8.3 name, got a warning"
fi
echo "sdimage_rom_test: passed"
