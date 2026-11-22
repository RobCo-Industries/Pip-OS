/* The software clock and the time group's decision tables. See time_clock.h. */
#include "time_clock.h"

void pipos_clock_set(pipos_clock_t *c, uint32_t seconds, uint64_t now_us)
{
    c->set = 1;
    c->base = seconds;
    c->base_us = now_us;
}

int32_t pipos_clock_get(const pipos_clock_t *c, uint64_t now_us, uint32_t *out)
{
    uint64_t elapsed;

    if (!c->set) {
        return -PIP_ENODEV;
    }
    elapsed = (now_us - c->base_us) / 1000000u;
    if (elapsed > (uint64_t)(0xffffffffu - c->base)) {
        *out = 0xffffffffu;
    } else {
        *out = c->base + (uint32_t)elapsed;
    }
    return 0;
}

int32_t pipos_time_get(const pipos_sys_ctx_t *ctx, const pipos_clock_t *c, uint64_t now_us,
                       uintptr_t out)
{
    uint32_t v;
    int32_t rc = pipos_user_check_out(ctx, out, sizeof(v));

    if (rc != 0) {
        return rc;
    }
    rc = pipos_clock_get(c, now_us, &v);
    if (rc != 0) {
        return rc;
    }
    return pipos_user_copy_out(ctx, out, &v, sizeof(v));
}

int32_t pipos_time_read_sensor(const pipos_sys_ctx_t *ctx, const pipos_clock_t *c,
                               uint64_t now_us, uint32_t sensor, uintptr_t out)
{
    int32_t rc;

    if (sensor > PIPOS_SENSOR_MAX) {
        return -PIP_EINVAL;
    }
    rc = pipos_user_check_out(ctx, out, sizeof(uint32_t));
    if (rc != 0) {
        return rc;
    }
    if (sensor != PIPOS_SENSOR_RTC) {
        return -PIP_ENODEV;
    }
    return pipos_time_get(ctx, c, now_us, out);
}

int32_t pipos_time_sensor_available(const pipos_clock_t *c, uint32_t sensor)
{
    if (sensor > PIPOS_SENSOR_MAX) {
        return -PIP_EINVAL;
    }
    return sensor == PIPOS_SENSOR_RTC && c->set ? 1 : 0;
}

int32_t pipos_time_set(const pipos_sys_ctx_t *ctx, pipos_clock_t *c, uint64_t now_us,
                       uint32_t seconds)
{
    int32_t rc = pipos_sys_rom_only(ctx);

    if (rc != 0) {
        return rc;
    }
    pipos_clock_set(c, seconds, now_us);
    return 0;
}

int32_t pipos_power_action(const pipos_sys_ctx_t *ctx, uint32_t op)
{
    int32_t rc = pipos_sys_rom_only(ctx);

    if (rc != 0) {
        return rc;
    }
    switch (op) {
    case 0:
        return PIPOS_POWER_REBOOT;
    case 1:
        return PIPOS_POWER_HALT;
    case 2:
        return PIPOS_POWER_SCREEN_OFF;
    case 3:
        return PIPOS_POWER_SCREEN_ON;
    default:
        return -PIP_EINVAL;
    }
}
