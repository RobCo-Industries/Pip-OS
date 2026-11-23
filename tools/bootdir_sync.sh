#!/bin/sh
# Copies a staged card into the folder of make bootdir:
#
#   tools/bootdir_sync.sh <stage> <dir> [clean]
#
# The folder belongs to the person, so nothing in it is deleted unless
# they ask. <dir>/BOOTDIR.LST, the manifest, lists the files the last run
# wrote, one path per line.
#
# The script works in three steps: plan, check, apply.
#
# 1. Plan: every folder to create, every file to write and, with clean,
#    every stale file to remove.
# 2. Check the whole plan against <dir>. FAT ignores case, so names
#    compare case-folded. Any of these stops the script, and it lists
#    every problem it found:
#    - a path in <dir> spelled with other case than the path this run
#      writes, even with clean. Nothing is renamed.
#    - a file in <dir> where the plan needs a folder, or a folder where it
#      writes a file, at any depth, even with clean (a stale file that
#      clean removes is not in the way).
#    - a file in <dir> that this run does not write and that the manifest
#      names: stale, from an earlier run. Stops unless the third argument
#      is "clean", which removes it.
#    - a file in <dir> that this run does not write, that the manifest does
#      not name, at a path bootdir writes (the firmware, PIPOS.IMG,
#      config.txt, PIPBOY/ROM1.ROM, PIPBOY/HOLO/*.HTP): bootdir cannot tell
#      whether it is stale, so it stops, even with clean.
#    - a file the plan overwrites or removes that is not writable, or a
#      folder it writes into that is not writable.
#    Any other file is left alone and mentioned once.
# 3. Apply: only when the check found nothing.
#
# "Nothing was changed" on a stop is a promise: the check reads <dir> and
# writes nothing. Once the apply step starts, the check cannot foresee
# everything: a full disk, a file changed by another program meanwhile,
# or a permission the check cannot see (an ACL, a read-only mount) can
# make a copy fail. The script then says it failed after the checks and
# that <dir> is partly updated; running it again once the cause is fixed
# finishes the job.
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

# ---- 1. Plan -------------------------------------------------------------

files() {
    (cd "$1" && find . -type f | sed 's|^\./||') | grep -v -x -i -F -e "$MANIFEST" -e "$CFG" | sort || true
}
paths() {
    (cd "$1" && find . ! -name . | sed 's|^\./||')
}

# Folders to create, parents first; files to write.
(cd "$stage" && find . -mindepth 1 -type d | sed 's|^\./||') | sort >"$tmp/newdirs"
files "$stage" >"$tmp/new"
# PIPBOY/PIPOS.CFG goes in only when <dir> has none.
cfg=0
if [ -f "$stage/$CFG" ] && [ ! -e "$dir/$CFG" ]; then
    cfg=1
fi

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
# Stale files that this run removes.
: >"$tmp/remove"
[ "$clean" != clean ] || cp "$tmp/stale" "$tmp/remove"

# ---- 2. Check ------------------------------------------------------------

