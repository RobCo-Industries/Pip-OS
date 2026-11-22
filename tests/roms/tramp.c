/* Returns through the lr it got at entry, the exit trampoline, with r0 = 9. */
#include "rt.h"

int pip_main(const pipos_startup_t *startup)
{
    void (*trampoline)(int) = (void (*)(int))(uintptr_t)pip_entry_regs[PIP_ENTRY_LR];

    (void)startup;
    trampoline(9);
    pip_log("trampoline came back\n");
    return 1;
}
