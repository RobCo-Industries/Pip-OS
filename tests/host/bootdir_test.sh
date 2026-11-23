#!/bin/sh
# Checks that make bootdir run again into the same DIR never leaves a
# stale ROM or tape and never deletes a file the person owns: it stops
# and lists the files, CLEAN=1 removes only those an earlier run wrote,
# and PIPBOY/PIPOS.CFG is always kept. FIRMWARE=0 keeps it offline.
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
    echo "bootdir_test: $*" >&2
    exit 1
}

# A listing of DIR with a checksum per file, to tell "unchanged".
snap() {
    (cd "$dir" && find . -type d | sort && find . -type f | sort | while IFS= read -r f; do cksum "$f"; done)
}

# bootdir <expect: ok|stop> <make args...>: output in $tmp/out.
bootdir() {
    want=$1
    shift
    if make -s bootdir BUILD="$b" DIR="$dir" FIRMWARE=0 "$@" >"$tmp/out" 2>&1; then
        [ "$want" = ok ] || fail "bootdir $* did not stop: $(cat "$tmp/out")"
    else
        [ "$want" = stop ] || fail "bootdir $* failed: $(cat "$tmp/out")"
    fi
}

cd "$root"
printf 'TAPE ONE\n' >"$tmp/ONE.HTP"
printf 'TAPE TWO\n' >"$tmp/TWO.HTP"

# First run into an empty folder.
bootdir ok TAPE="$tmp/ONE.HTP $tmp/TWO.HTP"
for f in PIPOS.IMG config.txt PIPBOY/PIPOS.CFG PIPBOY/ROM1.ROM PIPBOY/HOLO/ONE.HTP PIPBOY/HOLO/TWO.HTP BOOTDIR.LST; do
    [ -f "$dir/$f" ] || fail "first run did not write $f"
done
grep -qx 'PIPBOY/ROM1.ROM' "$dir/BOOTDIR.LST" || fail "BOOTDIR.LST does not name PIPBOY/ROM1.ROM"
if grep -q 'PIPOS.CFG' "$dir/BOOTDIR.LST"; then
    fail "BOOTDIR.LST names PIPBOY/PIPOS.CFG"
fi

# The person edits the settings and adds a file of their own.
printf 'edited\n' >"$dir/PIPBOY/PIPOS.CFG"
printf 'mine\n' >"$dir/NOTES.TXT"

# The same run again changes nothing and succeeds.
snap >"$tmp/before"
bootdir ok TAPE="$tmp/ONE.HTP $tmp/TWO.HTP"
snap >"$tmp/after"
cmp -s "$tmp/before" "$tmp/after" || fail "an identical run changed DIR"
grep -q 'NOTES.TXT' "$tmp/out" || fail "the stray NOTES.TXT was not mentioned"

# ROM=none stops, lists the ROM and leaves DIR as it was.
bootdir stop ROM=none TAPE="$tmp/ONE.HTP $tmp/TWO.HTP"
grep -q 'PIPBOY/ROM1.ROM' "$tmp/out" || fail "ROM=none stop does not list PIPBOY/ROM1.ROM"
grep -q 'CLEAN=1' "$tmp/out" || fail "ROM=none stop does not mention CLEAN=1"
snap >"$tmp/after"
cmp -s "$tmp/before" "$tmp/after" || fail "ROM=none stop changed DIR"

# CLEAN=1 removes the ROM and nothing else.
bootdir ok ROM=none TAPE="$tmp/ONE.HTP $tmp/TWO.HTP" CLEAN=1
[ ! -e "$dir/PIPBOY/ROM1.ROM" ] || fail "CLEAN=1 kept PIPBOY/ROM1.ROM"
grep -v 'ROM1.ROM' "$tmp/before" | grep -v 'BOOTDIR.LST' >"$tmp/want"
snap | grep -v 'BOOTDIR.LST' >"$tmp/after"
cmp -s "$tmp/want" "$tmp/after" || fail "CLEAN=1 changed more than PIPBOY/ROM1.ROM: $(diff "$tmp/want" "$tmp/after" | tr '\n' ' ')"

