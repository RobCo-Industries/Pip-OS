#!/bin/sh
# Checks tools/check_stage.sh and tools/bootdir_sync.sh on their own,
# without a build:
#
# - check_stage stops on two staged paths that are equal when case is
#   folded (NAME.TXT and name.txt, a file Data and a folder DATA). Only a
#   case-sensitive temp folder can hold both; on a case-insensitive one
#   these cases are skipped with a note.
# - bootdir_sync stops, and leaves DIR byte for byte the same, when DIR
#   holds a file where the plan needs a folder or a folder where it writes
#   a file, at the top and two levels down.
# - A sweep of random DIR and stage trees from a fixed seed: either the
#   run stops and DIR is unchanged, or it succeeds and DIR holds exactly
#   the staged files, the files it left alone and BOOTDIR.LST, with no two
#   paths equal when case is folded that it did not hold before.
set -eu
unset CLEAN DIR CARD_DIR
root=$(cd "$(dirname "$0")/../.." && pwd)
sync="$root/tools/bootdir_sync.sh"
check="$root/tools/check_stage.sh"
tmp=$(mktemp -d "${TMPDIR:-/tmp}/bootdir_sync_test.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
stage="$tmp/stage"
dir="$tmp/dir"

fail() {
    echo "bootdir_sync_test: $*" >&2
    exit 1
}

# A listing of a folder with types and a checksum per file.
snap() {
    (cd "$1" && find . -type d | LC_ALL=C sort | sed 's/^/d /' &&
        find . ! -type d | LC_ALL=C sort | while IFS= read -r f; do printf 'f %s\n' "$(cksum <"$f") $f"; done)
}

: >"$tmp/a"
if [ -e "$tmp/A" ]; then fs=insensitive; else fs=sensitive; fi
rm -f "$tmp/a"
echo "bootdir_sync_test: temp folder is case-$fs"

# ---- check_stage: one staged tree, no folded duplicates -------------------

# stagecase <what> <expected line>: the stage in $stage must stop.
stagecase() {
    if "$check" "$stage" >"$tmp/out" 2>&1; then
        fail "$1: check_stage did not stop"
    fi
    grep -q -F -e "$2" "$tmp/out" || fail "$1: check_stage does not list '$2': $(cat "$tmp/out")"
    grep -q 'differ only in case' "$tmp/out" || fail "$1: check_stage does not say why"
}

if [ "$fs" = sensitive ]; then
    # Finding A: CARD_DIR with NAME.TXT and name.txt, copied by card_dir.sh.
    rm -rf "$stage" "$tmp/cd"
    mkdir -p "$stage/PIPBOY" "$tmp/cd/Data" "$tmp/cd/DATA"
    : >"$stage/PIPBOY/ROM1.ROM"
    printf 'one\n' >"$tmp/cd/NAME.TXT"
    printf 'two\n' >"$tmp/cd/name.txt"
    printf 'x\n' >"$tmp/cd/Data/X"
    printf 'y\n' >"$tmp/cd/DATA/X"
    "$root/tools/card_dir.sh" "$stage" "$tmp/cd" || fail "card_dir.sh refused a CARD_DIR without standard paths"
    stagecase "two spellings of a file" "file NAME.TXT and file name.txt"
    stagecase "two spellings of a folder" "folder DATA and folder Data"
    if grep -q 'DATA/X' "$tmp/out"; then
        fail "check_stage lists paths below a folder it already listed"
    fi

    # A file and a folder of the same folded name.
    rm -rf "$stage"
    mkdir -p "$stage/LEVELS"
    printf 'l\n' >"$stage/LEVELS/ONE.LVL"
    printf 'f\n' >"$stage/levels"
    stagecase "a file and a folder" "folder LEVELS and file levels"
    echo "bootdir_sync_test: case-sensitive check_stage cases ran"
else
    echo "bootdir_sync_test: skipping the two-spellings check_stage cases: a case-insensitive folder cannot hold both names"
fi

# A clean stage passes, and a long CARD_DIR name gets a warning.
rm -rf "$stage" "$tmp/cd"
mkdir -p "$stage/PIPBOY" "$tmp/cd/data"
: >"$stage/PIPBOY/ROM1.ROM"
: >"$tmp/cd/data/level1.bin"
: >"$tmp/cd/data/LongLevelName.bin"
cp -R "$tmp/cd/." "$stage/"
"$check" "$stage" "$tmp/cd" >"$tmp/out" 2>&1 || fail "check_stage stopped on a clean stage: $(cat "$tmp/out")"
grep -q 'warning: CARD_DIR path data/LongLevelName.bin is not made of 8.3 names' "$tmp/out" ||
    fail "no warning for a long CARD_DIR name: $(cat "$tmp/out")"
if grep -q 'level1.bin' "$tmp/out"; then
    fail "a lower-case 8.3 name got a warning, but a ROM opens it as /DATA/LEVEL1.BIN"
fi

# ---- bootdir_sync: type conflicts -------------------------------------------

# typestop <what> <listed path>: DIR must stay the same, with and without clean.
typestop() {
    snap "$dir" >"$tmp/before"
    for c in "" clean; do
        if "$sync" "$stage" "$dir" $c >"$tmp/out" 2>&1; then
            fail "$1: bootdir_sync did not stop"
        fi
        grep -q -F -e "  $2 (a " "$tmp/out" || fail "$1: stop does not list $2: $(cat "$tmp/out")"
        grep -q 'holds a file where this run needs a folder, or a folder where it writes a file' "$tmp/out" ||
            fail "$1: stop does not say why"
        grep -q 'Nothing was changed' "$tmp/out" || fail "$1: stop does not say nothing changed"
        snap "$dir" >"$tmp/after"
        cmp -s "$tmp/before" "$tmp/after" || fail "$1: stop changed DIR: $(diff "$tmp/before" "$tmp/after" | tr '\n' ' ')"
    done
}

# stage1: a few files, so a half-done copy would show.
newstage() {
    rm -rf "$stage" "$dir"
    mkdir -p "$stage/PIPBOY/HOLO" "$dir"
    printf 'kernel\n' >"$stage/PIPOS.IMG"
    printf 'rom\n' >"$stage/PIPBOY/ROM1.ROM"
}

newstage
mkdir -p "$stage/DATA"
printf 'r\n' >"$stage/DATA/README.TXT"
printf 'mine\n' >"$dir/DATA"
typestop "file where a folder is needed, top" DATA

newstage
mkdir -p "$stage/A/B/C"
printf 'f\n' >"$stage/A/B/C/F.TXT"
mkdir -p "$dir/A/B"
printf 'mine\n' >"$dir/A/B/C"
typestop "file where a folder is needed, two down" A/B/C

newstage
printf 'r\n' >"$stage/README.TXT"
mkdir -p "$dir/README.TXT"
printf 'mine\n' >"$dir/README.TXT/X"
typestop "folder where a file is written, top" README.TXT

newstage
mkdir -p "$stage/A/B"
printf 'f\n' >"$stage/A/B/F.TXT"
mkdir -p "$dir/A/B/F.TXT"
typestop "folder where a file is written, two down" A/B/F.TXT

# A stale file at a path the plan needs as a folder: CLEAN removes it first.
newstage
mkdir -p "$stage/DATA"
printf 'r\n' >"$stage/DATA/README.TXT"
printf 'old\n' >"$dir/DATA"
printf 'DATA\n' >"$dir/BOOTDIR.LST"
"$sync" "$stage" "$dir" clean >"$tmp/out" 2>&1 || fail "clean did not replace a stale file with a folder: $(cat "$tmp/out")"
[ -f "$dir/DATA/README.TXT" ] || fail "clean did not write DATA/README.TXT"

# ---- the sweep ----------------------------------------------------------------

# gen <seed> <count> <pool>: random file paths, one per line, 1 to 3
# components from <pool>.
gen() {
    awk -v seed="$1" -v n="$2" -v pool="$3" 'BEGIN {
        srand(seed); m = split(pool, c, " ")
        for (i = 0; i < n; i++) {
            d = 1 + int(rand() * 3); p = ""
            for (j = 0; j < d; j++) p = p (j ? "/" : "") c[1 + int(rand() * m)]
            print p
        }
    }'
}

