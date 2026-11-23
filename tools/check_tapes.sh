#!/bin/sh
# Checks the TAPE files before they go to PIPBOY/HOLO on a FAT card:
#
#   tools/check_tapes.sh <tape>...
#
# Prints one line per error on stdout, for the Makefile's $(error), and
# warnings on stderr. Always exits 0.
#
# - Two files whose names differ only in case are the same FAT file: error.
# - A name that does not end in .HTP is never listed by holo_enumerate,
#   so it could never be launched: error.
# - A NAME.HTP that is not a plain 8.3 name (1 to 8 characters from the
#   ABI file name set) is listed under its FAT short alias, such as
#   LONGNA~1.HTP, which the build cannot predict: warning.
set -u

for t in "$@"; do
    printf '%s\n' "$t"
done | awk '
    function base(p) { sub(/.*\//, "", p); return p }
    function err(m) { printf "%s%s", (nerr++ ? "; " : ""), m }
    {
        n = base($0); k = toupper(n)
        if (k in seen)
            err("TAPE names " seen[k] " and " $0 ", the same file on a FAT card, which ignores case")
        else
            seen[k] = $0
        if (k !~ /\.HTP$/)
            err("TAPE file " $0 " does not end in .HTP, so holo_enumerate would never list it")
        else if (k !~ /^[A-Z0-9!#$%&'"'"'()@^_`{}~-]{1,8}\.HTP$/)
            printf "check_tapes: warning: TAPE file %s is not a plain 8.3 NAME.HTP: ROMs see it under a FAT short name such as XXXXXX~1.HTP\n", $0 > "/dev/stderr"
    }'
