/* System call wrappers. pip_syscall() holds the only `svc #0`. */
#include <pipos/syscall.h>

int32_t pip_syscall(uint32_t nr, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
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

int32_t pip_ticks_ms(void)
{
    return pip_syscall(PIP_SYS_TICKS_MS, 0, 0, 0, 0);
}

int32_t pip_sleep_ms(uint32_t ms)
{
    return pip_syscall(PIP_SYS_SLEEP_MS, ms, 0, 0, 0);
}

void pip_exit(int32_t status)
{
    pip_syscall(PIP_SYS_EXIT, (uint32_t)status, 0, 0, 0);
    for (;;) {
    }
}

int32_t pip_sys_info(pip_sys_info_t *out)
{
    return pip_syscall(PIP_SYS_SYS_INFO, (uint32_t)(uintptr_t)out, 0, 0, 0);
}

int32_t pip_mem_info(pip_mem_info_t *out)
{
    return pip_syscall(PIP_SYS_MEM_INFO, (uint32_t)(uintptr_t)out, 0, 0, 0);
}

int32_t pip_log(const char *text)
{
    return pip_syscall(PIP_SYS_LOG, (uint32_t)(uintptr_t)text, 0, 0, 0);
}

int32_t pip_rand(void)
{
    return pip_syscall(PIP_SYS_RAND, 0, 0, 0, 0);
}
