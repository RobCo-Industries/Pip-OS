/*
 * The time, sensor and battery calls with no RTC: the clock starts unset,
 * set_time sets it, it advances with the timer, the sensor table, and bad
 * pointers. tests/test_time.py lists the check names in order.
 */
#include "rt.h"

#define KERNEL_ADDR 0x00008000u
#define T0          1700000000u
#define CANARY      0xa5a5a5a5u

#define CK(name, expr, expected) rt_check_eq(name, (int32_t)(expr), (int32_t)(expected))

static uint32_t *at(uint32_t addr)
{
    return (uint32_t *)(uintptr_t)addr;
}

static uint32_t periph(void)
{
    pip_sys_info_t si;

    pip_sys_info(&si);
    return (si.soc == 0x2835 ? 0x20000000u : 0x3F000000u) + 0x3004u;
}

/* Every bad pointer for read_sensor(sensor) or get_time (sensor < 0). */
static void bad_pointers(const char *k, const char *m, const char *s, int sensor)
{
    volatile uint32_t *top = at(PIP_ROM_STACK_TOP - 4);
    uint32_t *straddle = (uint32_t *)(uintptr_t)(PIP_ROM_STACK_TOP - 2);
    uint32_t keep = *top;
    int32_t rc;

    CK(k, sensor < 0 ? pip_get_time(at(KERNEL_ADDR)) : pip_read_sensor(sensor, at(KERNEL_ADDR)),
       -PIP_EFAULT);
    CK(m, sensor < 0 ? pip_get_time(at(periph())) : pip_read_sensor(sensor, at(periph())),
       -PIP_EFAULT);
    *top = CANARY;
    rc = sensor < 0 ? pip_get_time(straddle) : pip_read_sensor(sensor, straddle);
    CK(s, rc == -PIP_EFAULT && *top == CANARY, 1);
    *top = keep;
}

int pip_main(const pipos_startup_t *startup)
{
    uint32_t v = CANARY, t = 0, u = 0;
    uint32_t s;
    int ok;
    pip_sys_info_t si;

    (void)startup;
    CK("hw_no_rtc", pip_sys_info(&si) == 0 && (si.hw_flags & PIP_HW_RTC) == 0, 1);
    /* Not set. */
    CK("unset_get", pip_get_time(&v), -PIP_ENODEV);
    CK("unset_intact", v == CANARY, 1);
    CK("unset_avail0", pip_sensor_available(0), 0);
    CK("unset_read0", pip_read_sensor(0, &v), -PIP_ENODEV);
    CK("unset_read0_intact", v == CANARY, 1);
    bad_pointers("unset_get_kernel", "unset_get_mmio", "unset_get_straddle", -1);

    /* Set, then one second later. */
    CK("set", pip_set_time(T0), 0);
    CK("get", pip_get_time(&t), 0);
    CK("get_value", t - T0 <= 1, 1);
    pip_sleep_ms(1100);
    CK("get2", pip_get_time(&u), 0);
    rt_begin("tick");
    rt_hex("t", t);
    rt_hex("u", u);
    rt_end();
    CK("advanced", u - T0 >= 1 && u - T0 <= 2, 1);
    CK("avail0", pip_sensor_available(0), 1);
    v = 0;
    CK("read0", pip_read_sensor(0, &v), 0);
    CK("read0_agrees", v >= u && v - u <= 1, 1);
    bad_pointers("get_kernel", "get_mmio", "get_straddle", -1);
    bad_pointers("read0_kernel", "read0_mmio", "read0_straddle", 0);

    /* Sensors 1 to 6 do not exist; 7 is out of range. */
    ok = 1;
    for (s = 1; s <= 6; s++) {
        v = CANARY;
        ok &= pip_read_sensor(s, &v) == -PIP_ENODEV && v == CANARY;
        ok &= pip_sensor_available(s) == 0;
    }
    CK("sensors_1_6", ok, 1);
    bad_pointers("read6_kernel", "read6_mmio", "read6_straddle", 6);
    CK("read7", pip_read_sensor(7, &v), -PIP_EINVAL);
    CK("read7_badptr", pip_read_sensor(7, at(KERNEL_ADDR)), -PIP_EINVAL);
    CK("avail7", pip_sensor_available(7), -PIP_EINVAL);
    CK("read_max", pip_read_sensor(0xffffffffu, &v), -PIP_EINVAL);
    CK("avail_max", pip_sensor_available(0xffffffffu), -PIP_EINVAL);
    CK("battery", pip_get_battery(), -PIP_ENODEV);

    /* Set again: backwards, then the top value. */
    CK("set_back", pip_set_time(5), 0);
    CK("get_back", pip_get_time(&v), 0);
    CK("back_value", v <= 6, 1);
    CK("set_max", pip_set_time(0xffffffffu), 0);
    pip_sleep_ms(1100);
    CK("get_max", pip_get_time(&v), 0);
    CK("max_clamped", v, 0xffffffffu);
    CK("set_zero", pip_set_time(0), 0);
    CK("get_zero", pip_get_time(&v), 0);
    CK("zero_value", v <= 1, 1);
    rt_checks_done();
    return 0;
}
