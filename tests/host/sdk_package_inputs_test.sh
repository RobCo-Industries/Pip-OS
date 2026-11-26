#!/bin/sh
# make sdk-package takes only files git tracks. An untracked header in
# sdk/include/pipos/ is not in the package's file list, and an untracked
# file in an input folder of Pip-OS or of RobCo-Core stops the build with
# a message that names it, before anything is built.
set -eu
unset CI MAKEFLAGS MFLAGS MAKELEVEL BUILD
root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root"
tmp=$(mktemp -d)
# The process id keeps the names off anyone's work. Each name still ends
# in .h or .c inside an input folder, so the rules under test apply.
hdr=sdk/include/pipos/zz_untracked_test_$$.h
src=external/robco-core/src/lib/zz_untracked_test_$$.c
# The trap removes only the files this run made, never one it found.
made=
trap 'for m in $made; do rm -f "$m"; done; rm -rf "$tmp"' EXIT

fail() {
    echo "sdk_package_inputs_test: $*" >&2
    exit 1
}

# Makes a file that did not exist and records it. noclobber stops the
# write when the file exists already, so nothing is overwritten.
plant() {
    if [ -e "$1" ] || ! (set -C; printf '/* untracked */\n' >"$1") 2>/dev/null; then
        fail "$1 exists already"
    fi
    made="$made $1"
}

for f in "$hdr" "$src"; do
    if [ -e "$f" ]; then fail "$f exists already"; fi
done

# The file list comes from git: a dry run lists the tracked headers only.
plant "$hdr"
make -n sdk-package BUILD="$tmp/dry" >"$tmp/dry.txt" 2>&1 || fail "make -n sdk-package failed: $(cat "$tmp/dry.txt")"
grep -q 'include/pipos/abi.h=sdk/include/pipos/abi.h' "$tmp/dry.txt" || fail "the dry run lists no tracked header"
if grep -qF "$hdr" "$tmp/dry.txt"; then fail "the untracked header is in the file list"; fi

# The build stops on it and names it.
for f in "$hdr" "$src"; do
    [ -e "$f" ] || plant "$f"
    if make sdk-package BUILD="$tmp/build" >"$tmp/out.txt" 2>&1; then
        fail "make sdk-package passed with the untracked $f"
    fi
    grep -q "untracked files in the inputs" "$tmp/out.txt" || fail "no message for $f: $(cat "$tmp/out.txt")"
    grep -qF "$f" "$tmp/out.txt" || fail "the message does not name $f: $(cat "$tmp/out.txt")"
    if [ -e "$tmp/build/sdk-package" ]; then fail "make sdk-package built something with $f"; fi
    rm -f "$f"
done
echo "sdk_package_inputs_test: ok"
