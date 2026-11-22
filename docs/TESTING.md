# Testing

`make test` runs the host tests, then the target tests in QEMU on all four boards.

| Command | Runs |
|---|---|
| `make test` | `make test-host`, then `make test-target` |
| `make test BOARD=raspi0` | The same, with the target tests on one board |
| `make test-host` | `make check-pins`, `make check-pins-test` and the host unit tests |
| `make test-target` | The QEMU tests. `BOARD` picks one board, `all` by default. |
| `make check-pins` | Checks the CI workflow pins, see below |
| `make check-pins-test` | Runs the pin check with a failing `git` |

## Host tests

`tests/host/test_<name>.c` files test the pure kernel code: configuration parsing in `kernel/settings.c` and boot line formatting in `kernel/bootlines.c`. They use RobCo-Core's framework, `external/robco-core/tests/host/rc_test.h`.

RobCo-Core's `mk/host.mk` builds them. The `Makefile` sets `HOST_TEST_DIR`, `HOST_SRCS` to the pure kernel files and `HOST_INCLUDES` to `kernel`. Each test links those files and the pure library, `external/robco-core/src/lib/`. It builds with `-Wall -Wextra -Werror` and with ASan and UBSan when the compiler supports them. `HOST_CC` picks the compiler, `HOST_SANITIZE=0` turns the sanitizers off. `make test-host` also runs the pin check, its test and `tests/host/build_dir_test.sh`, which checks that `BUILD` reaches the target tests.

## Target tests

The target tests boot `build/pipos.img` in QEMU with RobCo-Core's harness from `external/robco-core/tests/harness`. The harness loads a firmware stub, enters the image at `0x8000` like the firmware and reads the serial ports. `tests/conftest.py` loads RobCo-Core's pytest plugin, which adds the `--board` option and the `robco_build` fixture. `pytest.ini` sets the default boards with `robco_boards`. Pip-OS adds `--build-dir` and the `kernel_image` fixture, which builds the image in that directory once per session. The plugin builds only into `build` or `build/<variant>`, so `kernel_image` passes `BUILD` as a make flag and names the image itself. The first run creates `.venv` with pytest and Pillow.

| File | Covers |
|---|---|
| `tests/test_boot.py` | Boot without a card: markers, boot text on the screen and the UART, input backends, power-on beep, the golden screen, the script commands, the RTC |
| `tests/test_card.py` | Boot with a card: a valid ROM, bad ROMs, a missing ROM, configuration values and errors, colour schemes, the holotape line |
| `tests/test_input.py` | Events from the script channel and the serial console, control characters included |

Each card test builds its own SD card image with mtools in a temporary directory. `make_rom()` in `tests/pipos_helpers.py` builds a `ROM1` file with a title, an icon, a name and a version through `pack` in RobCo-Core's `tools/rimg.py`. The broken ROMs start from a valid one and change one field: a bad CRC, a size past the ROM slot, a header without the META flag. The tests never run the payload.

Tests read the screen in two ways: the `dump` command on the script channel, which `vm.script("dump")` sends, and a QEMU screenshot. See [BOOT.md](BOOT.md#maintenance-commands) for the commands. `tests/golden/boot_nocard.png` is the screen after a boot without a card. It must match on every board, pixel for pixel. Run the tests with `PIPOS_UPDATE_GOLDEN=1` to write it again after a deliberate change to the boot screen.

## CI

`.github/workflows/ci.yml` calls RobCo-Core's reusable workflow. It builds with `make CI=1 all test-host` in the RobCo CI container, uploads `build/pipos.img`, then runs `make CI=1 test-target BOARD=<board>` on each of the four boards.

A reusable workflow takes only literal inputs. So the workflow repeats two values from the submodule: the commit of the reusable workflow, which must be the pinned submodule commit, and the image tag, which must equal `external/robco-core/ci/IMAGE_TAG`. `make check-pins` fails when either differs. After you move the submodule, update both values in the workflow.

The CI job runs as root on a checkout owned by another user, so git refuses every command on the work tree. `make check-pins` runs RobCo-Core's `tools/check_pins.sh` on the workflow. It reads the submodule commit from `external/robco-core/.git` and its git directory instead. It uses git only as a cross-check when git works. `make check-pins-test` runs the check with a `git` that always fails.
