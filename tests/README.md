# PIP-OS Tests

## `qemu_boot_test.sh`

Builds the kernel, boots the ELF in QEMU with serial output to a file, and
fails unless the log contains the boot banner within 10 seconds.

```bash
bash tests/qemu_boot_test.sh 2835   # raspi0
bash tests/qemu_boot_test.sh 2836   # raspi2b
```

The serial log is written to `build/qemu-bcm<BCM>.log` and printed on failure.

## `run_tests.sh`

Build checks for BCM2835 and BCM2836, source and header presence, binary
size under 100 KB, and the `clean` target. Run it from `tests/`:

```bash
cd tests
bash run_tests.sh
```

## Prerequisites

```bash
sudo apt-get install -y gcc-arm-none-eabi qemu-system-arm
```

## CI

`.github/workflows/ci.yml` runs both scripts for BCM2835 and BCM2836 on every
push and pull request to `main` or `master`.

## Not covered

- BCM2837. It will return later as a 32-bit target.
- Unit tests of kernel code.
- Anything past the boot banner: UART input, ROM and holotape loading,
  system calls, power, audio.
- Real hardware.
