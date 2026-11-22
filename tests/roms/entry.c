/* Reports the registers at entry and every field of the startup block. */
#include "rt.h"

int pip_main(const pipos_startup_t *s)
{
    unsigned i;
    int args_zero = 1;

    rt_begin("entry");
    rt_hex("r0", pip_entry_regs[0]);
    rt_hex("r1", pip_entry_regs[1]);
    rt_hex("r2", pip_entry_regs[2]);
    rt_hex("r3", pip_entry_regs[3]);
    rt_hex("sp", pip_entry_regs[PIP_ENTRY_SP]);
    rt_hex("lr", pip_entry_regs[PIP_ENTRY_LR]);
    rt_hex("main_arg", (uint32_t)(uintptr_t)s);
    rt_end();

    for (i = 0; i < sizeof(s->args); i++) {
        if (s->args[i] != 0) {
            args_zero = 0;
        }
    }
    rt_begin("startup");
    rt_hex("abi_version", s->abi_version);
    rt_hex("size", s->size);
    rt_hex("heap_base", s->heap_base);
    rt_hex("heap_size", s->heap_size);
    rt_hex("stack_top", s->stack_top);
    rt_hex("kind", s->kind);
    rt_hex("flags", s->flags);
    rt_dec("args_zero", args_zero);
    rt_end();
    return 0;
}
