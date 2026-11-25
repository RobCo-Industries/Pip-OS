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
| `flags_json_test.sh` | `make flags-json` writes every field of the spec in order with its type, and no flag holds a path. The example tape and ROM built by `make` and by `flags_json_build.py`, which reads only `flags.json`, have the same SHA-256. |
| `sdk_abi_target_test.sh` | The `ABI` variable: `abi_version` of a ROM and a holotape, the same image with no `ABI` and with `ABI := 1.4`, a rebuild when it changes, `sample_queue` and its number refused below 1.1 with the call and the ABI named, 1.4 constants still visible at 1.0, a target above 1.4 refused, bad values refused before any compile, and two builds of `libpip.a` with the same hash. |
| `sdk_rebuild_test.sh` | A ROM project rebuilds after each change: a source removed from `SRCS` leaves the ELF, a header edit rebuilds its object, a new `TITLE` reseals, new flags rebuild `libpip.a`. `start.c` and `start.S` both link, objects stay in `BUILD`, and a failed seal leaves no image. |
| `sdimage_rom_test.sh` | `make sdimage` puts the hello example on the card as `PIPBOY/ROM1.ROM`, `ROM=<file>` puts that file, `ROM=none` puts none. `TAPE` names that differ only in case, a name not ending in `.HTP`, or a `CARD_DIR` file with a tape's name stop the build, and a long tape name only warns. It reads the card with mtools. |
| `outside_project_test.sh` | A ROM project in a temporary folder builds its ROM with `pipos-app.mk`, then calls `make -C <pip-os>` with an absolute `BUILD`. `kernel` builds only the kernel. `sdimage` and `bootdir` with `ROM=<file>` and `TAPE=none` build no example and write the same files. `CARD_DIR` lands on the card, a `CARD_DIR` file at a standard path stops the build in any case, `TAPE` takes a list, `SDIMAGE_DIR` is refused and `bootdir` keeps an existing `PIPOS.CFG`. |
| `bootdir_test.sh` | `make bootdir FIRMWARE=0` run again into the same `DIR`: an identical run changes nothing, a stale ROM or tape stops it, `CLEAN=1` removes only what `BOOTDIR.LST` names, and `PIPBOY/PIPOS.CFG` and the person's own files stay. |
| `conformance_bootdir_test.sh` | `make conformance-bootdir FIRMWARE=0` writes the same files as `build/conformance.img`, under the same names and with the same bytes. It needs no network, so it does not test the firmware download. |

