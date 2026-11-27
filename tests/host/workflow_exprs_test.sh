#!/usr/bin/env bash
# Checks that RobCo-Core's check-workflows passes without a make override
# warning and that the goal CI runs names the guard.
set -euo pipefail
unset CI MAKEFLAGS MFLAGS MAKELEVEL BUILD ROM BOARD PIPOS_SDK ROBCO_CORE CFLAGS_EXTRA KIND WINDOW

root=$(cd "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

fail() {
    echo "workflow_exprs_test: $*" >&2
    exit 1
}

cd "$root"
make -s check-workflows > "$tmp/run.out" 2> "$tmp/run.err" || fail "check-workflows failed: $(cat "$tmp/run.out" "$tmp/run.err")"
if grep -q overriding "$tmp/run.err"; then
    fail "make printed an override warning: $(cat "$tmp/run.err")"
fi
make -n CI=1 all test-host > "$tmp/dry.out" 2> "$tmp/dry.err" || fail "dry run failed: $(cat "$tmp/dry.err")"
grep -q 'external/robco-core/tools/check_workflow_exprs\.py' "$tmp/dry.out" || fail "the CI goal does not run the guard"
if grep -q overriding "$tmp/dry.err"; then
    fail "make printed an override warning: $(cat "$tmp/dry.err")"
fi
echo "workflow_exprs_test: ok"
