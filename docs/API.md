# System call interface

A running ROM calls the kernel with `svc #0`. The call number goes in r7, the arguments in r0 to r3, and the result comes back in r0. A negative result is `-PIP_E*`. The kernel keeps every register except r0. The contract is RobCo-Core's [PIPOS_ABI.md](https://github.com/RobCo-Industries/RobCo-Core/blob/1e0fb655ea09497d133c6e3c3b086a2bb8cbca0e/docs/spec/PIPOS_ABI.md).

[`sdk/include/pipos/abi.h`](../sdk/include/pipos/abi.h) holds the call numbers of the whole table, the error codes and the structs. [`sdk/include/pipos/syscall.h`](../sdk/include/pipos/syscall.h) declares a `pip_<name>()` wrapper for each working call, plus a raw `pip_syscall()`. They live in `libpip.a`. See [ROM_DEVELOPMENT.md](ROM_DEVELOPMENT.md).

## Calls that work today

| Number | Name | Arguments | Result |
|---|---|---|---|
| `0x42` | `ticks_ms` | none | Milliseconds since the tick started at boot, masked to 31 bits |
| `0x43` | `sleep_ms` | `uint32 ms`, at most `0x7FFFFFFF` | 0 once at least `ms` milliseconds passed on the 1 MHz system timer. It can run up to one 10 ms tick longer. IRQs stay on. A larger `ms` returns `-PIP_ERANGE` (-34) at once. |
| `0x44` | `exit` | `int32 status` | Does not return. The kernel prints `ev=rom_exit status=<status>`. |
| `0x45` | `sys_info` | `pip_sys_info_t *out` | 0 |
| `0x46` | `mem_info` | `pip_mem_info_t *out` | 0 |
| `0x48` | `log` | `const char *text` | 0. Prints the text on the PL011 with no newline added. |
| `0x49` | `rand` | none | 0 to 2^31 - 1 from the hardware generator, or `-PIP_EIO` if it fails |

Every other number returns `-PIP_ENOSYS` (-38), including the rest of the spec's table.

`sys_info` writes 32 bytes: `size` 32, `abi_version` `0x00010000`, `pipos_version` `(major << 16) | (minor << 8) | patch`, the firmware `board_rev`, `soc` as `0x2835`, `0x2836` or `0x2837`, `mem_size` in bytes, and two reserved words set to 0.

`mem_info` writes 16 bytes: `heap_base` `0x01000000`, `heap_size` `0x007F0000`, `stack_top` `0x01800000` and `stack_size` `0x00010000`.

`log` prints printable ASCII, `\n` and `\t` as they are, and every other byte as `?`. The text is at most 255 bytes plus the NUL.

## Pointer checks

The kernel checks every pointer against the ROM's two regions before it reads or writes. A bad pointer returns `-PIP_EFAULT` (-14) and the kernel writes nothing.

| Case | Result |
|---|---|
| An out struct not entirely inside the ROM slot or the heap and stack region | `-PIP_EFAULT` |
| `log` text that reaches a byte outside the regions before its NUL | `-PIP_EFAULT` |
| `log` text with no NUL in its first 256 bytes, all inside the regions | `-PIP_EINVAL` (-22) |

## Limits

- `sleep_ms` is the only call during which the kernel runs its own main-loop work. See [BOOT.md](BOOT.md#running-the-rom).
- A ROM can run forever. The kernel sets no time limit.
- Graphics, input, audio, time, sensors, saves, files, holotapes and `power` are not implemented.
