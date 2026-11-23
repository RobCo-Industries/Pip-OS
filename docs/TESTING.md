# Testing

`make test` runs the host tests, then the target tests in QEMU on all four boards.

| Command | Runs |
|---|---|
| `make test` | `make test-host`, then `make test-target` |
| `make test BOARD=raspi0` | The same, with the target tests on one board |
| `make test-host` | `make check-pins`, `make check-pins-test`, the shell tests, the `libpip` test and the host unit tests |
| `make test-target` | The QEMU tests. `BOARD` picks one board, `all` by default. |
| `make check-pins` | Checks the CI workflow pins and the RobCo-Core doc links, see below |
| `make check-pins-test` | Runs both checks with a failing `git` |

## Host tests

`tests/host/test_<name>.c` files test the pure kernel code of `PURE_SRCS`: configuration parsing, boot lines, the ROM runner helpers, the dispatcher and pointer helpers, and the rules of each call group in `test_gfx.c`, `test_input_events.c`, `test_audio.c`, `test_time.c`, `test_storage.c` and `test_holo.c`. `test_abi.c` checks `sdk/include/pipos/abi.h` against the spec: struct sizes and offsets, call numbers, constants. `test_sdk_calls.c` builds the wrappers of `sdk/lib/syscall.c` with a fake `pip_syscall()` and checks the number and arguments each one passes. They use RobCo-Core's framework, `external/robco-core/tests/host/rc_test.h`. Every `test_*.c` file is found by wildcard.

RobCo-Core's `mk/host.mk` builds them. The `Makefile` sets `HOST_TEST_DIR`, `HOST_SRCS` to the pure kernel files of `PURE_SRCS` and `HOST_INCLUDES` to `kernel` and `sdk/include`. Each test links those files and the pure library, `external/robco-core/src/lib/`. It builds with `-Wall -Wextra -Werror` and with ASan and UBSan when the compiler supports them. `HOST_CC` picks the compiler, `HOST_SANITIZE=0` turns the sanitizers off. `make test-host` also runs the pin check, its test and three shell tests:

| Script | Checks |
|---|---|
| `build_dir_test.sh` | `BUILD` reaches the target tests |
| `sdk_example_test.sh` | `sdk/examples/hello` builds from its folder and from a copy outside the tree, with an absolute and a relative `PIPOS_SDK`, and as a holotape. Each result passes `rimg.py verify` with the right magic, subtype and load address. |
| `sdk_rebuild_test.sh` | A ROM project rebuilds after each change: a source removed from `SRCS` leaves the ELF, a header edit rebuilds its object, a new `TITLE` reseals, new flags rebuild `libpip.a`. `start.c` and `start.S` both link, objects stay in `BUILD`, and a failed seal leaves no image. |
| `sdimage_rom_test.sh` | `make sdimage` puts the hello example on the card as `PIPBOY/ROM1.ROM`, `ROM=<file>` puts that file, `ROM=none` puts none. It reads the card with mtools. |
| `conformance_bootdir_test.sh` | `make conformance-bootdir FIRMWARE=0` writes the same files as `build/conformance.img`, under the same names and with the same bytes. It needs no network, so it does not test the firmware download. |

`tests/host/libpip_test.c` tests `libpip.a` on the host: printf conversions and truncation, the split of long `pip_printf` output into `log` calls, the string functions, and the heap. It is not a `test_*.c` file because it builds apart: the library sources get `tests/host/libpip_names.h` forced in, which renames `memcpy`, `strlen` and the others to `pipt_*` so they do not replace the host libc. `log` and `mem_info` are fakes in the test. `make libpip-host-test` runs it alone.

## Target tests

