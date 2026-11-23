/*
 * power: bad ops, then screen off and on around a drawn frame. Markers
 * "off" and "on" tell tests/test_time.py when to take a screenshot.
 */
#include "rt.h"

#define HOLD_MS 3000u

#define CK(name, expr, expected) rt_check_eq(name, (int32_t)(expr), (int32_t)(expected))

int pip_main(const pipos_startup_t *startup)
{
    (void)startup;
    CK("op4", pip_power(4), -PIP_EINVAL);
    CK("op_max", pip_power(0xffffffffu), -PIP_EINVAL);
    pip_clear(0x203040);
    pip_set_color(0xff8000, PIP_COLOR_NONE);
    pip_rect_xywh(40, 40, 120, 80, PIP_RECT_FILLED);
    pip_present();
    rt_begin("drawn");
    rt_end();
    pip_sleep_ms(HOLD_MS);
    CK("off", pip_power(2), 0);
    /* Drawing while blanked only reaches the back buffer. */
    pip_rect_xywh(200, 150, 40, 40, PIP_RECT_FILLED);
    pip_present();
    rt_begin("off");
    rt_end();
    pip_sleep_ms(HOLD_MS);
    CK("on", pip_power(3), 0);
    rt_checks_done();
    rt_begin("on");
    rt_end();
    for (;;) {
        pip_sleep_ms(1000);
    }
}
