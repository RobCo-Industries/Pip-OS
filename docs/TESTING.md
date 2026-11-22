# Testing

`make test` runs the host tests, then the target tests in QEMU on all four boards.

| Command | Runs |
|---|---|
| `make test` | `make test-host`, then `make test-target` |
| `make test BOARD=raspi0` | The same, with the target tests on one board |
| `make test-host` | `make check-pins`, `make check-pins-test`, the shell tests, the `libpip` test and the host unit tests |
| `make test-target` | The QEMU tests. `BOARD` picks one board, `all` by default. |
| `make check-pins` | Checks the CI workflow pins, see below |
| `make check-pins-test` | Runs the pin check with a failing `git` |

## Host tests

`tests/host/test_<name>.c` files test the pure kernel code: configuration parsing in `kernel/settings.c`, boot line formatting in `kernel/bootlines.c`, and the pure parts of the ROM runner and system calls in `kernel/sysabi.c`. `test_sysabi.c` also checks the struct sizes and offsets of `sdk/include/pipos/abi.h`. They use RobCo-Core's framework, `external/robco-core/tests/host/rc_test.h`.

RobCo-Core's `mk/host.mk` builds them. The `Makefile` sets `HOST_TEST_DIR`, `HOST_SRCS` to the pure kernel files and `HOST_INCLUDES` to `kernel` and `sdk/include`. Each test links those files and the pure library, `external/robco-core/src/lib/`. It builds with `-Wall -Wextra -Werror` and with ASan and UBSan when the compiler supports them. `HOST_CC` picks the compiler, `HOST_SANITIZE=0` turns the sanitizers off. `make test-host` also runs the pin check, its test and three shell tests:

| Script | Checks |
|---|---|
| `build_dir_test.sh` | `BUILD` reaches the target tests |
| `sdk_example_test.sh` | `sdk/examples/hello` builds from its folder and from a copy outside the tree, with an absolute and a relative `PIPOS_SDK`, and as a holotape. Each result passes `rimg.py verify` with the right magic, subtype and load address. |
| `sdk_rebuild_test.sh` | A ROM project rebuilds after each change: a source removed from `SRCS` leaves the ELF, a header edit rebuilds its object, a new `TITLE` reseals, new flags rebuild `libpip.a`. `start.c` and `start.S` both link, objects stay in `BUILD`, and a failed seal leaves no image. |
| `sdimage_rom_test.sh` | `make sdimage` puts the hello example on the card as `PIPBOY/ROM1.ROM`, `ROM=<file>` puts that file, `ROM=none` puts none. It reads the card with mtools. |

`tests/host/libpip_test.c` tests `libpip.a` on the host: printf conversions and truncation, the split of long `pip_printf` output into `log` calls, the string functions, and the heap. It is not a `test_*.c` file because it builds apart: the library sources get `tests/host/libpip_names.h` forced in, which renames `memcpy`, `strlen` and the others to `pipt_*` so they do not replace the host libc. `log` and `mem_info` are fakes in the test. `make libpip-host-test` runs it alone.

## Target tests

The target tests boot `build/pipos.img` in QEMU with RobCo-Core's harness from `external/robco-core/tests/harness`. The harness loads a firmware stub, enters the image at `0x8000` like the firmware and reads the serial ports. `tests/conftest.py` loads RobCo-Core's pytest plugin, which adds the `--board` option and the `robco_build` fixture. `pytest.ini` sets the default boards with `robco_boards`. Pip-OS adds `--build-dir` and the `kernel_image` fixture, which builds the image in that directory once per session. The plugin builds only into `build` or `build/<variant>`, so `kernel_image` passes `BUILD` as a make flag and names the image itself. The first run creates `.venv` with pytest and Pillow.

