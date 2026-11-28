/*
 * Blanks the screen with power, prints "ev=dark", then waits for a key
 * press. UP ends the ROM with exit status 7. Any other key runs an
 * undefined instruction. For tests/test_boot_scroll.py.
 */
#include "rt.h"

int pip_main(const pipos_startup_t *startup)
{
    pip_event_t ev;

    (void)startup;
    rt_check_eq("off", pip_power(PIP_POWER_SCREEN_OFF), 0);
    rt_checks_done();
    rt_begin("dark");
    rt_end();
    while (pip_wait_event(&ev, PIP_WAIT_FOREVER) != 1 || ev.type != PIP_EV_BUTTON ||
           ev.value != PIP_EV_PRESS) {
    }
    if (ev.code == PIP_BTN_UP) {
        return 7;
    }
    __asm__ volatile(".word 0xe7f000f0");
    return 1;
}
