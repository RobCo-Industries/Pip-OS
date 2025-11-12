#!/bin/bash

# PIP-OS Test Suite Runner
# This script performs build tests and validation checks

set -e  # Exit on error

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Test counters
TESTS_PASSED=0
TESTS_FAILED=0

# Function to print test result
print_result() {
    if [ $1 -eq 0 ]; then
        echo -e "${GREEN}✓${NC} $2"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        echo -e "${RED}✗${NC} $2"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
}

# Function to check if command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

echo "======================================"
echo "PIP-OS Test Suite"
echo "======================================"
echo ""

# Test 1: Check toolchain availability
echo "Testing toolchain availability..."
if command_exists arm-none-eabi-gcc; then
    print_result 0 "ARM toolchain (arm-none-eabi-gcc) found"
    arm-none-eabi-gcc --version | head -1
else
    print_result 1 "ARM toolchain (arm-none-eabi-gcc) not found"
fi
echo ""

# Test 2: Build for BCM2835
echo "Testing build for BCM2835 (Raspberry Pi Zero/1)..."
cd ../build
export BCM=2835
if make clean > /dev/null 2>&1 && make > /dev/null 2>&1; then
    if [ -f kernel.img ]; then
        SIZE=$(stat -c%s kernel.img 2>/dev/null || stat -f%z kernel.img 2>/dev/null)
        print_result 0 "BCM2835 build successful (size: $SIZE bytes)"
    else
        print_result 1 "BCM2835 build failed - kernel.img not found"
    fi
else
    print_result 1 "BCM2835 build failed"
fi
cd ../tests
echo ""

# Test 3: Build for BCM2836
echo "Testing build for BCM2836 (Raspberry Pi 2)..."
cd ../build
export BCM=2836
if make clean > /dev/null 2>&1 && make > /dev/null 2>&1; then
    if [ -f kernel7.img ]; then
        SIZE=$(stat -c%s kernel7.img 2>/dev/null || stat -f%z kernel7.img 2>/dev/null)
        print_result 0 "BCM2836 build successful (size: $SIZE bytes)"
    else
        print_result 1 "BCM2836 build failed - kernel7.img not found"
    fi
else
    print_result 1 "BCM2836 build failed"
fi
cd ../tests
echo ""

# Test 4: Check for required source files
echo "Testing source file presence..."
REQUIRED_FILES=(
    "../src/kernel/main.c"
    "../src/kernel/uart.c"
    "../src/kernel/boot_display.c"
    "../src/kernel/rom_loader.c"
    "../src/kernel/syscall.c"
    "../src/kernel/power.c"
    "../src/kernel/audio.c"
    "../src/kernel/k_libc/k_string.c"
    "../src/aarch32/boot.S"
)

ALL_FILES_PRESENT=true
for file in "${REQUIRED_FILES[@]}"; do
    if [ ! -f "$file" ]; then
        echo -e "${RED}✗${NC} Missing: $file"
        ALL_FILES_PRESENT=false
    fi
done

if [ "$ALL_FILES_PRESENT" = true ]; then
    print_result 0 "All required source files present"
else
    print_result 1 "Some required source files missing"
fi
echo ""

# Test 5: Check for required header files
echo "Testing header file presence..."
REQUIRED_HEADERS=(
    "../src/kernel/uart.h"
    "../src/kernel/boot_display.h"
    "../src/kernel/rom_loader.h"
    "../src/kernel/syscall.h"
    "../src/kernel/power.h"
    "../src/kernel/audio.h"
    "../src/kernel/k_libc/k_string.h"
)

ALL_HEADERS_PRESENT=true
for file in "${REQUIRED_HEADERS[@]}"; do
    if [ ! -f "$file" ]; then
        echo -e "${RED}✗${NC} Missing: $file"
        ALL_HEADERS_PRESENT=false
    fi
done

if [ "$ALL_HEADERS_PRESENT" = true ]; then
    print_result 0 "All required header files present"
else
    print_result 1 "Some required header files missing"
fi
echo ""

# Test 6: Check Makefile
echo "Testing Makefile..."
if [ -f "../build/Makefile" ]; then
    print_result 0 "Makefile exists"
else
    print_result 1 "Makefile not found"
fi
echo ""

# Test 7: Check linker script
echo "Testing linker script..."
if [ -f "../build/linker.ld" ]; then
    print_result 0 "Linker script exists"
else
    print_result 1 "Linker script not found"
fi
echo ""

# Test 8: Check binary size limits
echo "Testing binary size limits..."
cd ../build
export BCM=2836
make clean > /dev/null 2>&1
make > /dev/null 2>&1
if [ -f kernel7.img ]; then
    SIZE=$(stat -c%s kernel7.img 2>/dev/null || stat -f%z kernel7.img 2>/dev/null)
    if [ $SIZE -lt 100000 ]; then
        print_result 0 "Binary size within limits ($SIZE bytes < 100KB)"
    else
        print_result 1 "Binary size exceeds recommended limit ($SIZE bytes >= 100KB)"
    fi
else
    print_result 1 "Cannot check binary size - build failed"
fi
cd ../tests
echo ""

# Test 9: Check clean target
echo "Testing clean target..."
cd ../build
if make clean > /dev/null 2>&1; then
    if [ ! -f kernel7.img ] && [ ! -f kernel7.elf ]; then
        print_result 0 "Clean target works correctly"
    else
        print_result 1 "Clean target did not remove build artifacts"
    fi
else
    print_result 1 "Clean target failed"
fi
cd ../tests
echo ""

# Summary
echo "======================================"
echo "Test Summary"
echo "======================================"
echo -e "${GREEN}Passed:${NC} $TESTS_PASSED"
echo -e "${RED}Failed:${NC} $TESTS_FAILED"
echo ""

if [ $TESTS_FAILED -eq 0 ]; then
    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
else
    echo -e "${RED}Some tests failed.${NC}"
    exit 1
fi
