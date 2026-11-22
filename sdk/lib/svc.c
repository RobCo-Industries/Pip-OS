/* The only `svc #0` of libpip.a. Every wrapper in syscall.c calls it once. */
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
