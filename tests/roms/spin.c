/* Spins forever without a system call: the kernel cannot answer script commands. */
#include "rt.h"

int pip_main(const pipos_startup_t *startup)
{
    (void)startup;
    rt_begin("spinning");
    rt_end();
    for (;;) {
        __asm__ volatile("");
    }
}
