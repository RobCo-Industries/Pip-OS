/*
 * Calls every number of the ABI table that the kernel does not serve,
 * with zero arguments, and checks that each returns -PIP_ENOSYS. The
 * table comes from PIP_SYSCALLS in abi.h. tests/test_syscalls.py writes
 * the served numbers into served_map before it boots this ROM.
 */
#include "rt.h"

/*
 * Bit n set: the kernel serves call n, so the ROM does not call it.
 * Unpatched, every call counts as served and the ROM calls none.
 */
const volatile struct {
    char magic[16] __attribute__((nonstring));
    uint32_t served[PIP_SYS_NR_LIMIT / 32];
} served_map = { "PIPOS-SERVED-MAP", { ~0u, ~0u, ~0u, ~0u } };

#define ROW(NAME, name) { PIP_SYS_##NAME, #name },
static const struct {
    uint32_t nr;
    const char *name;
} table[] = { PIP_SYSCALLS(ROW) };

int pip_main(const pipos_startup_t *startup)
{
    unsigned i;

    (void)startup;
    rt_begin("table");
    rt_dec("count", (int32_t)(sizeof(table) / sizeof(table[0])));
    rt_end();
    for (i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        uint32_t nr = table[i].nr;

        if (served_map.served[nr / 32] & (1u << (nr % 32))) {
            rt_begin("served");
            rt_hex("nr", nr);
            rt_end();
            continue;
        }
        rt_check_eq(table[i].name, pip_syscall(nr, 0, 0, 0, 0), -PIP_ENOSYS);
    }
    rt_checks_done();
    return 0;
}