stop=0
if [ -d "$dir" ]; then
    # Case: only the outermost differing component is listed.
    { paths "$stage"; echo "$MANIFEST"; } >"$tmp/newpaths"
    paths "$dir" | sort | awk '
        FILENAME == ARGV[1] { want[toupper($0)] = $0; next }
        {
            for (p in bad) if (index($0, p "/") == 1) next
            k = toupper($0)
            if ((k in want) && want[k] != $0) { bad[$0] = 1; print "  " $0 " (this run writes " want[k] ")" }
        }' "$tmp/newpaths" - >"$tmp/case"
    if [ -s "$tmp/case" ]; then
        echo "bootdir: $dir holds paths that differ only in case from paths bootdir writes. FAT ignores case, so they would clash on the card:" >&2
        cat "$tmp/case" >&2
        echo "bootdir: rename or remove them yourself, or use an empty folder." >&2
        stop=1
    fi

    # Type: a file where the plan needs a folder, a folder where it writes
    # a file. Below a reported folder nothing more is listed.
    : >"$tmp/type"
    while IFS= read -r d; do
        if [ -e "$dir/$d" ] || [ -L "$dir/$d" ]; then
            [ ! -d "$dir/$d" ] || continue
            grep -q -x -F -e "$d" "$tmp/remove" && continue
            if ! awk -v p="$d" 'index(p, $0 "/") == 1 { f = 1 } END { exit !f }' "$tmp/type.paths" 2>/dev/null; then
                echo "  $d (a file: this run needs a folder there)" >>"$tmp/type"
                echo "$d" >>"$tmp/type.paths"
            fi
        fi
    done <"$tmp/newdirs"
    { cat "$tmp/new"; echo "$MANIFEST"; echo "$CFG"; } | while IFS= read -r f; do
        if [ -d "$dir/$f" ]; then
            echo "  $f (a folder: this run writes a file there)" >>"$tmp/type"
        elif [ -e "$dir/$f" ] && [ ! -f "$dir/$f" ]; then
            echo "  $f (not a regular file: this run writes a file there)" >>"$tmp/type"
        fi
    done
    if [ -s "$tmp/type" ]; then
        echo "bootdir: $dir holds a file where this run needs a folder, or a folder where it writes a file:" >&2
        cat "$tmp/type" >&2
        echo "bootdir: move them yourself, or use an empty folder." >&2
        stop=1
    fi

    if [ -s "$tmp/unknown" ]; then
        echo "bootdir: $dir holds files at paths bootdir writes, but this run would not write them and no $MANIFEST names them, so bootdir cannot tell whether they are stale:" >&2
        sed 's/^/  /' "$tmp/unknown" >&2
        echo "bootdir: delete or move them yourself, or use an empty folder." >&2
        stop=1
    fi
    if [ -s "$tmp/stale" ] && [ "$clean" != clean ]; then
        echo "bootdir: $dir holds files from an earlier bootdir run that this run would not write:" >&2
        sed 's/^/  /' "$tmp/stale" >&2
        echo "bootdir: use an empty folder, or run again with CLEAN=1 to remove them." >&2
        stop=1
    fi

    # Permissions: files to overwrite or remove, folders to write into.
    : >"$tmp/perm"
    [ -w "$dir" ] || echo "  . (folder)" >>"$tmp/perm"
    { cat "$tmp/new" "$tmp/remove"; echo "$MANIFEST"; } | while IFS= read -r f; do
        if [ -f "$dir/$f" ] && [ ! -w "$dir/$f" ]; then
            echo "  $f" >>"$tmp/perm"
        fi
    done
    { cat "$tmp/newdirs"; sed 's|/[^/]*$||;t;d' "$tmp/remove"; } | sort -u | while IFS= read -r d; do
        if [ -d "$dir/$d" ] && [ ! -w "$dir/$d" ]; then
            echo "  $d (folder)" >>"$tmp/perm"
        fi
    done
    if [ -s "$tmp/perm" ]; then
        echo "bootdir: this run must write these paths in $dir, but they are not writable:" >&2
        cat "$tmp/perm" >&2
        stop=1
    fi
fi
if [ "$stop" -ne 0 ]; then
    echo "bootdir: Nothing was changed." >&2
    exit 1
fi

if [ -s "$tmp/other" ]; then
    echo "bootdir: leaving these files in $dir alone, bootdir did not write them:" >&2
    sed 's/^/  /' "$tmp/other" >&2
fi

# ---- 3. Apply ------------------------------------------------------------

oops() {
    echo "bootdir: unexpected error after the checks passed: $*." >&2
    echo "bootdir: $dir is partly updated. Fix the cause (a full disk?) and run again." >&2
    exit 1
}

mkdir -p "$dir" || oops "could not create $dir"
while IFS= read -r f; do
    rm -f "$dir/$f" || oops "could not remove $f"
    echo "bootdir: removed $f"
done <"$tmp/remove"
while IFS= read -r d; do
    mkdir -p "$dir/$d" || oops "could not create the folder $d"
done <"$tmp/newdirs"
while IFS= read -r f; do
    cp "$stage/$f" "$dir/$f" || oops "could not copy $f"
done <"$tmp/new"
if [ "$cfg" -eq 1 ]; then
    cp "$stage/$CFG" "$dir/$CFG" || oops "could not copy $CFG"
fi
cp "$tmp/new" "$dir/$MANIFEST" || oops "could not write $MANIFEST"
