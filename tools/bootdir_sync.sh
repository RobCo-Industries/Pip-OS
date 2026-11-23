#!/bin/sh
# Copies a staged card into the folder of make bootdir:
#
#   tools/bootdir_sync.sh <stage> <dir> [clean]
#
# The folder belongs to the person, so nothing in it is deleted unless
# they ask. <dir>/BOOTDIR.LST, the manifest, lists the files the last run
# wrote, one path per line. Before anything changes, each file in <dir>
# that this run does not write is sorted out (FAT ignores case, so names
# compare case-folded):
#
# - named in the manifest: a stale file from an earlier run. The script
#   stops and lists them, unless the third argument is "clean", which
#   removes them.
# - not named, but at a path bootdir writes (the firmware, PIPOS.IMG,
#   config.txt, PIPBOY/ROM1.ROM, PIPBOY/HOLO/*.HTP): the script cannot
#   tell whether it is stale, so it stops, even with clean.
# - anything else: left alone and mentioned once.
#
# A path in <dir> spelled with other case than the path this run writes
# also stops the script, even with clean. Nothing is renamed.
#
# PIPBOY/PIPOS.CFG is never in the manifest and never removed. One already
# in <dir> is kept; the stage's copy only goes into an empty slot.
set -eu

MANIFEST=BOOTDIR.LST
CFG=PIPBOY/PIPOS.CFG

die() {
    echo "bootdir: $*" >&2
    exit 1
}

[ $# -ge 2 ] && [ $# -le 3 ] || die "usage: bootdir_sync.sh <stage> <dir> [clean]"
stage=$1
dir=$2
clean=${3:-}
[ -d "$stage" ] || die "$stage is not a folder"
if [ -e "$dir" ] && [ ! -d "$dir" ]; then
    die "DIR=$dir exists and is not a folder"
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

files() {
    (cd "$1" && find . -type f | sed 's|^\./||') | grep -v -x -i -F -e "$MANIFEST" -e "$CFG" | sort || true
}

# FAT ignores case, but the host file system may not. A path in <dir> that
# matches a path this run writes only when case is ignored would end up
# twice on a case-sensitive system, or keep its old spelling on a
# case-insensitive one. Either way the script stops, even with clean, and
# lists each pair. Only the outermost differing component is listed.
paths() {
    (cd "$1" && find . ! -name . | sed 's|^\./||')
}
if [ -d "$dir" ]; then
    { paths "$stage"; echo "$MANIFEST"; } >"$tmp/newpaths"
    paths "$dir" | awk '
        FILENAME == ARGV[1] { want[toupper($0)] = $0; next }
        {
            for (p in bad) if (index($0, p "/") == 1) next
            k = toupper($0)
            if ((k in want) && want[k] != $0) { bad[$0] = 1; print "  " $0 " (this run writes " want[k] ")" }
        }' "$tmp/newpaths" - >"$tmp/case"
    if [ -s "$tmp/case" ]; then
        echo "bootdir: $dir holds paths that differ only in case from paths bootdir writes. FAT ignores case, so they would clash on the card:" >&2
        cat "$tmp/case" >&2
        echo "bootdir: rename or remove them yourself, or use an empty folder. Nothing was changed." >&2
        exit 1
    fi
fi

files "$stage" >"$tmp/new"
: >"$tmp/have"
: >"$tmp/old"
if [ -d "$dir" ]; then
    files "$dir" >"$tmp/have"
    [ ! -f "$dir/$MANIFEST" ] || cp "$dir/$MANIFEST" "$tmp/old"
fi

# One line per file of <dir> that this run does not write: S stale,
# U unknown at a managed path, O other.
awk '
    FILENAME == ARGV[1] { new[toupper($0)] = 1; next }
    FILENAME == ARGV[2] { old[toupper($0)] = 1; next }
    {
        k = toupper($0)
        if (k in new) next
        if (k in old) print "S " $0
        else if (k ~ /^(BOOTCODE\.BIN|START\.ELF|FIXUP\.DAT|PIPOS\.IMG|CONFIG\.TXT|PIPBOY\/ROM1\.ROM|PIPBOY\/HOLO\/[^\/]*\.HTP)$/)
            print "U " $0
        else print "O " $0
    }' "$tmp/new" "$tmp/old" "$tmp/have" >"$tmp/sorted"

sed -n 's/^O //p' "$tmp/sorted" >"$tmp/other"
sed -n 's/^S //p' "$tmp/sorted" >"$tmp/stale"
sed -n 's/^U //p' "$tmp/sorted" >"$tmp/unknown"

if [ -s "$tmp/other" ]; then
    echo "bootdir: leaving these files in $dir alone, bootdir did not write them:" >&2
    sed 's/^/  /' "$tmp/other" >&2
fi
stop=0
if [ -s "$tmp/unknown" ]; then
    echo "bootdir: $dir holds files at paths bootdir writes, but this run would not write them and no $MANIFEST names them, so bootdir cannot tell whether they are stale:" >&2
    sed 's/^/  /' "$tmp/unknown" >&2
    echo "bootdir: delete or move them yourself, or use an empty folder. Nothing was changed." >&2
    stop=1
fi
if [ -s "$tmp/stale" ] && [ "$clean" != clean ]; then
    echo "bootdir: $dir holds files from an earlier bootdir run that this run would not write:" >&2
    sed 's/^/  /' "$tmp/stale" >&2
    echo "bootdir: use an empty folder, or run again with CLEAN=1 to remove them. Nothing was changed." >&2
    stop=1
fi
[ "$stop" -eq 0 ] || exit 1

mkdir -p "$dir"
while IFS= read -r f; do
    rm -f "$dir/$f"
    echo "bootdir: removed $f"
done <"$tmp/stale"
if [ -e "$dir/$CFG" ]; then
    rm -f "$stage/$CFG"
fi
cp -R "$stage/." "$dir/"
cp "$tmp/new" "$dir/$MANIFEST"
