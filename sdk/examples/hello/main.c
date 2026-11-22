/* Prints a greeting, the system and memory info, sleeps, and exits 0. */
#include <pipos/pip.h>

int pip_main(const pipos_startup_t *startup)
{
    pip_sys_info_t si;
    pip_mem_info_t mi;
    char *buf;
    int32_t t0;

    pip_printf("hello: Pip-OS ROM running, ABI 0x%08x\n", (unsigned)startup->abi_version);

    if (pip_sys_info(&si) == 0) {
        pip_printf("hello: Pip-OS %u.%u.%u, soc 0x%x, %u MiB RAM\n",
                   (unsigned)(si.pipos_version >> 16), (unsigned)((si.pipos_version >> 8) & 0xff),
                   (unsigned)(si.pipos_version & 0xff), (unsigned)si.soc,
                   (unsigned)(si.mem_size >> 20));
    }
    if (pip_mem_info(&mi) == 0) {
        pip_printf("hello: heap 0x%08x, %u KiB, stack top 0x%08x\n", (unsigned)mi.heap_base,
                   (unsigned)(mi.heap_size >> 10), (unsigned)mi.stack_top);
    }

    buf = pip_malloc(64);
    if (buf != NULL) {
        strlcpy(buf, "War. War never changes.", 64);
        pip_printf("hello: %s\n", buf);
        pip_free(buf);
    }

    t0 = pip_ticks_ms();
    pip_sleep_ms(500);
    pip_printf("hello: slept %d ms, bye\n", (int)(pip_ticks_ms() - t0));
    return 0;
}
