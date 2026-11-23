#!/usr/bin/env bash
# Runs `make check-doc-links`, the RobCo-Core doc link check that
# `make check-pins` calls, with a git that always fails, as in the CI
# container: the real tree passes, a stale link in a temporary copy fails,
# and check-pins depends on check-doc-links.
set -euo pipefail
# Build settings from the caller's environment or an outer make must not
# change the run here.
unset CI MAKEFLAGS MFLAGS MAKELEVEL BUILD ROM BOARD PIPOS_SDK ROBCO_CORE CFLAGS_EXTRA KIND WINDOW DOC_ROOT

root=$(cd "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

mkdir "$tmp/bin"
printf '#!/bin/sh\necho "fatal: detected dubious ownership" >&2\nexit 128\n' > "$tmp/bin/git"
chmod +x "$tmp/bin/git"
export PATH="$tmp/bin:$PATH"

fail() {
    echo "check_doc_links_test: $*" >&2
    exit 1
}

cd "$root"
make -s check-doc-links > "$tmp/real.out" 2>&1 || fail "real tree failed: $(cat "$tmp/real.out")"
grep -q '^check-doc-links: ok ([0-9a-f]\{40\})' "$tmp/real.out" || fail "no ok line: $(cat "$tmp/real.out")"
commit=$(sed -n 's/^check-doc-links: ok (\([0-9a-f]*\)).*$/\1/p' "$tmp/real.out")

# check-pins runs the doc link check first.
make -s -n check-pins > "$tmp/plan.out" 2>&1 || fail "make -n check-pins failed: $(cat "$tmp/plan.out")"
grep -q 'tools/check_doc_links.sh \. external/robco-core' "$tmp/plan.out" ||
    fail "check-pins does not run the link check: $(cat "$tmp/plan.out")"

# A copy of the docs with a submodule whose .git is a file, as in a checkout.
proj=$tmp/proj
mkdir -p "$proj/external/robco-core" "$proj/gitdir"
cp -R "$root/docs" "$root/README.md" "$proj/"
echo "gitdir: ../../gitdir" > "$proj/external/robco-core/.git"
echo "$commit" > "$proj/gitdir/HEAD"
make -s check-doc-links DOC_ROOT="$proj" > "$tmp/ok.out" 2>&1 ||
    fail "copy failed: $(cat "$tmp/ok.out")"

stale=0000000000000000000000000000000000000000
echo "[tree](https://github.com/RobCo-Industries/RobCo-Core/tree/$stale/docs)" >> "$proj/docs/API.md"
if make -s check-doc-links DOC_ROOT="$proj" > "$tmp/bad.out" 2>&1; then
    fail "stale link passed"
fi
grep -q "docs/API.md:[0-9]* links RobCo-Core at $stale" "$tmp/bad.out" ||
    fail "no stale message: $(cat "$tmp/bad.out")"

echo "check_doc_links_test: passed"