# fill <folder> <content prefix>: makes the paths on stdin; one that clashes
# with what is there already is skipped.
fill() {
    while IFS= read -r p; do
        { mkdir -p "$1/$(dirname "$p")" && printf '%s %s\n' "$2" "$p" >"$1/$p"; } 2>/dev/null || true
    done
}

cases=40
ok=0
stopped=0
i=1
while [ "$i" -le "$cases" ]; do
    rm -rf "$stage" "$dir"
    mkdir -p "$stage" "$dir"
    gen "$i" 6 "A B C.TXT D" | fill "$stage" stage
    gen "$((i + 1000))" 3 "A a b C.TXT c.txt E F G.TXT" | fill "$dir" dir
    # Sometimes a manifest that names some of DIR's files.
    if [ $((i % 3)) -ne 0 ]; then
        (cd "$dir" && find . -type f | sed 's|^\./||' | awk -v s="$i" 'BEGIN { srand(s) } rand() < 0.8') >"$tmp/lst"
        cp "$tmp/lst" "$dir/BOOTDIR.LST"
    fi
    c=
    [ $((i % 3)) -eq 0 ] || c=clean
    snap "$dir" >"$tmp/before"
    # A case-sensitive DIR can hold folded duplicates of its own; the run
    # must not add any.
    dupsbefore=$(cd "$dir" && find . | tr '[:lower:]' '[:upper:]' | sort | uniq -d)
    (cd "$dir" && find . -type f ! -name BOOTDIR.LST | sed 's|^\./||' | LC_ALL=C sort) >"$tmp/dirfiles"
    [ -f "$dir/BOOTDIR.LST" ] && cp "$dir/BOOTDIR.LST" "$tmp/lst" || : >"$tmp/lst"
    if "$sync" "$stage" "$dir" $c >"$tmp/out" 2>&1; then
        ok=$((ok + 1))
        # Expected: the staged files with their content, the files of DIR
        # that this run neither writes nor (with clean) removes, and the
        # manifest naming exactly the staged files.
        (cd "$stage" && find . -type f | sed 's|^\./||' | LC_ALL=C sort) >"$tmp/stagefiles"
        awk -v clean="$c" '
            FILENAME == ARGV[1] { st[toupper($0)] = 1; next }
            FILENAME == ARGV[2] { man[toupper($0)] = 1; next }
            { k = toupper($0); if (k in st) next; if (clean == "clean" && (k in man)) next; print }
        ' "$tmp/stagefiles" "$tmp/lst" "$tmp/dirfiles" >"$tmp/kept"
        { while IFS= read -r f; do printf '%s ./%s\n' "$(cksum <"$stage/$f")" "$f"; done <"$tmp/stagefiles"
          while IFS= read -r f; do awk -v p="./$f" '$1 == "f" && $4 == p { print $2, $3, $4 }' "$tmp/before"; done <"$tmp/kept"
          printf '%s ./BOOTDIR.LST\n' "$(cksum <"$tmp/stagefiles")"
        } | LC_ALL=C sort -k 3 >"$tmp/want"
        (cd "$dir" && find . -type f | while IFS= read -r f; do printf '%s %s\n' "$(cksum <"$f")" "$f"; done) | LC_ALL=C sort -k 3 >"$tmp/got"
        cmp -s "$tmp/want" "$tmp/got" || fail "sweep case $i ($fs, ${c:-no clean}): DIR is not the staged files plus the kept ones: $(diff "$tmp/want" "$tmp/got" | tr '\n' ' ')"
        dups=$(cd "$dir" && find . | tr '[:lower:]' '[:upper:]' | sort | uniq -d)
        added=$(printf '%s\n' "$dups" | grep -v -x -F -e "$dupsbefore" || true)
        [ -z "$dupsbefore" ] && added=$dups
        [ -z "$added" ] || fail "sweep case $i: the run added paths equal when case is folded: $added"
    else
        stopped=$((stopped + 1))
        grep -q 'Nothing was changed' "$tmp/out" || fail "sweep case $i: failed without a stop: $(cat "$tmp/out")"
        snap "$dir" >"$tmp/after"
        cmp -s "$tmp/before" "$tmp/after" || fail "sweep case $i ($fs): stop changed DIR: $(diff "$tmp/before" "$tmp/after" | tr '\n' ' ')"
    fi
    i=$((i + 1))
done
[ "$ok" -gt 0 ] && [ "$stopped" -gt 0 ] || fail "sweep did not reach both outcomes: $ok passed, $stopped stopped"

echo "bootdir_sync_test: passed (case-$fs branch; sweep: $cases cases, $ok written, $stopped stopped)"
