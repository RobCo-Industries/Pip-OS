/*
 * The pure part of the time group: the software clock in Unix seconds,
 * and the decision tables of read_sensor, sensor_available, set_time and
 * power. No hardware access; the caller passes the system timer reading.
 */
#ifndef PIPOS_TIME_CLOCK_H
#define PIPOS_TIME_CLOCK_H

#include <stddef.h>
#include <stdint.h>

#include "sys.h"

/* Sensors for read_sensor: 0 RTC, 1 battery ... 6 heart rate. */
#define PIPOS_SENSOR_RTC    0u
#define PIPOS_SENSOR_MAX    6u

/* Seconds from 1970-01-01 to 2000-01-01, the epoch of dt_to_seconds(). */
#define PIPOS_UNIX_2000     946684800u

/* The software clock: base seconds at base_us on the 1 MHz system timer. */
typedef struct pipos_clock {
    int set;
    uint32_t base;
    uint64_t base_us;
} pipos_clock_t;

/* Sets the clock to `seconds` at `now_us`. */
void pipos_clock_set(pipos_clock_t *c, uint32_t seconds, uint64_t now_us);

/*
 * The time at `now_us`: base plus whole seconds elapsed, clamped at
 * 0xFFFFFFFF. The elapsed time is now_us - base_us modulo 2^64, so it
 * survives the timer's wrap. 0, or -PIP_ENODEV when the clock is not set.
 */
int32_t pipos_clock_get(const pipos_clock_t *c, uint64_t now_us, uint32_t *out);

/* get_time: checks the pointer, then writes the time. 0, -PIP_EFAULT or -PIP_ENODEV. */
int32_t pipos_time_get(const pipos_sys_ctx_t *ctx, const pipos_clock_t *c, uint64_t now_us,
                       uintptr_t out);

/* read_sensor: -PIP_EINVAL above 6, then the pointer, then sensor 0 as get_time. */
int32_t pipos_time_read_sensor(const pipos_sys_ctx_t *ctx, const pipos_clock_t *c,
                               uint64_t now_us, uint32_t sensor, uintptr_t out);

/* sensor_available: 1 when read_sensor would succeed now, else 0; -PIP_EINVAL above 6. */
int32_t pipos_time_sensor_available(const pipos_clock_t *c, uint32_t sensor);

/* The Unix seconds a clock chip holds: years 2000 to 2099. */
#define PIPOS_RTC_FIRST     946684800u
#define PIPOS_RTC_LAST      4102444799u

/*
 * Writes `seconds` to the clock chip. Returns 0 or a negative error. The
 * caller passes it only when a chip answers.
 */
typedef int (*pipos_rtc_write_fn)(uint32_t seconds);

/*
 * set_time: -PIP_EPERM for a holotape. Else sets the clock, then writes
 * the chip through `rtc_write` when it is not NULL and `seconds` is in
 * PIPOS_RTC_FIRST to PIPOS_RTC_LAST. Returns -PIP_EIO when that write
 * fails, else 0. The clock keeps the new value either way.
 */
int32_t pipos_time_set(const pipos_sys_ctx_t *ctx, pipos_clock_t *c, uint64_t now_us,
                       uint32_t seconds, pipos_rtc_write_fn rtc_write);

/* What power(op) does. */
enum {
    PIPOS_POWER_REBOOT,
    PIPOS_POWER_HALT,
    PIPOS_POWER_SCREEN_OFF,
    PIPOS_POWER_SCREEN_ON,
};

/* PIPOS_POWER_*, or -PIP_EPERM for a holotape, or -PIP_EINVAL for op above 3. */
int32_t pipos_power_action(const pipos_sys_ctx_t *ctx, uint32_t op);

#endif
