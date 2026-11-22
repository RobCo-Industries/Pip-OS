/* Fills the screen red, presents, then exits with 3 (or faults with GFX_FAULT). */
#include "rt.h"

int pip_main(const pipos_startup_t *startup)
{
    (void)startup;
    pip_clear(0xff0000);
    pip_present();
    rt_begin("drawn");
    rt_end();
    pip_sleep_ms(200);
#ifdef GFX_FAULT
    *(volatile uint32_t *)(uintptr_t)0x00008000u = 1;
#endif
    return 3;
}
