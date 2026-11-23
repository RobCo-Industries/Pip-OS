#!/bin/sh
# Checks that make bootdir writes the same files, under the same names and
# with the same bytes, as the card of make sdimage from the same inputs:
# make conformance-bootdir against the conformance card, then bootdir
# against sdimage for the defaults, ROM=none TAPE=none, and a custom ROM
# with two tapes and a CARD_DIR. FIRMWARE=0 keeps it offline, so the
# firmware download is not tested here. BOOTDIR.LST, bootdir's list of
# what it wrote, is the one file the card lacks.
set -eu
# Build settings from the caller's environment or an outer make must not
# change the builds here.
unset CI MAKEFLAGS MFLAGS MAKELEVEL BUILD ROM BOARD PIPOS_SDK ROBCO_CORE CFLAGS_EXTRA KIND WINDOW TAPE DIR FIRMWARE CLEAN CARD_DIR
root=$(cd "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
b="$tmp/build"
dir="$tmp/card"

fail() {
    echo "conformance_bootdir_test: $*" >&2
    exit 1
}

# same <what> <card image> <expected files...>: DIR and the card hold the
# same files, upper case, sorted, with the same bytes, BOOTDIR.LST aside.
# FIRMWARE=0, so no firmware file is in DIR either.
same() {
    what=$1 img="$2@@1M"
    shift 2
    (cd "$dir" && find . -type f | sed 's|^\./||' | grep -v -x 'BOOTDIR.LST' | tr '[:lower:]' '[:upper:]' | sort) >"$tmp/dir.list"
    mdir -/ -b -i "$img" :: | grep -v '/$' | sed 's|^::/||' |
        tr '[:lower:]' '[:upper:]' | sort >"$tmp/card.list"
    cmp -s "$tmp/dir.list" "$tmp/card.list" ||
        fail "$what: files differ from the card: $(diff "$tmp/card.list" "$tmp/dir.list" | tr '\n' ' ')"
    for f in "$@"; do
        grep -q -x -F "$f" "$tmp/card.list" || fail "$what: the card has no $f"
    done
    (cd "$dir" && find . -type f | sed 's|^\./||' | grep -v -x 'BOOTDIR.LST') | while IFS= read -r f; do
        mcopy -n -i "$img" "::/$f" "$tmp/got"
        cmp -s "$tmp/got" "$dir/$f" || fail "$what: $f differs from the one on the card"
    done
}

cd "$root"
make -s conformance-bootdir BUILD="$b" DIR="$dir" FIRMWARE=0 >/dev/null
make -s conformance-card BUILD="$b" >/dev/null
same conformance "$b/conformance.img" PIPBOY/ROM1.ROM PIPBOY/HOLO/CONFTAPE.HTP PIPBOY/HOLO/CONFDATA.HTP \
    PIPBOY/PIPOS.CFG PIPOS.IMG
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
# make bootdir against make sdimage, each case into an empty DIR.
printf 'custom rom\n' >"$tmp/OTHER.ROM"
printf 'tape one\n' >"$tmp/ONE.HTP"
mkdir -p "$tmp/t2" "$tmp/content/DATA" "$tmp/content/PIPBOY/HOLO"
printf 'tape two\n' >"$tmp/t2/two.htp"
printf 'readme\n' >"$tmp/content/DATA/README.TXT"
printf 'tape three\n' >"$tmp/content/PIPBOY/HOLO/THREE.HTP"
printf 'not the manifest\n' >"$tmp/content/DATA/BOOTDIR.LST"

# pair <what> <expected files> <make args...>
pair() {
    what=$1 files=$2
    shift 2
    rm -rf "$dir"
    make -s bootdir BUILD="$b" DIR="$dir" FIRMWARE=0 "$@" >/dev/null
    make -s sdimage BUILD="$b" "$@" >/dev/null
    # shellcheck disable=SC2086
    same "$what" "$b/sd.img" $files
}
pair defaults "PIPOS.IMG CONFIG.TXT PIPBOY/PIPOS.CFG PIPBOY/ROM1.ROM PIPBOY/HOLO/BEACON.HTP"
pair "ROM=none TAPE=none" "PIPOS.IMG CONFIG.TXT PIPBOY/PIPOS.CFG" ROM=none TAPE=none
grep -q . "$tmp/card.list" && ! grep -q 'ROM1\.ROM\|\.HTP$' "$tmp/card.list" ||
    fail "ROM=none TAPE=none: the card still holds a ROM or a tape"
pair "custom ROM, two tapes, CARD_DIR" \
    "PIPBOY/ROM1.ROM PIPBOY/HOLO/ONE.HTP PIPBOY/HOLO/TWO.HTP PIPBOY/HOLO/THREE.HTP DATA/README.TXT DATA/BOOTDIR.LST" \
    ROM="$tmp/OTHER.ROM" TAPE="$tmp/ONE.HTP $tmp/t2/two.htp" CARD_DIR="$tmp/content"
cmp -s "$dir/PIPBOY/ROM1.ROM" "$tmp/OTHER.ROM" || fail "custom ROM: PIPBOY/ROM1.ROM is not the ROM given"
cmp -s "$dir/DATA/BOOTDIR.LST" "$tmp/content/DATA/BOOTDIR.LST" || fail "CARD_DIR: DATA/BOOTDIR.LST was replaced"

echo "conformance_bootdir_test: passed"