# A tape list change: the same rule.
snap >"$tmp/before"
bootdir stop ROM=none TAPE="$tmp/ONE.HTP"
grep -q 'PIPBOY/HOLO/TWO.HTP' "$tmp/out" || fail "tape change stop does not list PIPBOY/HOLO/TWO.HTP"
snap >"$tmp/after"
cmp -s "$tmp/before" "$tmp/after" || fail "tape change stop changed DIR"
bootdir ok ROM=none TAPE="$tmp/ONE.HTP" CLEAN=1
[ ! -e "$dir/PIPBOY/HOLO/TWO.HTP" ] || fail "CLEAN=1 kept PIPBOY/HOLO/TWO.HTP"
grep -v 'TWO.HTP' "$tmp/before" | grep -v 'BOOTDIR.LST' >"$tmp/want"
snap | grep -v 'BOOTDIR.LST' >"$tmp/after"
cmp -s "$tmp/want" "$tmp/after" || fail "CLEAN=1 changed more than PIPBOY/HOLO/TWO.HTP"

[ "$(cat "$dir/PIPBOY/PIPOS.CFG")" = edited ] || fail "PIPBOY/PIPOS.CFG lost its edits"
[ "$(cat "$dir/NOTES.TXT")" = mine ] || fail "NOTES.TXT did not survive"

# A DIR from before BOOTDIR.LST existed, with a ROM in it: bootdir cannot
# know the ROM is stale, so it stops even with CLEAN=1.
rm -rf "$dir"
mkdir -p "$dir/PIPBOY"
printf 'old rom\n' >"$dir/PIPBOY/ROM1.ROM"
printf 'old cfg\n' >"$dir/PIPBOY/PIPOS.CFG"
snap >"$tmp/before"
for c in 0 1; do
    bootdir stop ROM=none CLEAN=$c
    grep -q 'PIPBOY/ROM1.ROM' "$tmp/out" || fail "no-manifest stop does not list PIPBOY/ROM1.ROM"
    grep -q 'no BOOTDIR.LST names them' "$tmp/out" || fail "no-manifest stop does not say why"
    snap >"$tmp/after"
    cmp -s "$tmp/before" "$tmp/after" || fail "no-manifest stop changed DIR"
done

# Paths that differ only in case from a path bootdir writes stop it, even
# with CLEAN=1, and DIR stays byte for byte the same. A case-sensitive
# system would get both spellings; a case-insensitive one would keep the
# old spelling. The rule is the same on both.
rm -rf "$dir"
mkdir -p "$dir"
: >"$dir/a"
if [ -e "$dir/A" ]; then fs=insensitive; else fs=sensitive; fi
rm -f "$dir/a"
echo "bootdir_test: temp folder is case-$fs"

# casestop <what> <existing> <written> <make args...>
casestop() {
    what=$1 have=$2 writes=$3
    shift 3
    snap >"$tmp/before"
    for c in 0 1; do
        bootdir stop CLEAN=$c "$@"
        grep -q -F "$have (this run writes $writes)" "$tmp/out" || fail "$what: stop does not list $have and $writes: $(cat "$tmp/out")"
        grep -q 'differ only in case' "$tmp/out" || fail "$what: stop does not say why"
        snap >"$tmp/after"
        cmp -s "$tmp/before" "$tmp/after" || fail "$what: stop changed DIR ($fs)"
    done
}

# A lower-case kernel image name.
rm -rf "$dir"; mkdir -p "$dir"
printf 'old kernel\n' >"$dir/pipos.img"
casestop "lower-case image" pipos.img PIPOS.IMG

# A lower-case PIPBOY folder holding a ROM.
rm -rf "$dir"; mkdir -p "$dir/pipboy"
printf 'old rom\n' >"$dir/pipboy/ROM1.ROM"
casestop "lower-case folder" pipboy PIPBOY

# A tape the manifest names with another spelling.
rm -rf "$dir"
bootdir ok TAPE="$tmp/ONE.HTP"
mv "$dir/PIPBOY/HOLO/ONE.HTP" "$dir/PIPBOY/HOLO/x.htp"
mv "$dir/PIPBOY/HOLO/x.htp" "$dir/PIPBOY/HOLO/one.htp"
printf 'old tape\n' >"$dir/PIPBOY/HOLO/one.htp"
sed 's|^PIPBOY/HOLO/ONE.HTP$|PIPBOY/HOLO/one.htp|' "$dir/BOOTDIR.LST" >"$tmp/lst"
cp "$tmp/lst" "$dir/BOOTDIR.LST"
grep -qx 'PIPBOY/HOLO/one.htp' "$dir/BOOTDIR.LST" || fail "test setup: manifest not renamed"
casestop "manifest spelling" PIPBOY/HOLO/one.htp PIPBOY/HOLO/ONE.HTP TAPE="$tmp/ONE.HTP"

echo "bootdir_test: passed (case-$fs branch)"
