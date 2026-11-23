#!/usr/bin/env bash
# Runs tools/check_doc_links.sh with a git that always fails, as in the CI
# container: the real tree passes, a stale link fails, a current link passes
# and CHANGELOG.md is ignored.
set -euo pipefail
unset CI MAKEFLAGS MFLAGS MAKELEVEL BUILD ROM BOARD PIPOS_SDK ROBCO_CORE CFLAGS_EXTRA KIND WINDOW

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

check="$root/tools/check_doc_links.sh"
"$check" > "$tmp/real.out" 2>&1 || fail "real tree failed: $(cat "$tmp/real.out")"
grep -q '^check-doc-links: ok ([0-9a-f]\{40\})' "$tmp/real.out" || fail "no ok line: $(cat "$tmp/real.out")"
commit=$(sed -n 's/^check-doc-links: ok (\([0-9a-f]*\))$/\1/p' "$tmp/real.out")

# A fake project: a submodule whose .git is a file, as in a checkout.
proj=$tmp/proj
mkdir -p "$proj/external/robco-core" "$proj/gitdir" "$proj/docs"
echo "gitdir: ../../gitdir" > "$proj/external/robco-core/.git"
echo "$commit" > "$proj/gitdir/HEAD"
url=https://github.com/RobCo-Industries/RobCo-Core/blob
stale=0000000000000000000000000000000000000000

echo "[spec]($url/$commit/docs/spec/PIPOS_ABI.md)" > "$proj/docs/ok.md"
echo "[old]($url/$stale/docs/x.md)" > "$proj/CHANGELOG.md"
"$check" "$proj" > "$tmp/ok.out" 2>&1 || fail "current link failed: $(cat "$tmp/ok.out")"

echo "[tree](https://github.com/RobCo-Industries/RobCo-Core/tree/$stale/docs)" > "$proj/docs/bad.md"
if "$check" "$proj" > "$tmp/bad.out" 2>&1; then
    fail "stale link passed"
fi
grep -q "docs/bad.md:1 links RobCo-Core at $stale, the submodule is at $commit" "$tmp/bad.out" ||
    fail "no stale message: $(cat "$tmp/bad.out")"
grep -q 'CHANGELOG' "$tmp/bad.out" && fail "CHANGELOG.md was checked: $(cat "$tmp/bad.out")"

echo "check_doc_links_test: passed"
