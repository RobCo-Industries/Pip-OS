#!/usr/bin/env bash
# Boot the kernel in QEMU and check the serial log for the boot banner.
# Usage: tests/qemu_boot_test.sh <2835|2836>
set -euo pipefail

BCM="${1:-}"
case "$BCM" in
    2835) KERNEL=kernel;  MACHINE=raspi0;  MEM=512 ;;
    2836) KERNEL=kernel7; MACHINE=raspi2b; MEM=1024 ;;
    *) echo "usage: $0 <2835|2836>" >&2; exit 2 ;;
esac

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build"
LOG="$BUILD/qemu-bcm$BCM.log"
TIMEOUT=10

make -C "$BUILD" clean
make -C "$BUILD" BCM="$BCM"

rm -f "$LOG"
qemu-system-arm -m "$MEM" -M "$MACHINE" -display none -monitor none \
    -serial "file:$LOG" -kernel "$BUILD/$KERNEL.elf" &
QEMU_PID=$!
trap 'kill "$QEMU_PID" 2>/dev/null || true; wait "$QEMU_PID" 2>/dev/null || true' EXIT

EXPECTED=(
    "PIP-OS(R) V7.1.0.8"
    "PIP-OS KERNEL INITIALIZED"
    "PIP-OS READY"
)

all_found() {
    local s
    for s in "${EXPECTED[@]}"; do
        grep -qF -- "$s" "$LOG" 2>/dev/null || return 1
    done
}

for _ in $(seq 1 $((TIMEOUT * 4))); do
    all_found && break
    kill -0 "$QEMU_PID" 2>/dev/null || break
    sleep 0.25
done

if all_found; then
    echo "PASS: BCM$BCM boot banner found in serial log"
    exit 0
fi

echo "FAIL: BCM$BCM boot banner not found. Missing:"
for s in "${EXPECTED[@]}"; do
    grep -qF -- "$s" "$LOG" 2>/dev/null || echo "  $s"
done
echo "---- serial log ----"
cat "$LOG" 2>/dev/null || echo "(no log)"
exit 1
