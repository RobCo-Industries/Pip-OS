/*
 * Pure pieces of the ROM runner and the system calls: no hardware, also
 * built for the host tests.
 */
#ifndef PIPOS_SYSABI_H
#define PIPOS_SYSABI_H

#include <stddef.h>
#include <stdint.h>

#include <pipos/abi.h>

/* Where the kernel puts the startup block and the umode return stub. */
#define PIPOS_ROM_STARTUP   (PIP_ROM_STACK_TOP - PIP_STACK_SIZE)
#define PIPOS_ROM_STUB      (PIPOS_ROM_STARTUP + 128u)

/* (major << 16) | (minor << 8) | patch, each part masked to its width. */
uint32_t pipos_version_pack(unsigned major, unsigned minor, unsigned patch);

/* The startup block of a ROM. */
void pipos_startup_rom(pipos_startup_t *s);

/* mem_info and sys_info contents. */
void pipos_mem_info_rom(pip_mem_info_t *m);
void pipos_sys_info_fill(pip_sys_info_t *s, uint32_t pipos_version, uint32_t board_rev,
                         uint32_t soc, uint32_t mem_size);

/*
 * An image's exit status: the low 8 bits of the value it passed to exit
 * or returned from its entry function. Both ev=rom_exit and holo_launch use it.
 */
int32_t pipos_exit_status(int code);

/* The byte log prints for c: printable ASCII, '\n' and '\t' stay, the rest is '?'. */
char pipos_log_char(char c);

/*
 * A string's verdict from umode_strnlen_ok(ptr, size) and from
 * umode_range_ok(ptr, size, 0): the length, -PIP_EINVAL when the `size`
 * readable bytes hold no NUL, or -PIP_EFAULT. pipos_user_strlen() uses it.
 */
long pipos_str_check(long strnlen_result, int whole_range_ok);

/*
 * sleep_ms's verdict on its argument: 0, or -PIP_ERANGE when ms is above
 * 0x7FFFFFFF. A longer wait could never end: tick_ms() wraps at 2^32.
 */
int32_t pipos_sleep_check(uint32_t ms);

/*
 * When a sleep_ms of `ms` that starts at `now_us` on the 1 MHz system
 * timer may end: never before `ms` whole milliseconds passed.
 */
uint64_t pipos_sleep_deadline_us(uint64_t now_us, uint32_t ms);

/* rom_kill reason for a ROBCO_EXC_* fault type. */
const char *pipos_fault_reason(unsigned type);

/* "ROM(1) <reason> AT 0x<pc>" for the screen. Returns the k_snprintf length. */
int pipos_fault_line(unsigned type, uint32_t pc, char *buf, size_t size);

#endif
