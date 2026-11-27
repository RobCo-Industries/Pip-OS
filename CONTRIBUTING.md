# Contributing to Pip-OS

Thank you for your interest in Pip-OS. This page explains how to report problems and send changes.

## Code of conduct

- Be respectful and inclusive.
- Accept constructive criticism gracefully.
- Focus on what is best for the community.
- Show empathy towards other community members.

## Reporting bugs

Check the existing issues first. A good report holds:

- What you did, what you expected and what happened.
- The board, or the QEMU board and QEMU version.
- The commit of Pip-OS and of the `external/robco-core` submodule.
- The serial output, including the `@RC` marker lines.

## Suggesting features

Open an issue that describes the use case. Check the [status section of the README](README.md#status) and the RobCo-Core specifications first: the ROM interface, the image format and the memory map are shared with other projects.

## Pull requests

1. Open an issue first for a large change.
2. Create a branch from `master`.
3. Make the change and its tests.
4. Run `make CI=1` and `make test`. Both must pass with no warning.
5. Update the documentation and `CHANGELOG.md`.
6. Open the pull request.

Write commit messages with a short summary line in the present tense, a blank line and an explanation of what and why.

## Development

[docs/BUILD.md](docs/BUILD.md) explains the build and the layout. [docs/TESTING.md](docs/TESTING.md) explains the tests.

- Kernel sources live in `kernel/`. Keep code that touches no hardware in its own files, such as `kernel/settings.c`, and add host tests for it in `tests/host/`.
- Use the RobCo-Core drivers. Do not edit `external/robco-core`. Fix library problems upstream in [RobCo-Core](https://github.com/RobCo-Industries/RobCo-Core), then move the submodule.
- After you move the submodule, update the commit and the image tag in `.github/workflows/ci.yml`. `make check-pins` tells you when they differ.
- Code must build for ARMv6: the same image runs on the Pi Zero.
- New behaviour needs a target test in `tests/` that checks a marker or the screen.

### System-call groups

Each group of system calls lives in `kernel/sys_<group>.c` and exports the interface in `kernel/sys.h`: a table of `PIPOS_SYS_ENTRY(GROUP, NAME, handler)` lines and four hooks, `init`, `image_start`, `image_end` and `poll`. The dispatcher in `kernel/syscall.c` finds every group. A group PR touches only its own files:

- Calls: add handlers and table lines in `kernel/sys_<group>.c`. More kernel files: add `kernel/sys_<group>_<part>.c`; the `Makefile` builds every `kernel/*.c`.
- Pure code for host tests: one line in the sorted `PURE_SRCS` list of the `Makefile`, and `tests/host/test_<name>.c`.
- Test ROMs: `tests/roms/<name>.c`, or a folder with its own `Makefile`. Target tests: `tests/test_<group>.py`. See [Writing a test ROM](docs/TESTING.md#writing-a-test-rom).
- Docs: the group's section in [docs/API.md](docs/API.md) and its lines in `CHANGELOG.md`, in the same PR.

Handlers check every pointer with the `pipos_user_*` helpers of `sys.h` and refuse holotapes with `pipos_sys_rom_only()` where the ABI says ROM only. A served call drops out of the `-PIP_ENOSYS` test by itself.

### Coding style

- C11 with GNU extensions, 4-space indentation, no tabs.
- `stdint.h` types for fixed-width values.
- Comments explain why, not what.
- Markers follow the format in RobCo-Core's [BOOT_PROTOCOL.md](https://github.com/RobCo-Industries/RobCo-Core/blob/c0bee999b3c0993bd627ca5c4b0ecc8512563fca/docs/spec/BOOT_PROTOCOL.md#serial-markers).

### Testing on hardware

Every test runs in QEMU today. If you test on a board, report the board, the result and the serial output in the pull request.

## License

By contributing to Pip-OS, you agree that your contributions are licensed under the project's MIT license.
