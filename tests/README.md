# PIP-OS Test Suite

This directory contains the test suite for PIP-OS V7.1.0.8.

## Test Files

### `run_tests.sh`
Main test runner that performs:
- Toolchain availability checks
- Build tests for all platforms (BCM2835, BCM2836, BCM2837)
- Source and header file presence verification
- Compilation checks
- Binary size validation
- Clean target verification

**Usage:**
```bash
cd tests
./run_tests.sh
# Or from repository root:
# bash tests/run_tests.sh
```

### `test_memory.py`
Unit tests for kernel memory management functions:
- `k_memcmp` - Compare memory regions
- `k_memcpy` - Copy memory regions
- `k_strlen` - Calculate string length
- `memset` - Fill memory with a value

These tests compile the kernel functions in a host environment to verify correctness.

**Usage:**
```bash
cd tests
python3 test_memory.py
```

## Running Tests Locally

### Prerequisites
- ARM cross-compiler toolchain (`gcc-arm-none-eabi`)
- Python 3 (for unit tests)
- GCC (for compiling host-side tests)
- Make

### Install Dependencies (Ubuntu/Debian)
```bash
sudo apt-get update
sudo apt-get install -y gcc-arm-none-eabi binutils-arm-none-eabi python3 build-essential
```

### Run All Tests
```bash
# From tests directory
cd tests
./run_tests.sh
python3 test_memory.py

# Or from repository root
bash tests/run_tests.sh
python3 tests/test_memory.py
```

## Continuous Integration

Tests are automatically run on every pull request via GitHub Actions.

The CI workflow includes:

### 1. Build and Test Job
- Build for BCM2835 (Raspberry Pi Zero/1)
- Build for BCM2836 (Raspberry Pi 2)
- Build for BCM2837 (Raspberry Pi 3) - if aarch64 toolchain available
- Run unit tests
- Run integration tests
- Check binary size limits
- Archive build artifacts

### 2. Static Analysis Job
- Check for trailing whitespace
- Find TODO/FIXME comments
- Verify header guards
- Validate code structure
- Check required files exist

### 3. Documentation Check Job
- Verify documentation files exist
- Check README content
- Validate documentation completeness

## Test Results

Tests output color-coded results:
- ✓ (green) - Test passed
- ✗ (red) - Test failed
- ⚠ (yellow) - Warning

## Adding New Tests

### Adding Build Tests
Edit `run_tests.sh` and add new test cases following the existing pattern.

### Adding Unit Tests
Create a new Python script in this directory or extend `test_memory.py` with additional test functions.

### Updating CI Workflow
Edit `.github/workflows/ci.yml` to add new CI checks or modify existing ones.

## Test Coverage

Current test coverage:
- ✓ Build system (BCM2835, BCM2836, BCM2837)
- ✓ Memory functions (unit tests)
- ✓ String functions (unit tests)
- ✓ Source file presence
- ✓ Header file presence
- ✓ Binary size limits
- ✓ Clean target functionality
- ✓ Static analysis
- ✓ Documentation presence

## Known Limitations

- Hardware-specific functions (UART, GPIO, sensors, etc.) cannot be easily tested without actual hardware or emulation
- Integration tests are limited to build-time checks
- No runtime tests on actual Raspberry Pi hardware in CI
- BCM2837 builds require aarch64 toolchain (not always available in CI)

## Future Improvements

Potential enhancements for the test suite:

- [ ] Add QEMU-based integration tests for boot sequence
- [ ] Implement mock hardware for testing drivers
- [ ] Add code coverage reporting
- [ ] Performance benchmarks
- [ ] Automated regression testing
- [ ] Test ROM loading mechanism
- [ ] Test system call interface
- [ ] Holotape format validation tests

## Troubleshooting

### Build Tests Fail
- Ensure ARM toolchain is installed: `arm-none-eabi-gcc --version`
- Check that you're in the correct directory
- Verify Makefile exists in `build/` directory

### Unit Tests Fail
- Ensure Python 3 is installed: `python3 --version`
- Ensure GCC is available for host compilation: `gcc --version`
- Check that memory functions are correctly implemented

### Permission Denied on run_tests.sh
Make the script executable:
```bash
chmod +x tests/run_tests.sh
```

## CI/CD Integration

The test suite integrates with GitHub Actions for continuous integration. See `.github/workflows/ci.yml` for the complete workflow definition.

### Workflow Triggers
- Pull requests to `main` or `master` branches
- Pushes to `main` or `master` branches

### Viewing CI Results
- Check the "Actions" tab in the GitHub repository
- View detailed logs for each job
- Download build artifacts from successful builds

## Contributing

When adding new features to PIP-OS:
1. Add corresponding tests to this suite
2. Ensure all existing tests still pass
3. Update this README if new test files are added
4. Follow the testing patterns established here

See `CONTRIBUTING.md` in the repository root for more details.

---

**RobCo Industries - Ensuring Quality Since 2075**
