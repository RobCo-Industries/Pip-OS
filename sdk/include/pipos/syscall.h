/*
 * System call wrappers for ROM and holotape code, in libpip.a:
 * `svc #0`, number in r7, arguments in r0 to r3, result in r0.
 * A negative result is -PIP_E*. pip_syscall() is the one place that
 * makes the call; every wrapper goes through it.
 */
#ifndef PIPOS_SYSCALL_H
#define PIPOS_SYSCALL_H

#include <stdint.h>

#include <pipos/abi.h>

/* Any call by number. Unknown numbers return -PIP_ENOSYS. */
int32_t pip_syscall(uint32_t nr, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3);

/* Milliseconds since boot, 0 to 2^31 - 1, then wraps to 0. */
int32_t pip_ticks_ms(void);

/* Waits at least ms milliseconds, then returns 0. ms above 0x7FFFFFFF returns -PIP_ERANGE at once. */
int32_t pip_sleep_ms(uint32_t ms);

/* Ends the image with status. Does not return. */
__attribute__((noreturn)) void pip_exit(int32_t status);

/* Fill *out. 0, or -PIP_EFAULT for a bad pointer. */
int32_t pip_sys_info(pip_sys_info_t *out);
int32_t pip_mem_info(pip_mem_info_t *out);

/* Prints text on the serial console, at most 255 bytes. No newline is added. */
int32_t pip_log(const char *text);

/* 0 to 2^31 - 1, or -PIP_EIO when the generator fails. */
int32_t pip_rand(void);

#endif
