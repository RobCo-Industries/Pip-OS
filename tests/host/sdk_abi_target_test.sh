#!/bin/sh
# The ABI variable of pipos-app.mk and PIPOS_ABI_TARGET in the headers:
# the tape's abi_version, calls hidden above the target, the values make
# refuses, and a libpip.a that is the same on every build.
set -eu
unset CI MAKEFLAGS MFLAGS MAKELEVEL BUILD ROM BOARD PIPOS_SDK ROBCO_CORE CFLAGS_EXTRA KIND WINDOW TAPE SUBTYPE ABI
root=$(cd "$(dirname "$0")/../.." && pwd)
rimg="$root/external/robco-core/tools/rimg.py"
tmp=$(cd "$(mktemp -d)" && pwd -P)
trap 'rm -rf "$tmp"' EXIT

fail() {
    echo "sdk_abi_target_test: $*" >&2
    exit 1
}

# abi FILE WANT: the header's abi_version is WANT.
abi() {
    python3 "$rimg" verify "$1" >/dev/null || fail "$1 does not verify"
    python3 "$rimg" info "$1" | grep -qx "abi_version=$2" || fail "$1: abi_version is not $2"
}

sum() {
    python3 -c 'import hashlib,sys; print(hashlib.sha256(open(sys.argv[1],"rb").read()).hexdigest())' "$1"
}

cp -R "$root/sdk/examples/hello" "$tmp/proj"
rm -rf "$tmp/proj/build"
hello() {
    make -s -C "$tmp/proj" PIPOS_SDK="$root/sdk" "$@" >/dev/null
}

# 1. No ABI: the headers' ABI. ABI=1.4 gives the same bytes.
hello BUILD=none
abi "$tmp/proj/none/ROM1.ROM" 0x10004
hello BUILD=same ABI=1.4
[ "$(sum "$tmp/proj/none/ROM1.ROM")" = "$(sum "$tmp/proj/same/ROM1.ROM")" ] ||
    fail "ABI=1.4 and no ABI give different images"

# 2. An older target goes into abi_version, for a ROM and a holotape.
hello BUILD=old ABI=1.3
abi "$tmp/proj/old/ROM1.ROM" 0x10003
hello BUILD=oldtape ABI=1.3 KIND=holotape IMAGE=HELLO.HTP
abi "$tmp/proj/oldtape/HELLO.HTP" 0x10003
hello BUILD=zero ABI=1.0
abi "$tmp/proj/zero/ROM1.ROM" 0x10000

# 3. A change of ABI rebuilds the startup code.
hello BUILD=old ABI=1.2
abi "$tmp/proj/old/ROM1.ROM" 0x10002
hello BUILD=old
abi "$tmp/proj/old/ROM1.ROM" 0x10004

# 4. A call newer than the target does not compile. Its number neither.
#    Constants of the newer ABI stay visible.
mkdir "$tmp/new"
cat > "$tmp/new/Makefile" <<EOF
PIPOS_SDK := $root/sdk
IMAGE := NEW.ROM
NAME := NEW
SRCS := main.c
include \$(PIPOS_SDK)/pipos-app.mk
EOF
use() {
    printf '#include <pipos/pip.h>\nint pip_main(const pipos_startup_t *s) { (void)s; %s; return 0; }\n' "$1" > "$tmp/new/main.c"
}
# newer WANT1 WANT2 ARGS...: make ARGS fails and prints WANT1 and WANT2.
newer() {
    w1=$1
    w2=$2
    shift 2
    rm -rf "$tmp/new/build"
    if out=$(make -s -C "$tmp/new" "$@" 2>&1); then
        fail "$* did not fail"
    fi
    for w in "$w1" "$w2"; do
        printf '%s\n' "$out" | grep -q "$w" || fail "$*: no '$w' in: $out"
    done
}
use 'pip_sample_queue(0, 0)'
newer "pip_sample_queue' is unavailable: sample_queue was added in Pip-OS ABI 1.1" "target ABI 0x00010000" ABI=1.0
make -s -C "$tmp/new" ABI=1.1 BUILD=b11 >/dev/null || fail "sample_queue does not build at ABI 1.1"
use 'pip_syscall(PIP_SYS_SAMPLE_QUEUE, 0, 0, 0, 0)'
newer "sample_queue was added in Pip-OS ABI 1.1" "target ABI 0x00010000" ABI=1.0
use 'pip_log(PIP_HW_TAPE + PIP_EV_TAPE + PIP_HOLO_EV_BAD + PIP_BTN_BACK ? "x" : "y")'
make -s -C "$tmp/new" ABI=1.0 BUILD=consts >/dev/null || fail "1.4 constants are hidden at ABI 1.0"

# 5. A target above the headers' ABI names both versions.
use '(void)0'
newer "target ABI 0x00010005" "ABI of these headers, 0x00010004" ABI=1.5

# 6. Bad values stop make before it compiles anything.
for v in 2.0 0.9 1 1. .3 1.2.3 1.03 01.3 1.65536 x1.3 1.3x "1.3 1"; do
    if out=$(make -s -C "$tmp/new" ABI="$v" BUILD=bad 2>&1); then
        fail "ABI=$v did not fail"
    fi
    printf '%s\n' "$out" | grep -q "ABI=$v is not valid" || fail "ABI=$v: no message in: $out"
    [ ! -e "$tmp/new/bad" ] || fail "ABI=$v built something"
done

# 7. libpip.a is the same on two builds, with other file dates.
hello BUILD=liba
hello BUILD=libb
touch -t 200001020304 "$tmp/proj/libb/libpip/"*.o
rm "$tmp/proj/libb/libpip/libpip.a"
hello BUILD=libb
[ "$(sum "$tmp/proj/liba/libpip/libpip.a")" = "$(sum "$tmp/proj/libb/libpip/libpip.a")" ] ||
    fail "two builds of libpip.a differ"

echo "sdk_abi_target_test: passed"
