#!/usr/bin/env bash
# Runs the RobCo-Core check_pins.sh with a git that always fails, as in the CI
# container, against the real workflow and against a workflow with a wrong
# commit and a wrong tag.
set -euo pipefail
# Build settings from the caller's environment or an outer make
# (make CI=1 passes CI=1 down in MAKEFLAGS) must not change the builds here.
unset CI MAKEFLAGS MFLAGS MAKELEVEL BUILD ROM BOARD PIPOS_SDK ROBCO_CORE CFLAGS_EXTRA KIND WINDOW

root=$(cd "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

mkdir "$tmp/bin"
printf '#!/bin/sh\necho "fatal: detected dubious ownership" >&2\nexit 128\n' > "$tmp/bin/git"
chmod +x "$tmp/bin/git"
export PATH="$tmp/bin:$PATH"

fail() {
    echo "check_pins_test: $*" >&2
    exit 1
}

check="$root/external/robco-core/tools/check_pins.sh"
cd "$root"
"$check" .github/workflows/ci.yml > "$tmp/ok.out" 2>&1 || fail "real tree failed: $(cat "$tmp/ok.out")"
grep -q '^check-pins: ok ([0-9a-f]\{40\}, ' "$tmp/ok.out" || fail "no ok line: $(cat "$tmp/ok.out")"

sed -e 's/robco-ci\.yml@[0-9a-f]*/robco-ci.yml@0000000000000000000000000000000000000000/' \
    -e 's/image-tag:.*/image-tag: wrongtag/' \
    "$root/.github/workflows/ci.yml" > "$tmp/ci.yml"
if "$check" "$tmp/ci.yml" > "$tmp/bad.out" 2>&1; then
    fail "wrong pins passed"
fi
grep -q 'uses robco-ci.yml@0000000000000000000000000000000000000000, the submodule is at' \
    "$tmp/bad.out" || fail "no commit message: $(cat "$tmp/bad.out")"
grep -q 'uses image-tag wrongtag, ' "$tmp/bad.out" || fail "no tag message: $(cat "$tmp/bad.out")"

echo "check_pins_test: passed"