| File | Covers |
|---|---|
| `tests/test_boot.py` | Boot without a card: markers, boot text on the screen and the UART, input backends, power-on beep, the golden screen, the script commands, the RTC |
| `tests/test_card.py` | Boot with a card: a valid ROM, bad ROMs, a missing ROM, configuration values and errors, colour schemes, the holotape line, the default card from `make sdimage` running the hello example |
| `tests/test_input.py` | Events from the script channel and the serial console, control characters included |
| `tests/test_rom.py` | Running the test ROMs: exit and return, entry state, each system call, the `sleep_ms` range and lengths, bad pointers, unknown numbers, saved registers, faults, script commands while a ROM sleeps or spins, the `libpip` self-check |

Each card test builds its own SD card image with mtools in a temporary directory. `make_rom()` in `tests/pipos_helpers.py` builds a `ROM1` file with a title, an icon, a name and a version through `pack` in RobCo-Core's `tools/rimg.py`. Its payload starts with `exit(0)`, so the kernel runs it and returns to the main loop. The broken ROMs start from a valid one and change one field: a bad CRC, a size past the ROM slot, a header without the META flag.

## Test ROMs

`tests/roms/` holds small ROMs in C. `make` builds each one through `sdk/pipos-app.mk` into `build/roms/<name>/<name>.rom`. They share one `libpip.a` and `rt.c`, which formats the marker lines. The `test_rom` fixture reads them, and each test puts one on its card as `/PIPBOY/ROM1.ROM`. The ROMs report results through the `log` call as `@RC stage=testrom` lines.

| ROM | Does |
|---|---|
| `hello` | Logs a line, then `exit(7)` |
| `ret5` | Returns 5 from `pip_main` |
| `tramp` | Returns through the `lr` it got at entry |
| `entry` | Reports the registers at entry and the startup block |
| `calls` | Calls each system call, with good and bad arguments, `sleep_ms` with `0x80000000` and `0xFFFFFFFF`, and sleeps from 1 ms to 50 ms timed with `ticks_ms`. `regs.S` checks r4 to r11, sp and lr across a call. |
| `fault_<case>` | One fault each, from `fault.c`: `wrkern`, `rdperiph`, `jmpkern`, `undef`, `priv`, `null`. It logs the faulting address first. |
| `sleep`, `spin` | Never end, with and without `sleep_ms` |
| `sdkcheck` | A ROM project of its own in `tests/roms/sdkcheck/`, built only with `pipos-app.mk` and `libpip.a`. It checks printf through `log`, the string functions, and heap exhaustion and recovery, and prints one `ev=check name=<name> ok=<0\|1>` line per check. |

On `raspi0` and `raspi1ap` the `jmpkern` test asserts `rom_kill reason=undef` at the kernel's first privileged instruction, read from `pipos.elf` with `arm-none-eabi-objdump`. It fails if QEMU starts to raise the prefetch abort there. See [BOOT.md](BOOT.md#running-the-rom).

Tests read the screen in two ways: the `dump` command on the script channel, which `vm.script("dump")` sends, and a QEMU screenshot. See [BOOT.md](BOOT.md#maintenance-commands) for the commands. `tests/golden/boot_nocard.png` is the screen after a boot without a card. It must match on every board, pixel for pixel. Run the tests with `PIPOS_UPDATE_GOLDEN=1` to write it again after a deliberate change to the boot screen.

## CI

`.github/workflows/ci.yml` calls RobCo-Core's reusable workflow. It builds with `make CI=1 all test-host` in the RobCo CI container, uploads `build/pipos.img`, then runs `make CI=1 test-target BOARD=<board>` on each of the four boards.

A reusable workflow takes only literal inputs. So the workflow repeats two values from the submodule: the commit of the reusable workflow, which must be the pinned submodule commit, and the image tag, which must equal `external/robco-core/ci/IMAGE_TAG`. `make check-pins` fails when either differs. After you move the submodule, update both values in the workflow.

The CI job runs as root on a checkout owned by another user, so git refuses every command on the work tree. `make check-pins` runs RobCo-Core's `tools/check_pins.sh` on the workflow. It reads the submodule commit from `external/robco-core/.git` and its git directory instead. It uses git only as a cross-check when git works. `make check-pins-test` runs the check with a `git` that always fails.