The target tests boot `build/pipos.img` in QEMU with RobCo-Core's harness from `external/robco-core/tests/harness`. The harness loads a firmware stub, enters the image at `0x8000` like the firmware and reads the serial ports. `tests/conftest.py` loads RobCo-Core's pytest plugin, which adds the `--board` option and the `robco_build` fixture. `pytest.ini` sets the default boards with `robco_boards`. Pip-OS adds `--build-dir` and the `kernel_image` fixture, which builds the image in that directory once per session. The plugin builds only into `build` or `build/<variant>`, so `kernel_image` passes `BUILD` as a make flag and names the image itself. The first run creates `.venv` with pytest and Pillow.

| File | Covers |
|---|---|
| `tests/test_boot.py` | Boot without a card: markers, boot text on the screen and the UART, input backends, power-on beep, the golden screen, the script commands, the RTC |
| `tests/test_card.py` | Boot with a card: a valid ROM, bad ROMs, a missing ROM, configuration values and errors, colour schemes, the holotape line, the default card from `make sdimage` running the hello example |
| `tests/test_input.py` | Events from the script channel and the serial console, control characters included |
| `tests/test_syscalls.py` | The served numbers in the `ev=syscalls` marker, and `-PIP_ENOSYS` from every call of `abi.h` the kernel does not serve |
| `tests/test_gfx.py` | The graphics calls: results, errors, limits, the screen goldens `gfx_*.png`, and who owns the screen before, during and after a ROM |
| `tests/test_input_calls.py` | The input calls with events injected on the script channel and the console |
| `tests/test_audio.py`, `tests/test_audio_sample.py` | The audio calls and `sample` through the trace backend |
| `tests/test_time.py` | The time, sensor, battery and power calls, with and without QEMU's RTC model |
| `tests/test_storage.py` | Saves and files on a card image, read back with mtools |
| `tests/test_holo.py` | The holotape calls, the sandbox and the state handover between the ROM and a holotape |
| `tests/test_rom.py` | Running the test ROMs: exit and return, entry state, each system call, the `sleep_ms` range and lengths, bad pointers, unknown numbers, saved registers, faults, script commands while a ROM sleeps or spins, the `libpip` self-check |
| `tests/test_conformance.py` | The conformance ROM on the card from `make conformance-card`, with scripted answers and in auto mode. See [Conformance ROM](#conformance-rom). |

While a test VM runs, RobCo-Core's harness sends QEMU a monitor ping every 20 ms. This keeps a rare QEMU stall on `raspi0` and `raspi1ap` from hanging a boot. `ROBCO_QEMU_KEEPALIVE=0` turns it off. When a wait times out, the error also shows a guest state dump, with function names from `build/pipos.elf` next to the image. See [QEMU keep-alive](https://github.com/RobCo-Industries/RobCo-Core/blob/d6f756d259d8e6faefa983a719560d19209121d0/docs/spec/TESTING.md#qemu-keep-alive).

Each card test builds its own SD card image with mtools in a temporary directory. `make_rom()` in `tests/pipos_helpers.py` builds a `ROM1` file with a title, an icon, a name and a version through `pack` in RobCo-Core's `tools/rimg.py`. Its payload starts with `exit(0)`, so the kernel runs it and returns to the main loop. The broken ROMs start from a valid one and change one field: a bad CRC, a size past the ROM slot, a header without the META flag.

## Test ROMs

`tests/roms/` holds small ROMs in C. `make` finds them by wildcard and builds each one through `sdk/pipos-app.mk` into `build/roms/<name>/<name>.rom`. They share one `libpip.a` and `rt.c`, which formats the marker lines. The `test_rom` fixture reads them, and each test puts one on its card as `/PIPBOY/ROM1.ROM`. The ROMs report results through the `log` call as `@RC stage=testrom` lines.

| ROM | Does |
|---|---|
| `hello` | Logs a line, then `exit(7)` |
| `ret5` | Returns 5 from `pip_main` |
| `tramp` | Returns through the `lr` it got at entry |
| `entry` | Reports the registers at entry and the startup block |
| `calls` | Calls each system call, with good and bad arguments, `sleep_ms` with `0x80000000` and `0xFFFFFFFF`, and sleeps from 1 ms to 50 ms timed with `ticks_ms`. `regs.S` checks r4 to r11, sp and lr across a call. |
| `fault_<case>` | One fault each, from `fault.c`: `wrkern`, `rdperiph`, `jmpkern`, `undef`, `priv`, `null`. It logs the faulting address first. |
| `sleep`, `spin` | Never end, with and without `sleep_ms` |
| `enosys` | Calls each number of `PIP_SYSCALLS` that the kernel does not serve. `test_syscalls.py` writes the served numbers into the ROM's `served_map` and reseals it before the boot. |
| `sdkcheck` | A ROM project of its own in `tests/roms/sdkcheck/`, built only with `pipos-app.mk` and `libpip.a`. It checks printf through `log`, the string functions, and heap exhaustion and recovery, and prints one `ev=check name=<name> ok=<0\|1>` line per check. |
| `gfx_*` | The graphics calls, the scenes of the screen goldens, a ROM that never presents, and exit and fault after drawing |
| `input` | The input calls, driven step by step from `test_input_calls.py` |
| `audio_*` | The audio calls and `sample`, and sounds the kernel must stop at exit |
| `time_*` | The clock with and without an RTC, `power` reboot, halt and screen off and on |
| `storage_*` | Saves, recovery, files, aliases of the save folder, saves over 64 KiB, a card that goes away, a save kept across two boots |
| `holo_*` | Holotape listing, launch, card errors and the state handover. `holo_tapes/tape.c` is the test holotape. |

## Writing a test ROM

1. Add `tests/roms/<name>.c`. It includes `rt.h` and defines `int pip_main(const pipos_startup_t *)`. `make` builds it with `rt.c` into `build/roms/<name>/<name>.rom`. A ROM with several files is a folder `tests/roms/<name>/` with its own `Makefile`, like `sdkcheck`.
2. Report each result with a named check. `rt_check(name, ok)` prints `ev=check name=<name> ok=<0|1>`. `rt_check_eq(name, got, expected)` adds `got=` and `expected=`. End with `rt_checks_done()`, which prints `ev=checks total=<n> failed=<n>`. Names have no spaces.
3. Add `tests/test_<topic>.py`. Put the ROM on a card and boot it, then call `assert_rom_checks(vm, names)` from `pipos_helpers.py`. It asserts the list of names, that each passed, and the summary. `rom_checks(vm)` returns the checks as dicts with `name`, `ok`, `got` and `expected`, for tests that look closer.

```c
#include "rt.h"

int pip_main(const pipos_startup_t *startup)
{
    (void)startup;
    rt_check_eq("ticks_positive", pip_ticks_ms() >= 0, 1);
    rt_check_eq("bad_number", pip_syscall(0x7f, 0, 0, 0, 0), -PIP_ENOSYS);
    return rt_checks_done();
}
```

```python
def test_mine(board, kernel_image, test_rom, tmp_path, need_mtools):
    sd = make_card(tmp_path / "sd.img", {"PIPBOY/ROM1.ROM": test_rom("mine")})
    with booted(kernel_image, board, sd=sd) as vm:
        assert_rom_checks(vm, ["ticks_positive", "bad_number"])
        assert one(vm, "rom_exit")["status"] == "0"
```

On `raspi0` and `raspi1ap` the `jmpkern` test asserts `rom_kill reason=undef` at the kernel's first privileged instruction, read from `pipos.elf` with `arm-none-eabi-objdump`. It fails if QEMU starts to raise the prefetch abort there. See [BOOT.md](BOOT.md#running-the-rom).

Tests read the screen in two ways: the `dump` command on the script channel, which `vm.script("dump")` sends, and a QEMU screenshot. See [BOOT.md](BOOT.md#maintenance-commands) for the commands. `tests/golden/boot_nocard.png` is the screen after a boot without a card. `card_hello_list.png` and `card_beacon.png` show the default card's hello example and tape. `gfx_prims.png`, `gfx_text.png` and `gfx_blits.png` are the graphics scenes. `boot_nocard.png` must match on every board, pixel for pixel. Run the tests with `PIPOS_UPDATE_GOLDEN=1` to write it again after a deliberate change to the boot screen.

## Conformance ROM

`conformance/` holds a ROM that runs every system call of the [ABI](https://github.com/RobCo-Industries/RobCo-Core/blob/d6f756d259d8e6faefa983a719560d19209121d0/docs/spec/PIPOS_ABI.md) and shows a PASS/FAIL table. It runs in QEMU and on a real Pi. The steps for a real board are in [HARDWARE_VALIDATION.md](HARDWARE_VALIDATION.md).

| File | Is |
|---|---|
| `conformance/rom/` | `CONFORM.ROM`: the checks, the prompts and the result screens |
| `conformance/tape/` | `CONFTAPE.HTP`: the companion holotape. Its argument picks a self-check, an exit status or a fault. |
| `conformance/mkdata.py` | Writes `CONFDATA.HTP`, a copy of the tape with subtype 2. `holo_launch` must refuse it. |

Both images build with `sdk/pipos-app.mk` and `libpip.a` only, like a third-party project.

```sh
make conformance          # build/conformance/: the ROM and both tapes
make conformance-card     # build/conformance.img
make sdimage ROM=build/conformance/rom/CONFORM.ROM TAPE=build/conformance/tape/CONFTAPE.HTP
```

`make conformance-card` writes the card through `make sdimage`, so `build/sd.img` holds the same card afterwards. It carries `PIPOS.CFG`, the ROM as `ROM1.ROM` and both tapes. The plain `make sdimage` line gives a card without the data tape: its two data-tape checks show ABSENT. To run the card in QEMU:

```sh
make conformance-card
make run BOARD=raspi2b
```

`make run` rebuilds `build/sd.img` from the default rules, so boot the card with the harness instead: `PYTHONPATH=external/robco-core/tests python3 -m harness.run build/pipos.img --board raspi2b --sd build/conformance.img`.

### What it covers

The ROM's table is built from `PIP_SYSCALLS` in `abi.h`, with one `check_<call>()` function per call. A call added to `abi.h` without its function fails the link, and a `_Static_assert` compares the table with `PIP_SYSCALL_COUNT`.

The automatic part needs no person. Each call runs on its main path and on the error paths the spec names: bad limits, bad pointers to kernel memory and across the end of the stack, missing files and saves. Drawing calls are checked by return value. A save named `CONFTEST` is written, read back, compared and deleted. The ROM reads `CONFTAPE.HTP` through the file calls and compares the bytes read with the size from `dir_list`. It launches the tape to check the tape's entry state and memory limits, the calls a holotape may not make, `exit`, and a fault in the ROM slot that must return `-PIP_EFAULT`.

`power` runs only `op` 2 and 3. Reboot and halt would end the run. `exit` is checked through the tape: the ROM's own exit ends the run.

The interactive part starts with an intro screen. It says what comes next and waits 10 minutes for SELECT, which starts the part, or LEFT, which skips it. LEFT or no answer makes every interactive check SKIP. The interactive part asks a person to press and hold each button, turn the dial both ways, say whether a tone, a tune, a sample and a sample in two chained halves were heard and whether a test picture is right. SELECT is yes, LEFT is no. Each prompt waits 30 s, then the check is SKIP. While a button is held, `read_buttons` must show it down, and after the release up. A tap released before the ROM reads asks again: it proves nothing. A sound starts once its prompt shows, and UP plays it again.

### Mode

ROMs get no argument string from the kernel. A file `/PIPBOY/CONFORM.CFG` that starts with `auto` makes the run automatic: the interactive checks are SKIP without a prompt, and every page and the summary go by on their own. Without the file, the run is interactive.

### Reading the results

| Result | Means |
|---|---|
| PASS | The call answered as the spec says |
| FAIL | It did not. The serial line, and a second row on screen, show the value it returned and the one expected. |
| SKIP | A prompt got no answer, or the run is automatic |
| ABSENT | The hardware is not there and the call said so as the spec requires, for example `-PIP_ENODEV` from a sensor |

The screen shows 12 rows per page: the check number, its name and the result. In an interactive run, the pages open on the first page with a FAIL, or on page 1. DOWN or RIGHT turns the page, UP or LEFT goes back, SELECT jumps to the summary. Each page waits for a button, at most 10 minutes. The summary shows the four counts and, after a FAIL, the pages that hold one. From the summary, UP or LEFT goes back to the last page, DOWN and RIGHT do nothing, and SELECT ends the run. The summary also ends the run after 10 minutes. In auto mode, each page shows for 0.3 s and the summary for 1 s. The ROM exits with status 0 when no check failed, 1 otherwise.

On the serial port, each check prints one line:

```
@RC stage=conform ev=check n=86 call=get_time name=time_at_boot result=ABSENT got=-41 expected=0
```

Then `ev=intro` when the intro screen shows and `ev=intro_answer start=<0|1>` once it is answered, `ev=prompt name=<check>` when a prompt is on screen, `ev=page n=<n> of=<pages>` per results page, and `ev=summary total= pass= fail= skip= absent= mode=` each time the summary shows. The tape prints `ev=tape_check name=<name> ok=<0|1>` for each of its own checks.

### Test

`tests/test_conformance.py` builds the card with `make conformance-card` and boots it on each board. One run presses SELECT on the intro, answers every prompt through the script channel, turns every page with DOWN, goes back from the summary to the last page once, then ends on SELECT. A second run, on `raspi0` and `raspi2b` only, presses LEFT on the intro: every interactive check must be SKIP. It asserts the exact list of check names, no FAIL, the ABSENT and SKIP sets, exit status 0, one results row per serial check line with the last check on the last page, and golden screens of the intro, the picture prompt, the first results page and the summary, `tests/golden/conform_intro.png`, `conform_picture.png`, `conform_page1.png` and `conform_summary.png`. The ROM has room for 319 results. A check past that is not shown: the ROM adds a FAIL named `results_overflow` instead, so the run fails. QEMU plays no sound a person can hear, so the "heard" answers are scripted and prove only that the prompts work. The script holds each button until its `_state` check shows, taps UP once to see the ROM ask again, and replays the sample once. A third run in auto mode must end by itself with every interactive check SKIP.

## CI

`.github/workflows/ci.yml` calls RobCo-Core's reusable workflow. It builds with `make CI=1 all test-host` in the RobCo CI container, uploads `build/pipos.img`, then runs `make CI=1 test-target BOARD=<board>` on each of the four boards.

A reusable workflow takes only literal inputs. So the workflow repeats two values from the submodule: the commit of the reusable workflow, which must be the pinned submodule commit, and the image tag, which must equal `external/robco-core/ci/IMAGE_TAG`. `make check-pins` fails when either differs. After you move the submodule, update both values in the workflow.

The CI job runs as root on a checkout owned by another user, so git refuses every command on the work tree. `make check-pins` runs RobCo-Core's `tools/check_pins.sh` on the workflow. It reads the submodule commit from `external/robco-core/.git` and its git directory instead. It uses git only as a cross-check when git works. `make check-pins-test` runs the check with a `git` that always fails.

`make check-pins` also runs `tools/check_doc_links.sh`. It fails when a Markdown file links to `github.com/RobCo-Industries/RobCo-Core/(blob|tree|commit)/<hash>` with another commit than the submodule's. `CHANGELOG.md` is skipped. It reads the commit the same way, without git.
