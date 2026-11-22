#!/bin/sh
# Rebuilds a small ROM project after each kind of change and checks that
# sdk/pipos-app.mk notices it: a source removed from SRCS, a header edit,
# a new TITLE, new library flags. Also checks that src/start.c and src/start.S both link, and
# that objects of sources outside the project stay inside BUILD.
set -eu
# Build settings from the caller's environment or an outer make
# (make CI=1 passes CI=1 down in MAKEFLAGS) must not change the builds here.
unset CI MAKEFLAGS MFLAGS MAKELEVEL BUILD ROM BOARD PIPOS_SDK ROBCO_CORE CFLAGS_EXTRA KIND WINDOW
root=$(cd "$(dirname "$0")/../.." && pwd)
rimg="$root/external/robco-core/tools/rimg.py"
nm=${CROSS:-arm-none-eabi-}nm
tmp=$(cd "$(mktemp -d)" && pwd -P)
trap 'rm -rf "$tmp"' EXIT
p="$tmp/proj"
b="$p/build"

fail() {
    echo "sdk_rebuild_test: $*" >&2
    exit 1
}

# has SYMBOL: the ELF defines SYMBOL.
has() {
    "$nm" "$b/APP.elf" | grep -q " $1\$"
}

build() {
    make -s -C "$p" PIPOS_SDK="$root/sdk" "$@" >/dev/null
}

mkdir -p "$p/src" "$tmp/common"
cat >"$p/Makefile" <<'EOF'
IMAGE := APP.ROM
NAME  := APP
TITLE ?= First title
SRCS  ?= src/main.c src/gone.c src/start.c src/start.S ../common/shared.c
include $(PIPOS_SDK)/pipos-app.mk
EOF
cat >"$p/src/app.h" <<'EOF'
#define APP_VALUE 1
EOF
# Weak references keep the other sources in the link without needing them.
cat >"$p/src/main.c" <<'EOF'
#include <pipos/crt0.h>
#include "app.h"
void gone_fn(void) __attribute__((weak));
void start_c_fn(void) __attribute__((weak));
void start_s_fn(void) __attribute__((weak));
void shared_fn(void) __attribute__((weak));
int pip_main(const pipos_startup_t *startup)
{
    (void)startup;
    if (gone_fn) gone_fn();
    if (start_c_fn) start_c_fn();
    if (start_s_fn) start_s_fn();
    if (shared_fn) shared_fn();
    return APP_VALUE;
}
EOF
printf 'void gone_fn(void) {}\n' >"$p/src/gone.c"
printf 'void start_c_fn(void) {}\n' >"$p/src/start.c"
printf '.text\n.global start_s_fn\nstart_s_fn:\n    bx lr\n' >"$p/src/start.S"
printf 'void shared_fn(void) {}\n' >"$tmp/common/shared.c"

# 1. Same base name in .c and .S: both objects link. A source outside the
#    project builds inside BUILD, never beside the source.
build
for s in gone_fn start_c_fn start_s_fn shared_fn; do
    has "$s" || fail "first build: $s missing from the ELF"
done
[ -z "$(find "$tmp/common" -name '*.o')" ] || fail "an object landed beside ../common/shared.c"
[ -z "$(find "$tmp" -name '*.o' ! -path "$b/*")" ] || fail "an object landed outside BUILD"

# 2. A source removed from SRCS leaves the ELF.
build SRCS="src/main.c src/start.c src/start.S ../common/shared.c"
has gone_fn && fail "gone_fn still in the ELF after src/gone.c left SRCS"
has start_c_fn || fail "start_c_fn lost after the SRCS change"

# 3. A header edit rebuilds the object that includes it.
obj=$(find "$b/obj" -name 'main.c.o' -o -name 'main.o' | head -n 1)
[ -n "$obj" ] || fail "no object for src/main.c"
sleep 1
touch "$tmp/mark"
sleep 1
printf '#define APP_VALUE 2\n' >"$p/src/app.h"
build
[ "$obj" -nt "$tmp/mark" ] || fail "editing src/app.h did not rebuild $obj"

# 4. A new TITLE reseals the image.
build TITLE="Second title"
python3 "$rimg" info "$b/APP.ROM" | grep -qx "title=Second title" || fail "TITLE change did not reseal APP.ROM"

# 5. New library flags (CI=1 adds -Werror) rebuild libpip.a. The builds
#    above ran without CI, since the script unsets it.
touch "$tmp/mark2"
sleep 1
build CI=1
[ "$b/libpip/libpip.a" -nt "$tmp/mark2" ] || fail "CI=1 did not rebuild libpip.a"

# 6. A failed seal leaves no image.
if build NAME=NAME_LONGER_THAN_16_BYTES >/dev/null 2>&1; then
    fail "an invalid NAME did not fail"
fi
[ ! -e "$b/APP.ROM" ] || fail "a failed seal left APP.ROM behind"
[ ! -e "$b/APP.ROM.tmp" ] || fail "a failed seal left APP.ROM.tmp behind"
echo "sdk_rebuild_test: passed"
