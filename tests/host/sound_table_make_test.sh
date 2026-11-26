#!/bin/sh
# A stale sound table stops the build before the kernel image is written.
# SOUND_PARAMS points the check at a changed copy of the parameters, so
# the committed files stay as they are. Every image, card and package
# goal reaches the check.
set -eu
unset CI MAKEFLAGS MFLAGS MAKELEVEL BUILD ROM BOARD PIPOS_SDK ROBCO_CORE CFLAGS_EXTRA KIND WINDOW TAPE SOUND_PARAMS
root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root"
tmp=$(cd "$(mktemp -d)" && pwd -P)
trap 'rm -rf "$tmp"' EXIT
tree=$(git status --porcelain -- kernel tools)

fail() {
    echo "sound_table_make_test: $*" >&2
    exit 1
}

sum() {
    python3 -c 'import hashlib,sys; print(hashlib.sha256(open(sys.argv[1],"rb").read()).hexdigest())' "$1"
}

# A changed parameter: the committed table no longer matches it.
python3 - kernel/sound_params.json "$tmp/stale.json" <<'EOF'
import json, sys
p = json.load(open(sys.argv[1], encoding="utf-8"))
p["click"]["seed"] += 1
json.dump(p, open(sys.argv[2], "w", encoding="utf-8"))
EOF

# 1. Every goal that makes an image, a card or a package runs the check
# before it compiles the table.
for goal in all kernel sdimage run "bootdir DIR=$tmp/card" test-host test-target; do
    # shellcheck disable=SC2086
    out=$(make -n $goal BUILD="$tmp/dry") || fail "make -n $goal failed"
    printf '%s\n' "$out" | python3 -c '
import sys
lines = sys.stdin.read().splitlines()
chk = [i for i, l in enumerate(lines) if "sound_table.py --check" in l]
cc = [i for i, l in enumerate(lines) if "kernel/sound_table.c" in l and "sound_table.py" not in l]
sys.exit(0 if chk and (not cc or chk[0] < cc[0]) else 1)' || fail "make $goal does not run the check before it compiles the table"
done

# 2. A fresh build with a stale table fails with the message and writes
# no image and no table object.
if make kernel BUILD="$tmp/b1" SOUND_PARAMS="$tmp/stale.json" >"$tmp/out" 2>&1; then
    fail "make kernel passed with a stale table"
fi
grep -q 'sound_table.c is stale: run make sound-table' "$tmp/out" || fail "no stale message: $(cat "$tmp/out")"
for f in pipos.img pipos.elf obj/kernel/sound_table.o; do
    [ ! -e "$tmp/b1/$f" ] || fail "make kernel wrote $f from a stale table"
done

# 3. An up-to-date table builds.
make kernel BUILD="$tmp/b2" >"$tmp/out" 2>&1 || fail "make kernel failed: $(cat "$tmp/out")"
[ -f "$tmp/b2/pipos.img" ] || fail "make kernel wrote no image"
before=$(sum "$tmp/b2/pipos.img")

# 4. Over that build, a stale table fails and leaves the image as it was,
# although the object is up to date.
if make kernel BUILD="$tmp/b2" SOUND_PARAMS="$tmp/stale.json" >"$tmp/out" 2>&1; then
    fail "make kernel passed over a build with a stale table"
fi
[ "$(sum "$tmp/b2/pipos.img")" = "$before" ] || fail "the image changed after a failed check"

[ "$(git status --porcelain -- kernel tools)" = "$tree" ] || fail "the test changed the work tree"
echo "sound_table_make_test: passed"