`tests/host/libpip_test.c` tests `libpip.a` on the host: printf conversions and truncation, the split of long `pip_printf` output into `log` calls, the string functions, the heap, the self-check markers, the INI parser and the CRC. It is not a `test_*.c` file because it builds apart: the library sources get `tests/host/libpip_names.h` forced in, which renames `memcpy`, `strlen` and the others to `pipt_*` so they do not replace the host libc. `log`, `ticks_ms` and `mem_info` are fakes in the test. `make libpip-host-test` runs it alone.

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
| `tests/test_tft.py` | The SPI panel in a test build: the panel picture against HDMI, the dirty rectangle, full frames, fallback to HDMI, refused pins, streaming while redrawing. See [SPI panel](#spi-panel). |
| `tests/test_time.py` | The time, sensor, battery and power calls, with and without QEMU's RTC model |
| `tests/test_storage.py` | Saves and files on a card image, read back with mtools |
| `tests/test_holo.py` | The holotape calls, the sandbox and the state handover between the ROM and a holotape |
| `tests/test_slot.py` | The ABI 1.4 holotape slot in a test build, with the RAM card backend. See [Holotape slot](#holotape-slot). |
| `tests/test_side_files.py` | A card with macOS side files, dot folders, hidden and system files: the holotape count, `holo_enumerate`, `dir_list` and a save match a clean card |
| `tests/test_rom.py` | Running the test ROMs: exit and return, entry state, each system call, the `sleep_ms` range and lengths, bad pointers, unknown numbers, saved registers, faults, script commands while a ROM sleeps or spins, the `libpip` self-check |
| `tests/tools/test_card_tool.py` | `tools/card.py` on its own, without a build or QEMU: the `TAPE` and `CARD_DIR` rules, the staged-card check and each `bootdir` stop, links at every position in `DIR` and in the sources, `BOOTDIR.LST` lines, read-only folders, and a sweep of 300 random trees from a fixed seed. Either a run stops and `DIR` is unchanged, or it holds exactly the staged files plus the files it left alone, and a file outside `DIR` that links point at never changes. Cases that need two spellings of a name run only on a case-sensitive temp folder; the run prints which branch it took. The read-only cases skip as root, as in the CI container. |
| `tests/tools/test_hosttiming.py` | `tests/hosttiming.py` on its own, without a build or QEMU: the stream judgement on the numbers of the failing CI run and of normal runs, the chains, the panel retries, and the session rule. See [Host timing](#host-timing). |
| `tests/test_env_hygiene.py` | No test copies the host environment; a failing test with `minimal_env()` prints no environment value |
| `tests/test_conformance.py` | The conformance ROM on the card from `make conformance-card`, with scripted answers, in auto mode, and in auto mode with the clock chip. See [Conformance ROM](#conformance-rom). |

While a test VM runs, RobCo-Core's harness sends QEMU a monitor ping every 20 ms. This keeps a rare QEMU stall on `raspi0` and `raspi1ap` from hanging a boot. `ROBCO_QEMU_KEEPALIVE=0` turns it off. When a wait times out, the error also shows a guest state dump, with function names from `build/pipos.elf` next to the image. See [QEMU keep-alive](https://github.com/RobCo-Industries/RobCo-Core/blob/cab5f69b88f982e145adba717962ae69b47dc866/docs/spec/TESTING.md#qemu-keep-alive).

The helpers live in the SDK, in `sdk/testing/pipos_testing/`, so a ROM project can use them too. `tests/pipos_helpers.py` re-exports them. Each card test builds its own SD card image with mtools in a temporary directory. `make_rom()` builds a `ROM1` file with a title, an icon, a name and a version through `pack` in RobCo-Core's `tools/rimg.py`. Its payload starts with `exit(0)`, so the kernel runs it and returns to the main loop. The broken ROMs start from a valid one and change one field: a bad CRC, a size past the ROM slot, a header without the META flag.

A test that runs a subprocess builds its environment with `minimal_env()` from RobCo-Core's `harness.env`, and passes the variables it needs as arguments. Never copy `os.environ`: pytest prints locals when a test fails, so a copy puts every host secret in the log. `tests/test_env_hygiene.py` fails on such a copy.

## SPI panel

QEMU models the SPI0 controller but no panel. Writes go out and every byte read is 0, so `tft_read_id()` reports no panel.

`tests/test_tft.py` builds a second kernel into `<build-dir>/tft` with `EXTRA_CFLAGS=-DPIPOS_TFT_TEST`. That build:

- taps every byte sent to the panel with its DC level, and prints `ev=tft_send why x y w h bytes crc` per send. `why` is `full`, `diff`, `retry` for the whole frame sent again after a failed send, or `wake` for the sleep out and display on commands a panic sends to a blanked panel. `crc` is the CRC-32 of the stream, each byte fed with its DC level first, as RobCo-Core's `examples/tft` does.
- keeps every byte since `tft_init()` from `0x00D80000`, up to 2.5 MiB.
- answers the `tftdump` script command with that transcript. `C` lines hold command bytes. `D` lines hold data as 16-bit words in hex, with `*<count>` for repeats. An `end bytes=<n> full=<0|1>` line closes it.
- waits after each send until the bytes would have left the wire at the rate set, so timing is not shorter than on a panel.
- accepts `[display] tft_test = panel` to treat the panel as present, and `tft_test = fail` to give `tft_init()` a config it refuses.

`test_shipped_image_has_no_hook` checks that `build/pipos.img` holds none of this and answers `tftdump` with an error.

`build/host/panel_model`, from `tests/host/panel_model.c`, feeds the transcript to RobCo-Core's ILI9341 model, `external/robco-core/tests/host/ili9341_model.h`, and prints the picture as the panel shows it. It fails when the byte count differs from the dump's `end` line. The test compares it with the HDMI screenshot of the same frame: every pixel, after scaling down by 2 and conversion to RGB565.

A whole frame is about 150 KiB on the wire. The repeat counts shrink flat colours. After a boot with `backend = both`, the transcript holds 189861 bytes, the boot console frames, and the dump is about 85 KB of text. It takes about 0.4 s in QEMU, whose serial ports have no baud rate. At 115200 baud it would take about 7.4 s. A frame with no repeats would be about 300 KB of hex, about 27 s.

A full-frame `present` takes about 128 ms on a panel at 9.62 MHz, and 200 to 600 ms in QEMU. QEMU's `raspi3b` starts all four cores. RobCo-Core's harness points cores 1 to 3 at its AArch64 shim too, and the firmware stub parks them in `WFI`, for every boot. Its canary V18 checks this. Before it did, they looped through an exception vector and held QEMU's big lock often. The kernel's polled SPI needs that lock on every register access, so a present took 3 s there on a Mac and 8.8 s on a GitHub runner. The streaming test uses 8 s chunks, so a present has a margin of about 20 times.

## Holotape slot

QEMU has no SD card on SPI0, so the real slot driver cannot run there. A test build with `-DPIPOS_HOLO_TEST` replaces it with `kernel/slot_test.c`, a card served from RAM. `tests/test_slot.py` builds it into `build/holotest`. The test puts card images in guest RAM with QEMU's `-device loader`, card `n` at `0x02000000 + n * 0x00800000`, and drives the slot with the `holo` script command:

| Command | Effect |
|---|---|
| `holo insert <n> <blocks>` | Card `n` goes in. The backend reports it present after 250 ms, as the driver's debounce. Replies `t_ms`. |
| `holo pull` | The card leaves. Replies `t_ms`. |
| `holo pullin <ms>` | The card leaves `<ms>` from now. It reaches a tape that keeps the script channel from running. Replies `t_ms`. |
| `holo readfail` | The next read fails with the card in |
| `holo readpull` | The next read pulls the card |
| `holo refuse` | The next attach ends in `SD_ERR_CMD` |
| `holo slow` | The next attach ends in `SD_ERR_SLOW` |
| `holo again <k>` | The next `k` attach starts return `SD_ERR_AGAIN` |
| `holo attachpull` | The next attach's first step pulls the card and returns `BLK_ERR_NOMEDIA` |
| `holo delay <ms>` | Every attach steps for `<ms>` before its result, as a slow card. Each step returns at once. |
| `holo pending` | Replies `attaching=<0\|1> t_ms`: 1 while an attach runs |

The backend's attach is stepped like the library's. A start that runs prints `ev=holo_attach_start t_ms`. Each attach, a refused start included, prints at its end `ev=holo_attach rc ms polls steps t0_ms t1_ms pcm0 pos0 pcm1 pos1`. `polls` counts the `pipos_poll()` passes after the start's pass, `steps` the step calls. One step per pass makes them equal. `t0_ms` and `t1_ms` are `tick_ms()` at its start and end, the clock of the audio trace's `t_ms` within one tick. `pcm0` and `pcm1` are `audio_busy()` at its start and end. `pos0` and `pos1` are the samples the current PCM play has output then. Each pair is read with IRQs masked. The slow attach tests judge the sound from these fields. Where they read the audio trace, they first wait for the ROM's `ev=wait` after a marker that comes after the records they need, then send a `holo` command. The trace prints at the start of each `pipos_poll()` pass, so the pass that answers has printed those records. `test_shipped_image_has_no_test_backend` checks that the shipped image holds none of it and answers `holo` with `err unknown command`.

The `slow` case of `test_attach_fails_in_steps_then_card_works` is the test of the late token limit in [ARCHITECTURE.md](ARCHITECTURE.md#a-slow-card): a stepped attach that ends in `SD_ERR_SLOW` makes the card bad.

What the backend can fake: card detect, the debounce, each attach result, a read error, a pull at any read or step, a slow attach. What it cannot: the real `holo.h` driver and its step bound, its pins, the bus buffer and supply, SPI0 timing and sharing with the panel, the lock tab, a card that answers slowly in the middle of a read. The real driver has met only RobCo-Core's card model in its own tests. No board with the slot exists yet, so the shipped path is unverified until hardware.

## Host timing

QEMU runs without icount, so the guest clock follows the host. A stalled host stretches guest time, and a test with a time limit can fail with a correct kernel. `tests/hosttiming.py` judges such tests with numbers from the same run.

Host timing may excuse only the check that the evidence explains. A test states its checks with `split_checks()` or `judged_split()`. The strict part holds every check a slow host cannot break, and it fails in every run. Each timed check is a `Timed` with a name and its own evidence. A timed check fails unless its own evidence is present, whatever the other checks show:

- `test_stream_while_redrawing`: the ROM reports `max_gap_ms`, the longest time between two queue calls. Strict: the frame count and the full send count, every chunk once, in order, with its CRC, each chunk played in full, and the end of the stream. Timed `underrun`: excused when the gap plus one 10 ms tick reaches a chunk. Timed `late switch`, more than 500 ms late: excused when the gap reaches a chunk and also the switch's lateness. A fresh start after an underrun starts a new time base.
- `test_sample_queue`: the ROM reports `ev=l_gap max_ms`, the same gap in the long chain of 250 ms chunks. Strict: the whole trace before the long chain, every long chain chunk once, in order, with its length and CRC, and each chunk played in full. Timed: `underrun` and `late switch`, as above. An underrun is the only trace change a slow host can cause: a `pcm_end` then a fresh `pcm` of the same chunk in place of its `pcm_next`.
- The `tft` tests that check exact sends, in `tests/panelsends.py`: a retried present adds one whole-frame send with `why=retry` and cuts the failed send short. The kernel prints `ev=display_error op=present` before the first retry of a run. The tests take out each retry send and check every other send, rectangle and byte count in every run. Timed `bytes of a retried present`: only the byte count of a send the kernel retried, excused by that retry.

The slot tests in `tests/test_slot.py` raise `CannotJudge` themselves. Each checks every result that does not depend on timing first. It excuses a run only when the run did not meet its case, and only with a second piece of evidence that a kernel fault cannot give. `holo insert`, `holo pull` and `holo pullin` reply `t_ms=<tick_ms()>`, the time they took effect:

- `test_pull_while_slot_tape_runs`: the pull must land while the tape runs. The ROM's `ev=launch` carries `t_ms`. A launch that returns before the pull was due fails. A pull before the load is excused only with `-PIP_ENOENT` or `-PIP_EIO` and no `holo_start`. A pull after `holo_start` and before the tape's `ev=tloop` is excused after the kill checks.
- `test_pull_in_debounce`: the pull must land within the 250 ms debounce. An attach is excused only when the pull came 250 ms or more after the insert, by their `t_ms`.
- `test_slow_attach_keeps_main_loop`: strict in every run, the attach's own witness: `rc` 0, `ms` 1200 or more, `steps` 2 or more and `polls` equal to `steps`. A `holo pending` sent during the attach must reply `attaching=1`. A reply of 0 is excused only when its `t_ms` is past the attach's end. The sample must play at the attach's start and end, `pcm0` and `pcm1`. A sample not playing then is excused only when its 7.5 s length says it was due to end by then.
- `test_slow_attach_keeps_a_stream_live`: strict, the same witness, an attach a debounce after the insert and at least 12 chunks queued. A play must run at both ends of the attach, with no `pcm_end` in it and a `pcm_next` every 100 ms through it. A stream that ran dry is excused only when the ROM's `ev=maxgap ms`, its longest time between two queue calls, plus one tick reaches a chunk.
- `test_pending_attach_no_tape_yet` and `test_pull_in_pending_attach_then_second_card`: the attach steps for 3000 ms, and the listing, launch, mask and pull must land inside it. `holo pending` checks that afterwards. A reply of 0 is excused only when the attach's marker shows the stepped witness above and the reply's `t_ms` is 3000 ms or more after the start. A kernel that held the loop fails the witness.

When the timed checks fail and each has its evidence, the test runs again, up to 4 runs. Then it skips with a reason that starts with `host timing:` and prints the numbers. Any other failure fails at once.

A skip must not hide a failure. The session counts `host timing:` skips per board, lists them at the end, and fails when any board has more than one. A skip belongs to the `board` parameter of its test. Skips of tests without one count together under `no board`. `--timing-skips-max=<n>` allows n skips per board. `make test-target` prints every skip reason with `-rs`. `tests/tools/test_hosttiming.py` tests the judgement and the rule without QEMU.

## Test ROMs

`tests/roms/` holds small ROMs in C. `make` finds them by wildcard and builds each one through `sdk/pipos-app.mk` into `build/roms/<name>/<name>.rom`. They share one `libpip.a`. `rt.h` gives its self-check markers, `pipos/selftest.h`, the short `rt_` names. The `test_rom` fixture reads them, and each test puts one on its card as `/PIPBOY/ROM1.ROM`. The ROMs report results through the `log` call as `@RC stage=testrom` lines.

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
| `tft_*` | Panel frames for `test_tft.py`: a scene, small changes and `power`, streaming while redrawing, and changes outside the window the panel shows at 0 degrees |
| `time_*` | The clock with and without an RTC, `power` reboot, halt and screen off and on |
| `storage_*` | Saves, recovery, files, aliases of the save folder, saves over 64 KiB, a card that goes away, a save kept across two boots |
| `side_files` | Prints the holotape listing, `dir_list` of four folders and a save's size, for `test_side_files.py` |
| `holo_*` | Holotape listing, launch, card errors and the state handover. `holo_tapes/tape.c` is the test holotape. |

## Writing a test ROM

1. Add `tests/roms/<name>.c`. It includes `rt.h` and defines `int pip_main(const pipos_startup_t *)`. `make` builds it into `build/roms/<name>/<name>.rom`. A ROM with several files is a folder `tests/roms/<name>/` with its own `Makefile`, like `sdkcheck`.
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

## Testing a ROM project

A ROM project that has Pip-OS as a submodule tests its ROM with the same helpers. They are the `pipos_testing` package in `sdk/testing/`. Its plugin loads RobCo-Core's `harness.plugin`, which adds `--board`, the `board` fixture and `need_mtools`. Importing the package puts RobCo-Core's harness and `rimg.py` on `sys.path`, so the project's `tests/conftest.py` needs only the Pip-OS root:

```python
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "external", "pip-os", "sdk", "testing"))
pytest_plugins = ["pipos_testing.plugin"]
```

Build the kernel and the ROM first with `make -C external/pip-os kernel BUILD=<absolute folder>` and the project's own `make`, see [ROM_DEVELOPMENT.md](ROM_DEVELOPMENT.md#using-pip-os-from-another-repository). The package needs the Python packages of RobCo-Core's `tests/requirements.txt`: pytest and Pillow.

The main helpers:

| Helper | Does |
|---|---|
| `booted(image, board, sd=None, wait="ready")` | Boots the kernel in QEMU with the script channel and yields the VM. `wait="ready"` waits for `ev=ready`, which the kernel prints only when no ROM runs. `wait="jump"` waits for `ev=jump`, printed right before the kernel enters the ROM: use it for a ROM that never exits. `wait=None` does not wait. |
| `make_card(path, files)` | An SD card image with the given files, such as `{"PIPBOY/ROM1.ROM": data}` |
| `script(vm, command)`, `press(vm, key)` | One script command, checked to answer `ok`, and a press and release of a button: `up`, `down`, `left`, `right`, `select`, `dial_push`, `back` |
| `screenshot(vm, tmp_path)`, `check_golden(img, path)` | A 640x480 screenshot, and its pixel compare with a PNG. With `PIPOS_UPDATE_GOLDEN=1` set, `check_golden` writes the PNG first. Pass another variable name as `update_var`. |
| `rom_checks(vm)`, `assert_rom_checks(vm, names)` | The checks a ROM printed with `pipos/selftest.h` |
| `pipos_markers(vm, ev)`, `one(vm, ev)`, `no_exception(vm)` | The kernel's `@RC stage=pipos` markers, and a check that nothing faulted |

`vm.wait_for(text)` waits for a line on the serial console. A ROM logs with `pip_printf`, which goes there. A test that boots the project's ROM on the four boards, waits for its first line, presses SELECT and compares the screen:

```python
import pathlib

from pipos_testing import booted, check_golden, make_card, no_exception, press, screenshot

ROOT = pathlib.Path(__file__).resolve().parent.parent
KERNEL = ROOT / "build" / "pipos" / "pipos.img"
ROM = ROOT / "build" / "ROM1.ROM"


def test_menu(board, tmp_path, need_mtools):
    sd = make_card(tmp_path / "sd.img", {"PIPBOY/ROM1.ROM": ROM.read_bytes()})
    with booted(KERNEL, board, sd=sd, wait="jump") as vm:
        vm.wait_for("myrom: ready\n")
        press(vm, "select")
        vm.wait_for("myrom: menu\n")
        check_golden(screenshot(vm, tmp_path), ROOT / "tests" / "golden" / "menu.png")
        no_exception(vm)
```

Run it with `pytest tests --board all`. `tests/test_outside_project.py` proves the import: it builds a small project in a temporary folder and runs its own pytest on one board.

For self-checks inside the ROM, include `<pipos/selftest.h>`. `pip_check(name, ok)` and `pip_check_eq(name, got, expected)` print one `@RC stage=testrom ev=check` line each, and `pip_checks_done()` prints the summary that `assert_rom_checks` reads. The test ROMs of Pip-OS use the same functions through `tests/roms/rt.h`.

## Conformance ROM

`conformance/` holds a ROM that runs every system call of the [ABI](https://github.com/RobCo-Industries/RobCo-Core/blob/cab5f69b88f982e145adba717962ae69b47dc866/docs/spec/PIPOS_ABI.md) and shows a PASS/FAIL table. It runs in QEMU and on a real Pi. The steps for a real board are in [HARDWARE_VALIDATION.md](HARDWARE_VALIDATION.md).

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

What the run leaves behind: it deletes `CONFTEST` again, but the save write creates `/PIPBOY/SAVE` if it was missing, and the folder stays. `set_time` also writes the real-time clock chip when one answers. The ROM restores a valid old time within about a second. A chip with no valid time keeps 2023-11-14.

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

`tests/test_conformance.py` builds the card with `make conformance-card` and boots it on each board. One run presses SELECT on the intro, answers every prompt through the script channel, turns every page with DOWN, goes back from the summary to the last page once, then ends on SELECT. A second run, on `raspi0` and `raspi2b` only, presses LEFT on the intro: every interactive check must be SKIP. It asserts the exact list of check names, no FAIL, the ABSENT and SKIP sets, exit status 0, one results row per serial check line with the last check on the last page, and golden screens of the intro, the picture prompt, the first results page and the summary, `tests/golden/conform_intro.png`, `conform_picture.png`, `conform_page1.png` and `conform_summary.png`. The ROM has room for 319 results. A check past that is not shown: the ROM adds a FAIL named `results_overflow` instead, so the run fails. QEMU plays no sound a person can hear, so the "heard" answers are scripted and prove only that the prompts work. The script holds each button until its `_state` check shows, taps UP once to see the ROM ask again, and replays the sample once. A third run in auto mode must end by itself with every interactive check SKIP. A fourth run in auto mode adds QEMU's DS1338 clock chip: `sensor_rtc` and `time_at_boot` pass instead of ABSENT, and `hw_flags` has bits 0 and 2 set. Without the chip it has bit 2 only. The `sysinfo_hw_*` checks follow the spec rules in `conformance/rom/hw_flags.h`. `PIP_HW_RTC` says only that a chip answered, so `get_time` may return 0 or `-PIP_ENODEV` with the bit set or clear. With `PIP_HW_AUDIO` set, a short tone must start and `audio_status` must read 1. With the bit clear, the tone may play or return `-PIP_EIO`. The reserved bits must be 0. `tests/host/test_conform_hw.c` tests these rules on the host. It covers a chip with an invalid time, which QEMU's DS1338 cannot hold. A fifth run in auto mode boots a kernel built with the PWM audio backend forced. On `raspi0` and `raspi1ap`, QEMU's PWM registers abort, so `audio_init()` fails and the trace backend plays the sound. `hw_flags` must still have bit 2 set. A full run records 216 checks.

## Hardware results

Each run on a real board is recorded here, with the commit that ran. A result holds only for that board and that commit.

Every run below used Pip-OS `2d10c2c` with RobCo-Core `f5bba36`. No later commit of Pip-OS has run on a board, so ABI 1.3 and 1.4, the SPI panel backend, the holotape slot and this tree with RobCo-Core `cab5f69` have not run on a board. RobCo-Core records its own hardware runs in its docs.

### Hardware checklist

| Item | Status |
|---|---|
| Boot to the ROM on a Pi Zero or Zero W | Done on a Zero W 1.1 at `2d10c2c`, see [Zero W 1.1 run](#zero-w-11-run-2026-11-24) |
| Boot on a Pi Zero, Pi 1, Pi 2 or Pi 3 | Open |
| MMU, heap and 100 Hz tick start | Done on a Zero W 1.1 at `2d10c2c` |
| Serial console: boot log and markers on the PL011 | Done on a Zero W 1.1 at `2d10c2c` |
| Serial console keys as ROM input | Done on a Zero W 1.1 at `2d10c2c`: arrow keys, select and back, one press at a time |
| Card: mount, `PIPOS.CFG`, ROM check and load | Done on a Zero W 1.1 at `2d10c2c`, with the default `PIPOS.CFG` on one FAT32 partition |
| Holotapes | Partly done on a Zero W 1.1 at `2d10c2c`: one holotape ran from the ROM, took serial keys and exited with status 0, and the ROM went on. See [run 2](#zero-w-11-run-2-2026-11-24). No holotape fault or kill. |
| ROM in user mode with system calls | Partly done on a Zero W 1.1 at `2d10c2c`: file reads, saves, input, the system information, a holotape launch. No fault was tested. |
| Saves on the card | Partly done on a Zero W 1.1 at `2d10c2c`: the kernel made `PIPBOY/SAVE/`, five saves left no work file, and the save was read back at the next boot. A power cut during a save and a save by a holotape are not tested. |
| HDMI picture | Open |
| GPIO buttons | Open |
| Dial | Open |
| Sound on the PWM output | Open. The PWM registers answered the probe; nobody listened. |
| RTC chip | Open. Only `rtc=absent` with no chip is seen. |
| SPI panel | Open |
| The conformance ROM, see [HARDWARE_VALIDATION.md](HARDWARE_VALIDATION.md) | Open |
| ABI 1.4 holotape slot | Open. No board with the slot exists. To check: insert, list, launch and pull on the real driver, a slow card's attach time, and SPI0 shared with the panel. |
| ABI 1.3 `hw_flags` from `sys_info` | Open. `2d10c2c` predates ABI 1.3. To check: `PIP_HW_RTC` set with a clock chip fitted and clear without one, and `PIP_HW_AUDIO` set when the audio driver starts. |

### Zero W 1.1 run, 2026-11-24

| Field | Value |
|---|---|
| Board | Raspberry Pi Zero W rev 1.1, BCM2835 |
| Revision code | `0x009000c1` |
| Pip-OS | [`2d10c2c`](https://github.com/RobCo-Industries/Pip-OS/commit/2d10c2c), before the SPI panel backend |
| RobCo-Core | `f5bba36`, the submodule of that commit |
| ROM | Deitrix `ba21d5a`, built against Pip-OS `2d10c2c`, as `PIPBOY/ROM1.ROM` |
| Firmware | Tag `1.20260915` |
| Date | 2026-11-24 |
| Card | 16 GB SDHC, one FAT32 partition with an MBR. 22 files from Deitrix `make bootdir`: the firmware, `PIPOS.IMG`, `config.txt`, the default `PIPBOY/PIPOS.CFG`, `PIPBOY/ROM1.ROM`, `PIPBOY/HOLO/CULVERT.HTP` and the `PIPBOY/DATA` tree. No `.dtb` and no `overlays/`. |
| Serial | CP2102 USB adapter at 3.3 V on header pins 6, 8 and 10, 115200 8N1 |
| Nothing else wired | No HDMI screen, speaker, buttons, dial or clock chip |

The boot log, exact:

```
@RC stage=pipos ev=boot midr=0x410fb767 soc=bcm2835 base=0x20000000 board_rev=0x009000c1 mem=0x1c000000 entry_mode=svc
@RC stage=pipos ev=mmu on=1
@RC stage=pipos ev=heap base=0x0006f8d0 size=3737392
@RC stage=pipos ev=tick hz=100
@RC stage=pipos ev=input gpio=0 serial=0 mini=0 script=0 repeat=0
@RC stage=pipos ev=audio_init rc=0 backend=pwm probe=ok
@RC stage=pipos ev=clock rtc=absent time=unset
@RC stage=pipos ev=card rc=ok
@RC stage=pipos ev=mount part=0 type=0x0b lba=8192 blocks=31107072 marker=1
@RC stage=pipos ev=config rc=ok path=/PIPBOY/PIPOS.CFG line=0 scheme=green line_delay_ms=150 volume=80 beep=1
@RC stage=pipos ev=gfx bb=0x00c00000 size=307200 user=0 heap_end=0x00400000
@RC stage=pipos ev=syscalls served=0000000f00ff07ff0003007f000ffffe
*************** PIP-OS(R) V7.1.0.8 ***************
COPYRIGHT 2075 ROBCO IND.
**************************************************
LOADER V1.1
EXEC VERSION 41.10
64K RAM SYSTEM
38911 BYTES FREE
@RC stage=pipos ev=holo count=1
1 HOLOTAPE FOUND
@RC stage=pipos ev=load path=/PIPBOY/ROM1.ROM size=71672 crc=0xacfaddef addr=0x00400000 name=DEITRIX
@RC stage=pipos ev=rom rc=ok name=DEITRIX version=0.1.0 size=71672 crc=0xacfaddef
LOAD ROM(1): DEITRIX 0.1.0
**************************************************
PIP-OS KERNEL INITIALIZED
PIP-OS READY
**************************************************
@RC stage=pipos ev=jump entry=0x00400000
deitrix: load PROFILE.INI gauges=4 attrs=6 about=3
deitrix: load ITEMS.INI items=14 categories=4
deitrix: load NOTES notes=4
deitrix: load MAP.INI maps=2 lines=16 places=12
deitrix: load RADIO.INI stations=5
deitrix: load RADIO/MORNING.TUN notes=49 ms=28800
deitrix: load RADIO/WALK.TUN notes=56 ms=14530
deitrix: load RADIO/LULLABY.TUN notes=19 ms=19994
deitrix: load RADIO/HEARTH.PCM bytes=256000
deitrix: load RADIO/DRIFT.PCM bytes=256000
deitrix: load DEITRIX.SAV missing tab=0 volume=80 sound=1 station=0
deitrix: load INV.SAV missing
deitrix: ready
```

Key lines:

- `ev=boot ... board_rev=0x009000c1 entry_mode=svc`: the firmware started the kernel on a Zero W 1.1.
- `ev=mmu on=1`: the MMU enable bit was set after `mmu_enable()`.
- `ev=input gpio=0 serial=0 mini=0 script=0`: each input backend's start returned 0, which is success.
- `ev=audio_init rc=0 backend=pwm probe=ok`: the PWM registers answered and kept a write.
- `ev=mount part=0 type=0x0b ... marker=1`: the kernel mounted MBR slot 0, FAT32, and found a Pip-OS file on it.
- `ev=rom rc=ok` and `ev=jump entry=0x00400000`: the ROM passed the check and the kernel entered it.
- The `deitrix:` lines: the ROM read its files from the card.

Then 19 keys went to the PL011 as ANSI sequences, about 1.5 s apart. Each key gave `ev=key type=key src=serial code=<key> value=1`, then `value=0`, then an answer from Deitrix. Deitrix changed tabs, moved rows, opened and closed an item, picked two radio stations and set the volume to 70, then 60. Its system page showed `ABI=1.2 | SoC=BCM2835 | RAM=448 MiB` and `Time=no clock`, and its uptime moved from `0d 00:01:01` to `0d 00:01:09`. Deitrix logged `save DEITRIX.SAV ok` five times. No `ev=exception` line and no `deitrix: radio underrun` line appeared. The capture ended about 15 s after the second station started.

The card afterwards, read on a computer:

- `PIPBOY/SAVE/` exists. The copied files did not hold it, so the kernel made it.
- `PIPBOY/SAVE/DEITRIX.SAV` is 20 bytes: `44 58 53 54 01 00 01 00 d0 6b 77 b0 04 3c 01 04 00 00 00 00`. In Deitrix's format, that is the `DXST` header, version 1, one record and a CRC, then last tab 4, volume 60, UI sound on and station 4. It matches the last keys.
- No `~DEITRIX.SAV` or `#DEITRIX.SAV` work file is left. There is no `INV.SAV`, as the inventory did not change.
- The folder and the file are dated 1 January 2026, as no clock chip was fitted.

What this run confirms, on this board at `2d10c2c` only:

- The kernel boots from the Pi firmware, turns the MMU on and starts the heap and the tick.
- The serial console prints the boot log and the markers.
- The input backends start with no error. Serial keys reach the ROM as press and release events.
- The PWM audio registers answer the probe.
- The SD driver reads the card, mounts the FAT32 boot volume and reads `PIPOS.CFG`.
- The ROM check passes on a real ROM and the kernel runs it in user mode. The ROM reads files, takes input, reads the system information and writes saves through system calls. `ev=syscalls` lists the calls the kernel serves, one bit per call.
- `write_save` creates `PIPBOY/SAVE/` and replaces a save five times with no work file left.

What it does not show:

- Any picture, on HDMI or a panel. No screen was connected.
- Any sound. No speaker was connected. `backend=pwm`, `probe=ok` and "Playing" show only that the calls did not fail.
- GPIO buttons or the dial. `gpio=0` shows the backend started with nothing wired.
- The RTC chip.
- A holotape run, and a power cut during a save.
- Any other board, or any other commit. Pip-OS `master` has the SPI panel backend and later work that did not run here.

`64K RAM SYSTEM` and `38911 BYTES FREE` are presentation text, see [Lore numbers](BOOT.md#lore-numbers).

### Zero W 1.1 run 2, 2026-11-24

The same board, card, files, firmware, serial setup and commits as [run 1](#zero-w-11-run-2026-11-24): Pip-OS `2d10c2c`, RobCo-Core `f5bba36`, Deitrix `ba21d5a`. This was the second power-on of the card. Between the runs, a computer only read `PIPBOY/SAVE/DEITRIX.SAV`.

The kernel lines of the boot match run 1. The ROM's lines after `ev=jump`, exact:

```
deitrix: load PROFILE.INI gauges=4 attrs=6 about=3
deitrix: load ITEMS.INI items=14 categories=4
deitrix: load NOTES notes=4
deitrix: load MAP.INI maps=2 lines=16 places=12
deitrix: load RADIO.INI stations=5
deitrix: load RADIO/MORNING.TUN notes=49 ms=28800
deitrix: load RADIO/WALK.TUN notes=56 ms=14530
deitrix: load RADIO/LULLABY.TUN notes=19 ms=19994
deitrix: load RADIO/HEARTH.PCM bytes=256000
deitrix: load RADIO/DRIFT.PCM bytes=256000
deitrix: load DEITRIX.SAV ok tab=4 volume=60 sound=1 station=4
deitrix: load INV.SAV missing
deitrix: ready
```

Then the keys went to the PL011 as in run 1. LEFT five times went through `tab=MAP`, `tab=DATA`, `tab=INV`, `tab=STAT` and back to `tab=RADIO`. Then the holotape, exact:

```
@RC stage=pipos ev=key type=key src=serial code=left value=1
@RC stage=pipos ev=key type=key src=serial code=left value=0
deitrix: tab=MAP
@RC stage=pipos ev=key type=key src=serial code=left value=1
@RC stage=pipos ev=key type=key src=serial code=left value=0
deitrix: data notes sel=0 top=0 count=4
deitrix: tab=DATA
@RC stage=pipos ev=key type=key src=serial code=right value=1
@RC stage=pipos ev=key type=key src=serial code=right value=0
deitrix: data tapes sel=0 top=0 count=1 | 
deitrix: save DEITRIX.SAV ok
@RC stage=pipos ev=key type=key src=serial code=select value=1
@RC stage=pipos ev=key type=key src=serial code=select value=0
deitrix: data launch CULVERT.HTP
@RC stage=pipos ev=holo_start name=CULVERT.HTP
culvert: load rc=-2
culvert: title
@RC stage=pipos ev=key type=key src=serial code=select value=1
@RC stage=pipos ev=key type=key src=serial code=select value=0
culvert: level n=1 w=5 h=4 seed=0x128f528f
@RC stage=pipos ev=key type=key src=serial code=right value=1
@RC stage=pipos ev=key type=key src=serial code=right value=0
culvert: cursor x=3 y=2
@RC stage=pipos ev=key type=key src=serial code=select value=1
@RC stage=pipos ev=key type=key src=serial code=select value=0
culvert: turn x=3 y=2 connected=7
@RC stage=pipos ev=key type=key src=serial code=down value=1
@RC stage=pipos ev=key type=key src=serial code=down value=0
culvert: cursor x=3 y=3
@RC stage=pipos ev=key type=key src=serial code=select value=1
@RC stage=pipos ev=key type=key src=serial code=select value=0
culvert: turn x=3 y=3 connected=7
@RC stage=pipos ev=key type=key src=serial code=select value=1
@RC stage=pipos ev=key type=key src=serial code=select value=0
culvert: turn x=3 y=3 connected=7
@RC stage=pipos ev=key type=key src=serial code=left value=1
@RC stage=pipos ev=key type=key src=serial code=left value=0
culvert: cursor x=2 y=3
@RC stage=pipos ev=key type=key src=serial code=select value=1
@RC stage=pipos ev=key type=key src=serial code=select value=0
culvert: turn x=2 y=3 connected=6
@RC stage=pipos ev=key type=key src=serial code=back value=1
@RC stage=pipos ev=key type=key src=serial code=back value=0
culvert: pause
@RC stage=pipos ev=key type=key src=serial code=down value=1
@RC stage=pipos ev=key type=key src=serial code=down value=0
culvert: menu item=1
@RC stage=pipos ev=key type=key src=serial code=down value=1
@RC stage=pipos ev=key type=key src=serial code=down value=0
culvert: menu item=2
@RC stage=pipos ev=key type=key src=serial code=select value=1
@RC stage=pipos ev=key type=key src=serial code=select value=0
culvert stats: grid draws=8 ms=70
culvert: exit rc=0
@RC stage=pipos ev=holo_exit status=0
deitrix: data tape rc=0
deitrix: data tapes sel=0 top=0 count=1 | Tape ended, status 0.
@RC stage=pipos ev=key type=key src=serial code=left value=1
@RC stage=pipos ev=key type=key src=serial code=left value=0
deitrix: data notes sel=0 top=0 count=4
```

Key lines:

- `deitrix: load DEITRIX.SAV ok tab=4 volume=60 sound=1 station=4`: the ROM read back the save run 1 wrote, through `read_save`, call `0x50`.
- `ev=holo_start name=CULVERT.HTP`: the ROM called `holo_launch` and the kernel entered the holotape.
- The `culvert:` lines between them: the holotape ran, took serial keys and logged.
- `ev=holo_exit status=0`: the holotape called `exit` or returned, with exit status 0.
- `deitrix: data tape rc=0` and the last LEFT: `holo_launch` returned 0 to the ROM, and the ROM kept taking keys.

No `ev=exception`, `ev=panic`, `ev=rom_kill` or `ev=holo_kill` line appeared.

What this run confirms, on this board at `2d10c2c` only:

- A save written in one boot is read back with `read_save` at the next.
- `holo_launch` loads a holotape from `PIPBOY/HOLO/` into the holotape slot at `0x00800000` and runs it in user mode inside the ROM's call, on the same `umode_run()` path as the ROM.
- A holotape gets serial key events and makes system calls.
- When the holotape exits, the kernel prints `ev=holo_exit`, `holo_launch` returns the exit status to the ROM, and the ROM goes on.

What it does not show:

- A fault or a `kill` in a holotape, so neither the kill path nor the sandbox's protection.
- A `write_save` by a holotape. No level was solved, so `CULVERT.SAV` was neither written nor read.
- Any picture or sound. Culvert's drawing calls ran, but nobody saw the result.
- GPIO buttons, the dial, the RTC chip, and any other board or commit.
- The card after this run. Nobody read it on a computer.

## CI

`.github/workflows/ci.yml` calls RobCo-Core's reusable workflow. It builds with `make CI=1 all test-host` in the RobCo CI container, uploads `build/pipos.img`, then runs `make CI=1 test-target BOARD=<board>` on each of the four boards.

A reusable workflow takes only literal inputs. So the workflow repeats two values from the submodule: the commit of the reusable workflow, which must be the pinned submodule commit, and the image tag, which must equal `external/robco-core/ci/IMAGE_TAG`. `make check-pins` fails when either differs. After you move the submodule, update both values in the workflow.

The CI job runs as root on a checkout owned by another user, so git refuses every command on the work tree. `make check-pins` runs RobCo-Core's `tools/check_pins.sh` on the workflow. It reads the submodule commit from `external/robco-core/.git` and its git directory instead. It uses git only as a cross-check when git works. `make check-pins-test` runs the check with a `git` that always fails.

`make check-pins` also runs RobCo-Core's `tools/check_doc_links.sh` through `make check-doc-links`. It fails when a Markdown file links to `github.com/RobCo-Industries/RobCo-Core/(blob|tree|commit)/<ref>` with a full hash other than the submodule's, or with a short hash. Branch and tag names pass. It skips `CHANGELOG.md`, `CLAUDE.md`, `AGENTS.md` and folders named `build`, `.venv` or `.git`. It reads the commit the same way, without git. `tests/host/check_doc_links_test.sh` proves the wiring on a copy with a stale link.
