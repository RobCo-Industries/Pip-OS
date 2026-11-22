/*
 * System call wrappers for ROM and holotape code: `svc #0`, number in
 * r7, arguments in r0 to r3, result in r0. ARM state only.
 */
#ifndef PIPOS_SYSCALL_H
#define PIPOS_SYSCALL_H

#include <stdint.h>

#include <pipos/abi.h>

static inline int32_t pip_syscall(uint32_t nr, uint32_t a0, uint32_t a1, uint32_t a2,
                                  uint32_t a3)
{
    register uint32_t r0 __asm__("r0") = a0;
    register uint32_t r1 __asm__("r1") = a1;
    register uint32_t r2 __asm__("r2") = a2;
    register uint32_t r3 __asm__("r3") = a3;
    register uint32_t r7 __asm__("r7") = nr;

    __asm__ volatile("svc #0"
                     : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3)
                     : "r"(r7)
                     : "r12", "cc", "memory");
    return (int32_t)r0;
}

/* Milliseconds since boot, 0 to 2^31 - 1, then wraps to 0. */
static inline int32_t pip_ticks_ms(void)
{
    return pip_syscall(PIP_SYS_TICKS_MS, 0, 0, 0, 0);
}

static inline int32_t pip_sleep_ms(uint32_t ms)
{
    return pip_syscall(PIP_SYS_SLEEP_MS, ms, 0, 0, 0);
}

static inline __attribute__((noreturn)) void pip_exit(int32_t status)
{
    pip_syscall(PIP_SYS_EXIT, (uint32_t)status, 0, 0, 0);
    for (;;) {
    }
}

static inline int32_t pip_sys_info(pip_sys_info_t *out)
{
    return pip_syscall(PIP_SYS_SYS_INFO, (uint32_t)(uintptr_t)out, 0, 0, 0);
}

static inline int32_t pip_mem_info(pip_mem_info_t *out)
{
    return pip_syscall(PIP_SYS_MEM_INFO, (uint32_t)(uintptr_t)out, 0, 0, 0);
}

/* Prints text on the serial console. No newline is added. */
static inline int32_t pip_log(const char *text)
{
    return pip_syscall(PIP_SYS_LOG, (uint32_t)(uintptr_t)text, 0, 0, 0);
}

/* 0 to 2^31 - 1, or -PIP_EIO when the generator fails. */
static inline int32_t pip_rand(void)
{
    return pip_syscall(PIP_SYS_RAND, 0, 0, 0, 0);
}

#endif
