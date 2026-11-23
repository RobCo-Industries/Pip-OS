#!/bin/sh
# Checks a staged card as a whole, after every file is in place and
# before the card image or the bootdir folder is written:
#
#   tools/check_stage.sh <stage> [<card-dir>]
#
# make sdimage (and so make run) and make bootdir both call it, so the
# rule cannot differ between them. It covers every path that reaches the
# card: the standard files, the ROM, the tapes, the CARD_DIR files and
# every folder.
#
# - FAT ignores case. Two paths that are equal when case is folded are
#   one path on the card, and the card would keep only one of them. A
#   case-sensitive host can stage both, for example NAME.TXT and name.txt,
#   or a file Data next to a folder DATA. Error: the script lists each
#   pair and exits 1. Only the outermost clashing component is listed.
# - Names: the kernel's file calls take an absolute path of 8.3
#   components and upper-case it first (RobCo-Core PIPOS_ABI.md, Files).
#   A short name in any case, such as data/level1.bin, is stored with the
#   8.3 short name DATA/LEVEL1.BIN, so a ROM opens it as /DATA/LEVEL1.BIN.
#   A longer name, such as LongName.txt, is stored under a FAT alias such
#   as LONGNA~1.TXT that the build cannot predict: a ROM can only open it
#   through that alias, found with dir_list. Warning, on stderr, for each
#   such path that comes from <card-dir>. The standard files (firmware,
#   PIPOS.IMG, config.txt, PIPBOY/PIPOS.CFG, PIPBOY/ROM1.ROM) are 8.3, and
#   tools/check_tapes.sh has already checked the tapes.
set -eu

die() {
    echo "check_stage: $*" >&2
    exit 1
}

[ $# -ge 1 ] && [ $# -le 2 ] || die "usage: check_stage.sh <stage> [<card-dir>]"
stage=$1
src=${2:-}
[ -d "$stage" ] || die "$stage is not a folder"
[ -z "$src" ] || [ -d "$src" ] || die "CARD_DIR=$src is not a folder"

# One line per path: d or f, a space, the path from the folder root.
list() {
    (cd "$1" && find . -mindepth 1 \( -type d -exec printf 'd %s\n' {} + \) -o \
        \( -exec printf 'f %s\n' {} + \)) | sed 's|^\(.\) \./|\1 |'
}

# Sorted by path, so a folder comes before what it holds.
clashes=$(list "$stage" | sed 's/^\(.\) \(.*\)$/\2 \1/' | LC_ALL=C sort | awk '
    function kind(t) { return t == "d" ? "folder" : "file" }
    {
        t = substr($0, length($0)); p = substr($0, 1, length($0) - 2)
        for (q in bad) if (index(p, q "/") == 1) next
        k = toupper(p)
        if (k in seen) {
            bad[p] = 1
            printf "  %s %s and %s %s\n", kind(st[k]), seen[k], kind(t), p
        } else {
            seen[k] = p; st[k] = t
        }
    }')
if [ -n "$clashes" ]; then
    echo "check_stage: these paths differ only in case, and FAT ignores case, so the card would keep only one of each pair:" >&2
    printf '%s\n' "$clashes" >&2
    die "rename them in CARD_DIR. Nothing was written."
fi

[ -n "$src" ] || exit 0
list "$src" | awk '
    {
        p = substr($0, 3); n = split(p, c, "/")
        for (i = 1; i <= n; i++)
            if (toupper(c[i]) !~ /^[A-Z0-9!#$%&'"'"'()@^_`{}~-]{1,8}(\.[A-Z0-9!#$%&'"'"'()@^_`{}~-]{1,3})?$/) {
                printf "check_stage: warning: CARD_DIR path %s is not made of 8.3 names: FAT stores it under an alias such as XXXXXX~1, and a ROM can only open it by that alias\n", p > "/dev/stderr"
                break
            }
    }'
