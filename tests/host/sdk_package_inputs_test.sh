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
hdr=sdk/include/pipos/zz_untracked_test.h
src=external/robco-core/src/lib/zz_untracked_test.c
trap 'rm -f "$hdr" "$src"; rm -rf "$tmp"' EXIT

fail() {
    echo "sdk_package_inputs_test: $*" >&2
    exit 1
}

for f in "$hdr" "$src"; do
    if [ -e "$f" ]; then fail "$f exists already"; fi
done

# The file list comes from git: a dry run lists the tracked headers only.
printf '/* untracked */\n' >"$hdr"
make -n sdk-package BUILD="$tmp/dry" >"$tmp/dry.txt" 2>&1 || fail "make -n sdk-package failed: $(cat "$tmp/dry.txt")"
grep -q 'include/pipos/abi.h=sdk/include/pipos/abi.h' "$tmp/dry.txt" || fail "the dry run lists no tracked header"
if grep -q zz_untracked_test "$tmp/dry.txt"; then fail "the untracked header is in the file list"; fi

# The build stops on it and names it.
for f in "$hdr" "$src"; do
    printf '/* untracked */\n' >"$f"
    if make sdk-package BUILD="$tmp/build" >"$tmp/out.txt" 2>&1; then
        fail "make sdk-package passed with the untracked $f"
    fi
    grep -q "untracked files in the inputs" "$tmp/out.txt" || fail "no message for $f: $(cat "$tmp/out.txt")"
    grep -qF "$f" "$tmp/out.txt" || fail "the message does not name $f: $(cat "$tmp/out.txt")"
    if [ -e "$tmp/build/sdk-package" ]; then fail "make sdk-package built something with $f"; fi
    rm -f "$f"
done
echo "sdk_package_inputs_test: ok"
