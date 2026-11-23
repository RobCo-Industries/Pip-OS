/*
 * The clock seeded from the RTC chip: get_time works at once, then
 * set_time overrides it. tests/test_time.py compares "ev=rtc" with the
 * time QEMU gave the chip.
 */
#include "rt.h"

#define T0 1234567890u

#define CK(name, expr, expected) rt_check_eq(name, (int32_t)(expr), (int32_t)(expected))

int pip_main(const pipos_startup_t *startup)
{
    uint32_t v = 0, r = 0;
    pip_sys_info_t si;

    (void)startup;
    CK("hw_rtc", pip_sys_info(&si) == 0 && (si.hw_flags & PIP_HW_RTC) != 0, 1);
    CK("avail0", pip_sensor_available(0), 1);
    CK("get", pip_get_time(&v), 0);
    CK("read0", pip_read_sensor(0, &r), 0);
    rt_begin("rtc");
    rt_dec("time", (int32_t)v);
    rt_end();
    CK("read0_agrees", r >= v && r - v <= 1, 1);
    CK("set", pip_set_time(T0), 0);
    CK("get_set", pip_get_time(&v), 0);
    CK("set_value", v - T0 <= 1, 1);
    rt_checks_done();
    return 0;
}
