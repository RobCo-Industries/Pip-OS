#!/bin/sh
# Copies a project's folder tree onto a staged card, after the standard
# files:
#
#   tools/card_dir.sh <stage> <card-dir>
#
# The standard files win: when a path of <card-dir> names a file that is
# already in <stage>, or names a folder where <stage> has a file, or the
# other way round, nothing is copied and the script exits 1. FAT ignores
# case, so PIPBOY/ROM1.ROM and pipboy/rom1.rom are the same path, and a
# folder that differs from a staged one only in case is refused too.
# Folders with the same name merge.
set -eu

die() {
    echo "card_dir: $*" >&2
    exit 1
}

[ $# -eq 2 ] || die "usage: card_dir.sh <stage> <card-dir>"
stage=$1
src=$2
[ -d "$stage" ] || die "$stage is not a folder"
[ -d "$src" ] || die "CARD_DIR=$src is not a folder"

# One line per path: d or f, a space, the path from the folder root.
list() {
    (cd "$1" && find . -mindepth 1 \( -type d -exec printf 'd %s\n' {} + \) -o \
        \( -exec printf 'f %s\n' {} + \)) | sed 's|^\(.\) \./|\1 |'
}

clashes=$( { list "$stage" | sed 's/^/S /'; list "$src" | sed 's/^/C /'; } | awk '
    $1 == "S" { p = substr($0, 5); k = tolower(p); st[k] = $2; sp[k] = p; next }
    {
        p = substr($0, 5); k = tolower(p)
        if (!(k in st)) next
        if (st[k] == "f" || $2 == "f") print p " clashes with the standard " sp[k]
        else if (sp[k] != p) print p " differs only in case from the standard " sp[k]
    }')
if [ -n "$clashes" ]; then
    printf 'card_dir: %s\n' "$clashes" >&2
    die "CARD_DIR=$src would replace a standard card file: nothing copied"
fi
cp -R "$src/." "$stage/"
