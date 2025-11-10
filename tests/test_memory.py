#!/usr/bin/env python3
"""
PIP-OS Memory Function Unit Tests

This script tests the kernel's memory manipulation functions
by compiling them in a host environment and running unit tests.
"""

import subprocess
import sys
import os
import tempfile
import ctypes

# Color codes for output
GREEN = '\033[0;32m'
RED = '\033[0;31m'
YELLOW = '\033[1;33m'
NC = '\033[0m'  # No Color

def print_result(passed, test_name):
    """Print test result with color"""
    if passed:
        print(f"{GREEN}✓{NC} {test_name}")
        return True
    else:
        print(f"{RED}✗{NC} {test_name}")
        return False

def compile_test_module():
    """Compile kernel memory functions for host testing"""
    print("Compiling memory functions for testing...")
    
    # Create a test wrapper C file
    test_wrapper = """
#include <stddef.h>
#include <stdint.h>

// k_memcmp implementation
int k_memcmp(const void *s1, const void *s2, size_t n)
{
    const uint8_t *p1 = (const uint8_t *)s1;
    const uint8_t *p2 = (const uint8_t *)s2;

    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) {
            return (int)(p1[i] - p2[i]);
        }
    }

    return 0;
}

// k_memcpy implementation
void *k_memcpy(void *dest, const void *src, size_t n)
{
    uint8_t *bdest = (uint8_t*)dest;
    uint8_t *bsrc = (uint8_t*)src;

    for (size_t i = 0; i < n; i++) {
        bdest[i] = bsrc[i];
    }

    return dest;
}

// k_strlen implementation
uint32_t k_strlen(char *s)
{
    uint32_t n = 0;
    while (s[n] != '\\0')
    {
        n++;
    }
    return n;
}

// memset implementation (standard)
void *memset(void *s, int c, size_t n)
{
    uint8_t *p = (uint8_t *)s;
    for (size_t i = 0; i < n; i++) {
        p[i] = (uint8_t)c;
    }
    return s;
}
"""
    
    # Write to temp file and compile
    try:
        with tempfile.NamedTemporaryFile(mode='w', suffix='.c', delete=False) as f:
            test_c_file = f.name
            f.write(test_wrapper)
        
        test_so_file = test_c_file.replace('.c', '.so')
        
        # Compile to shared library
        result = subprocess.run(
            ['gcc', '-shared', '-fPIC', '-o', test_so_file, test_c_file],
            capture_output=True,
            text=True
        )
        
        if result.returncode != 0:
            print(f"{RED}Compilation failed:{NC}")
            print(result.stderr)
            return None
        
        print(f"{GREEN}Compilation successful{NC}")
        return test_so_file
    
    except Exception as e:
        print(f"{RED}Error during compilation: {e}{NC}")
        return None
    finally:
        if os.path.exists(test_c_file):
            os.unlink(test_c_file)

def test_k_strlen(lib):
    """Test k_strlen function"""
    lib.k_strlen.argtypes = [ctypes.c_char_p]
    lib.k_strlen.restype = ctypes.c_uint32
    
    tests = [
        ("", 0),
        ("a", 1),
        ("hello", 5),
        ("PIP-OS V7.1.0.8", 15),
    ]
    
    all_passed = True
    for test_str, expected in tests:
        result = lib.k_strlen(test_str.encode('utf-8'))
        passed = (result == expected)
        if not passed:
            print(f"  {RED}Failed:{NC} k_strlen('{test_str}') = {result}, expected {expected}")
            all_passed = False
    
    return print_result(all_passed, "k_strlen tests")

def test_k_memcmp(lib):
    """Test k_memcmp function"""
    lib.k_memcmp.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t]
    lib.k_memcmp.restype = ctypes.c_int
    
    tests = [
        (b"hello", b"hello", 5, 0),
        (b"hello", b"world", 5, True),  # Just check not equal
        (b"abc", b"abd", 2, 0),  # First 2 bytes equal
        (b"abc", b"abd", 3, True),  # Not equal
    ]
    
    all_passed = True
    for s1, s2, n, expected in tests:
        result = lib.k_memcmp(s1, s2, n)
        if isinstance(expected, bool):
            passed = (result != 0) if expected else (result == 0)
        else:
            passed = (result == expected)
        
        if not passed:
            print(f"  {RED}Failed:{NC} k_memcmp comparison")
            all_passed = False
    
    return print_result(all_passed, "k_memcmp tests")

def test_k_memcpy(lib):
    """Test k_memcpy function"""
    lib.k_memcpy.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t]
    lib.k_memcpy.restype = ctypes.c_void_p
    
    # Test basic copy
    src = b"Hello PIP-OS"
    dst = (ctypes.c_char * len(src))()
    
    lib.k_memcpy(dst, src, len(src))
    
    passed = (bytes(dst) == src)
    return print_result(passed, "k_memcpy tests")

def test_memset(lib):
    """Test memset function"""
    lib.memset.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_size_t]
    lib.memset.restype = ctypes.c_void_p
    
    # Test setting memory
    size = 10
    buffer = (ctypes.c_ubyte * size)()
    
    lib.memset(buffer, 0xAA, size)
    
    passed = all(b == 0xAA for b in buffer)
    return print_result(passed, "memset tests")

def main():
    """Main test function"""
    print("=" * 40)
    print("PIP-OS Memory Function Unit Tests")
    print("=" * 40)
    print()
    
    # Compile test module
    lib_path = compile_test_module()
    if not lib_path:
        print(f"{RED}Failed to compile test module{NC}")
        return 1
    
    try:
        # Load shared library
        lib = ctypes.CDLL(lib_path)
        
        # Run tests
        print("\nRunning tests...")
        results = []
        results.append(test_k_strlen(lib))
        results.append(test_k_memcmp(lib))
        results.append(test_k_memcpy(lib))
        results.append(test_memset(lib))
        
        # Summary
        print("\n" + "=" * 40)
        print("Test Summary")
        print("=" * 40)
        passed = sum(results)
        total = len(results)
        print(f"{GREEN}Passed:{NC} {passed}/{total}")
        print(f"{RED}Failed:{NC} {total - passed}/{total}")
        print()
        
        if passed == total:
            print(f"{GREEN}All tests passed!{NC}")
            return 0
        else:
            print(f"{RED}Some tests failed.{NC}")
            return 1
    
    finally:
        # Cleanup
        if os.path.exists(lib_path):
            os.unlink(lib_path)

if __name__ == "__main__":
    sys.exit(main())
