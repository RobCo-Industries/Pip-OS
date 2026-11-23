#!/bin/sh
# Plays a ROM project that has Pip-OS in another folder, as in
# docs/ROM_DEVELOPMENT.md "Using Pip-OS from another repository": its own
# ROM built with pipos-app.mk, then make -C <pip-os> kernel, sdimage,
# bootdir and run with an absolute BUILD outside the Pip-OS tree.
# Checks that ROM=<file> builds none of the bundled examples, that
# CARD_DIR lands on the card, that a CARD_DIR file clashing with a
# standard card file stops the build, and that TAPE takes a list.
# Reads the card with mtools.
set -eu
# Build settings from the caller's environment or an outer make
# (make CI=1 passes CI=1 down in MAKEFLAGS) must not change the builds here.
unset CI MAKEFLAGS MFLAGS MAKELEVEL BUILD ROM BOARD PIPOS_SDK ROBCO_CORE CFLAGS_EXTRA KIND WINDOW TAPE \
    DIR FIRMWARE CARD_DIR SDIMAGE_DIR CLEAN
pipos=$(cd "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

fail() {
    echo "outside_project_test: $*" >&2
    exit 1
}

# The outside project: one ROM source, a Makefile and a content tree.
proj="$tmp/proj"
mkdir -p "$proj/src" "$proj/content/PIPBOY/DATA"
cat >"$proj/src/main.c" <<'EOF'
#include <pipos/pip.h>

int pip_main(const pipos_startup_t *startup)
{
    (void)startup;
    pip_log("outside: ready\n");
    return 0;
}
EOF
cat >"$proj/Makefile" <<EOF
PIPOS_SDK := $pipos/sdk
IMAGE     := ROM1.ROM
KIND      := rom
NAME      := OUTSIDE
SRCS      := src/main.c
include \$(PIPOS_SDK)/pipos-app.mk
EOF
printf 'hello from the project\n' >"$proj/content/PIPBOY/DATA/README.TXT"
make -s -C "$proj" >/dev/null
rom="$proj/build/ROM1.ROM"
[ -f "$rom" ] || fail "the project did not build its ROM"

b="$tmp/pipos"
pm() {
    make -s -C "$pipos" BUILD="$b" "$@"
}
no_bundled() {
    for d in examples roms holotapes conformance; do
        [ ! -e "$b/$d" ] || fail "$1 built $b/$d"
    done
}

# make kernel: the image and its ELF, nothing else.
pm kernel >/dev/null
[ -f "$b/pipos.img" ] || fail "make kernel wrote no pipos.img"
[ -f "$b/pipos.elf" ] || fail "make kernel wrote no pipos.elf"
no_bundled "make kernel"

# make sdimage with the project's ROM, no tape and its content tree.
card="$b/sd.img@@1M"
pm sdimage ROM="$rom" TAPE=none CARD_DIR="$proj/content" >/dev/null
no_bundled "make sdimage ROM=<file> TAPE=none"
mcopy -n -i "$card" ::/PIPBOY/ROM1.ROM "$tmp/got.rom"
cmp -s "$tmp/got.rom" "$rom" || fail "PIPBOY/ROM1.ROM is not the project's ROM"
mcopy -n -i "$card" ::/PIPBOY/DATA/README.TXT "$tmp/got.txt" || fail "CARD_DIR file is not on the card"
cmp -s "$tmp/got.txt" "$proj/content/PIPBOY/DATA/README.TXT" || fail "CARD_DIR file differs"
mdir -b -i "$card" ::/ | grep -q 'PIPOS\.IMG$' || fail "card has no PIPOS.IMG"
mdir -b -i "$card" ::/PIPBOY | grep -q 'PIPOS\.CFG$' || fail "card has no PIPBOY/PIPOS.CFG"
if mdir -b -i "$card" ::/PIPBOY/HOLO 2>/dev/null | grep -q 'HTP$'; then
    fail "TAPE=none card has a holotape"
fi

# TAPE takes a list: every file lands in PIPBOY/HOLO under its own name.
printf 'tape one\n' >"$tmp/ONE.HTP"
printf 'tape two\n' >"$tmp/TWO.HTP"
pm sdimage ROM="$rom" TAPE="$tmp/ONE.HTP $tmp/TWO.HTP" >/dev/null
mdir -b -i "$card" ::/PIPBOY/HOLO >"$tmp/holo"
grep -q 'ONE\.HTP$' "$tmp/holo" || fail "TAPE list lost ONE.HTP"
grep -q 'TWO\.HTP$' "$tmp/holo" || fail "TAPE list lost TWO.HTP"
mkdir -p "$tmp/dup"
printf 'same name\n' >"$tmp/dup/ONE.HTP"
if pm sdimage ROM="$rom" TAPE="$tmp/ONE.HTP $tmp/dup/ONE.HTP" >/dev/null 2>"$tmp/err"; then
    fail "two tapes with one name did not stop the build"
fi

# A CARD_DIR file at a standard path stops the build, whatever its case.
for clash in PIPBOY/PIPOS.CFG pipboy/rom1.rom PIPOS.IMG config.txt; do
    rm -rf "$tmp/bad"
    mkdir -p "$tmp/bad/$(dirname "$clash")"
    printf 'mine\n' >"$tmp/bad/$clash"
    if pm sdimage ROM="$rom" TAPE=none CARD_DIR="$tmp/bad" >/dev/null 2>"$tmp/err"; then
        fail "CARD_DIR with $clash did not stop the build"
    fi
    grep -q 'standard' "$tmp/err" || fail "CARD_DIR with $clash: no clash message"
done
if pm sdimage ROM="$rom" TAPE=none CARD_DIR="$tmp/missing" >/dev/null 2>"$tmp/err"; then
    fail "a missing CARD_DIR did not stop the build"
fi
if pm sdimage ROM="$rom" TAPE=none SDIMAGE_DIR="$proj/content" >/dev/null 2>"$tmp/err"; then
    fail "SDIMAGE_DIR did not stop the build"
fi
grep -q 'CARD_DIR' "$tmp/err" || fail "SDIMAGE_DIR: the message does not name CARD_DIR"

# make bootdir: the same files as the card, firmware left out, plus
# BOOTDIR.LST, its list of what it wrote.
dir="$tmp/card"
pm bootdir DIR="$dir" FIRMWARE=0 ROM="$rom" TAPE=none CARD_DIR="$proj/content" >/dev/null
no_bundled "make bootdir ROM=<file> TAPE=none"
pm sdimage ROM="$rom" TAPE=none CARD_DIR="$proj/content" >/dev/null
(cd "$dir" && find . -type f | sed 's|^\./||' | grep -v -x 'BOOTDIR.LST' | tr '[:lower:]' '[:upper:]' | sort) >"$tmp/dir.list"
mdir -/ -b -i "$card" :: | grep -v '/$' | sed 's|^::/||' | tr '[:lower:]' '[:upper:]' | sort >"$tmp/card.list"
cmp -s "$tmp/dir.list" "$tmp/card.list" ||
    fail "bootdir files differ from the card: $(diff "$tmp/card.list" "$tmp/dir.list" | tr '\n' ' ')"
for f in PIPBOY/ROM1.ROM PIPBOY/DATA/README.TXT PIPBOY/PIPOS.CFG PIPOS.IMG config.txt; do
    mcopy -n -i "$card" "::/$f" "$tmp/got"
    cmp -s "$tmp/got" "$dir/$f" || fail "bootdir $f differs from the card's"
done
# A PIPOS.CFG the user changed stays.
printf '[display]\nscheme = amber\n' >"$dir/PIPBOY/PIPOS.CFG"
pm bootdir DIR="$dir" FIRMWARE=0 ROM="$rom" TAPE=none CARD_DIR="$proj/content" >/dev/null
grep -q amber "$dir/PIPBOY/PIPOS.CFG" || fail "bootdir replaced an existing PIPBOY/PIPOS.CFG"
for f in bootcode.bin start.elf fixup.dat; do
    [ ! -e "$dir/$f" ] || fail "FIRMWARE=0 still wrote $f"
done
rm -rf "$tmp/bad"
mkdir -p "$tmp/bad/PIPBOY"
printf 'mine\n' >"$tmp/bad/PIPBOY/ROM1.ROM"
if pm bootdir DIR="$tmp/card2" FIRMWARE=0 ROM="$rom" TAPE=none CARD_DIR="$tmp/bad" >/dev/null 2>"$tmp/err"; then
    fail "bootdir with a clashing CARD_DIR did not stop"
fi
[ ! -e "$tmp/card2/PIPBOY/ROM1.ROM" ] || fail "bootdir wrote files before it stopped on a clash"

# make run builds the same card and boots it: check what it would run.
pm -n run ROM="$rom" TAPE=none CARD_DIR="$proj/content" BOARD=raspi0 >"$tmp/run"
grep -q 'harness.run' "$tmp/run" || fail "make run does not start the runner"
grep -q -- '--board raspi0' "$tmp/run" || fail "make run does not pass BOARD"
grep -q 'card_dir.sh' "$tmp/run" || fail "make run does not copy CARD_DIR"
if grep -q 'examples' "$tmp/run"; then
    fail "make run ROM=<file> TAPE=none would build the examples"
fi
echo "outside_project_test: passed"
