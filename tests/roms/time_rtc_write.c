/*
 * set_time writes the clock chip. Step 1 sets T1, which the chip holds.
 * Step 2 sets a time before 2000: the clock takes it, the chip does not.
 * After each step the ROM waits so that tests/test_time.py can read the
 * chip with the "rtc" script command.
 */
#include "rt.h"

#define T1   1900000000u    /* 2030-03-17T17:46:40Z */
#define OLD  5u             /* 1970: outside the chip's range */
#define WAIT 5000u

#define CK(name, expr, expected) rt_check_eq(name, (int32_t)(expr), (int32_t)(expected))

int pip_main(const pipos_startup_t *startup)
{
    uint32_t v = 0;

    (void)startup;
    CK("set_t1", pip_set_time(T1), 0);
    CK("get_t1", pip_get_time(&v), 0);
    CK("t1_value", v - T1 <= 1u, 1);
    rt_begin("step1");
    rt_end();
    pip_sleep_ms(WAIT);
    CK("set_old", pip_set_time(OLD), 0);
    CK("get_old", pip_get_time(&v), 0);
    CK("old_value", v - OLD <= WAIT / 1000u, 1);
    rt_begin("step2");
    rt_end();
    pip_sleep_ms(WAIT);
    rt_checks_done();
    return 0;
}
