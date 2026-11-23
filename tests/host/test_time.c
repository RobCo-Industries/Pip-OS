/*
 * kernel/time_clock.c: the software clock (unset, set, elapsed time,
 * clamp, timer wrap) and the read_sensor, sensor_available, set_time
 * and power tables, over a fake memory map.
 */
#include <stdint.h>
#include <string.h>

#include <pipos/abi.h>

#include "rc_test.h"
#include "time_clock.h"

#define S 1000000ull    /* one second in microseconds */

/* The caller owns mem[0..16) read-only and mem[16..32) read-write. */
static uint32_t mem[8];
#define RO ((uintptr_t)mem)
#define RW ((uintptr_t)mem + 16)
#define END ((uintptr_t)mem + 32)

static int fake_range_ok(uintptr_t p, size_t n, int write)
{
    uintptr_t lo = write ? RW : RO;

    return p >= lo && p <= END && n <= END - p;
}

static long fake_strnlen_ok(uintptr_t p, size_t max)
{
    (void)p, (void)max;
    return -1;
}

static const pipos_user_mem_t fake = { fake_range_ok, fake_strnlen_ok };
static const pipos_sys_ctx_t rom = { PIP_KIND_ROM, NULL, &fake };
static const pipos_sys_ctx_t holo = { PIP_KIND_HOLOTAPE, NULL, &fake };

#define CANARY 0xa5a5a5a5u

TEST(clock_not_set)
{
    pipos_clock_t c = { 0 };
    uint32_t v = CANARY;

    CHECK_EQ(pipos_clock_get(&c, 123 * S, &v), -PIP_ENODEV);
    CHECK_EQ(v, CANARY);
    mem[4] = CANARY;
    CHECK_EQ(pipos_time_get(&rom, &c, 0, RW), -PIP_ENODEV);
    CHECK_EQ(mem[4], CANARY);
}

TEST(clock_set_then_elapsed)
{
    pipos_clock_t c = { 0 };
    uint32_t v = 0;

    pipos_clock_set(&c, 1000, 5 * S + 300000);
    CHECK_EQ(pipos_clock_get(&c, 5 * S + 300000, &v), 0);
    CHECK_EQ(v, 1000);
    CHECK_EQ(pipos_clock_get(&c, 6 * S + 299999, &v), 0);
    CHECK_EQ(v, 1000);
    CHECK_EQ(pipos_clock_get(&c, 6 * S + 300000, &v), 0);
    CHECK_EQ(v, 1001);
    CHECK_EQ(pipos_clock_get(&c, 3605 * S + 300000, &v), 0);
    CHECK_EQ(v, 4600);
    mem[4] = 0;
    CHECK_EQ(pipos_time_get(&rom, &c, 15 * S + 300000, RW), 0);
    CHECK_EQ(mem[4], 1010);
}

TEST(clock_set_backwards_and_forwards)
{
    pipos_clock_t c = { 0 };
    uint32_t v = 0;

    pipos_clock_set(&c, 5000, 0);
    CHECK_EQ(pipos_clock_get(&c, 100 * S, &v), 0);
    CHECK_EQ(v, 5100);
    pipos_clock_set(&c, 10, 100 * S);
    CHECK_EQ(pipos_clock_get(&c, 100 * S, &v), 0);
    CHECK_EQ(v, 10);
    CHECK_EQ(pipos_clock_get(&c, 102 * S, &v), 0);
    CHECK_EQ(v, 12);
    pipos_clock_set(&c, 2000000000u, 102 * S);
    CHECK_EQ(pipos_clock_get(&c, 103 * S, &v), 0);
    CHECK_EQ(v, 2000000001u);
    pipos_clock_set(&c, 0, 103 * S);
    CHECK_EQ(pipos_clock_get(&c, 103 * S, &v), 0);
    CHECK_EQ(v, 0);
}

TEST(clock_clamps_at_top)
{
    pipos_clock_t c = { 0 };
    uint32_t v = 0;

    pipos_clock_set(&c, 0xfffffffdu, 0);
    CHECK_EQ(pipos_clock_get(&c, 1 * S, &v), 0);
    CHECK_EQ(v, 0xfffffffeu);
    CHECK_EQ(pipos_clock_get(&c, 2 * S, &v), 0);
    CHECK_EQ(v, 0xffffffffu);
    CHECK_EQ(pipos_clock_get(&c, 3 * S, &v), 0);
    CHECK_EQ(v, 0xffffffffu);
    CHECK_EQ(pipos_clock_get(&c, 0x100000000ull * S, &v), 0);
    CHECK_EQ(v, 0xffffffffu);
    pipos_clock_set(&c, 0xffffffffu, 0);
    CHECK_EQ(pipos_clock_get(&c, UINT64_MAX, &v), 0);
    CHECK_EQ(v, 0xffffffffu);
    /* From 0, a 2^32-second gap still clamps instead of wrapping. */
    pipos_clock_set(&c, 0, 0);
    CHECK_EQ(pipos_clock_get(&c, 0x100000000ull * S, &v), 0);
    CHECK_EQ(v, 0xffffffffu);
}

TEST(clock_across_timer_wrap)
{
    pipos_clock_t c = { 0 };
    uint32_t v = 0;

    pipos_clock_set(&c, 700, UINT64_MAX - S / 2);
    CHECK_EQ(pipos_clock_get(&c, UINT64_MAX, &v), 0);
    CHECK_EQ(v, 700);
    CHECK_EQ(pipos_clock_get(&c, S / 2, &v), 0);
    CHECK_EQ(v, 701);
    CHECK_EQ(pipos_clock_get(&c, 10 * S, &v), 0);
    CHECK_EQ(v, 710);
    /* The 32-bit low word wraps every 71 minutes; the 64-bit count goes on. */
    pipos_clock_set(&c, 100, 0xfffff000ull);
    CHECK_EQ(pipos_clock_get(&c, 0x100000000ull + 2 * S, &v), 0);
    CHECK_EQ(v, 102);
}

/* read_sensor and sensor_available for sensors 0 to 7, clock set and not. */
TEST(sensor_table)
{
    pipos_clock_t c = { 0 };
    uint32_t s;
    int set;

    for (set = 0; set < 2; set++) {
        if (set) {
            pipos_clock_set(&c, 42, 0);
        }
        for (s = 0; s <= 7; s++) {
            int32_t want_read = s > 6 ? -PIP_EINVAL : s == 0 && set ? 0 : -PIP_ENODEV;
            int32_t want_av = s > 6 ? -PIP_EINVAL : s == 0 && set ? 1 : 0;

            mem[4] = CANARY;
            CHECK_EQ(pipos_time_read_sensor(&rom, &c, 3 * S, s, RW), want_read);
            CHECK_EQ(mem[4], want_read == 0 ? 45u : CANARY);
            CHECK_EQ(pipos_time_sensor_available(&c, s), want_av);
            /* A bad pointer: EFAULT for 0 to 6, EINVAL above. */
            CHECK_EQ(pipos_time_read_sensor(&rom, &c, 3 * S, s, RO),
                     s > 6 ? -PIP_EINVAL : -PIP_EFAULT);
            CHECK_EQ(pipos_time_read_sensor(&rom, &c, 3 * S, s, END - 2),
                     s > 6 ? -PIP_EINVAL : -PIP_EFAULT);
        }
    }
    CHECK_EQ(pipos_time_read_sensor(&rom, &c, 0, 0xffffffffu, RW), -PIP_EINVAL);
    CHECK_EQ(pipos_time_sensor_available(&c, 0xffffffffu), -PIP_EINVAL);
}

TEST(get_time_bad_pointer)
{
    pipos_clock_t c = { 0 };

    CHECK_EQ(pipos_time_get(&rom, &c, 0, RO), -PIP_EFAULT);
    pipos_clock_set(&c, 9, 0);
    CHECK_EQ(pipos_time_get(&rom, &c, 0, RO), -PIP_EFAULT);
    CHECK_EQ(pipos_time_get(&rom, &c, 0, END - 3), -PIP_EFAULT);
    CHECK_EQ(pipos_time_get(&rom, &c, 0, END - 4), 0);
    CHECK_EQ(mem[7], 9u);
}

TEST(set_time_rom_and_holotape)
{
    pipos_clock_t c = { 0 };
    uint32_t v = 0;

    CHECK_EQ(pipos_time_set(&holo, &c, 0, 77, NULL), -PIP_EPERM);
    CHECK_EQ(c.set, 0);
    CHECK_EQ(pipos_time_set(&rom, &c, 0, 77, NULL), 0);
    CHECK_EQ(pipos_time_set(&holo, &c, 0, 5, NULL), -PIP_EPERM);
    CHECK_EQ(pipos_clock_get(&c, 0, &v), 0);
    CHECK_EQ(v, 77);
    CHECK_EQ(pipos_time_set(&rom, &c, 0, 0xffffffffu, NULL), 0);
    CHECK_EQ(pipos_clock_get(&c, 0, &v), 0);
    CHECK_EQ(v, 0xffffffffu);
}

/* A fake chip: counts writes, keeps the last value, returns `fake_rc`. */
static int fake_calls;
static uint32_t fake_value;
static int fake_rc;

static int fake_write(uint32_t seconds)
{
    fake_calls++;
    fake_value = seconds;
    return fake_rc;
}

static void fake_reset(int rc)
{
    fake_calls = 0;
    fake_value = 0;
    fake_rc = rc;
}

TEST(set_time_chip)
{
    pipos_clock_t c = { 0 };
    uint32_t v = 0;

    /* A chip that answers: written, 0. */
    fake_reset(0);
    CHECK_EQ(pipos_time_set(&rom, &c, 0, 1700000000u, fake_write), 0);
    CHECK_EQ(fake_calls, 1);
    CHECK_EQ(fake_value, 1700000000u);
    CHECK_EQ(pipos_clock_get(&c, 0, &v), 0);
    CHECK_EQ(v, 1700000000u);

    /* The range edges are written; one second outside is not. */
    fake_reset(0);
    CHECK_EQ(pipos_time_set(&rom, &c, 0, PIPOS_RTC_FIRST, fake_write), 0);
    CHECK_EQ(pipos_time_set(&rom, &c, 0, PIPOS_RTC_LAST, fake_write), 0);
    CHECK_EQ(fake_calls, 2);
    CHECK_EQ(fake_value, PIPOS_RTC_LAST);
    fake_reset(0);
    CHECK_EQ(pipos_time_set(&rom, &c, 0, PIPOS_RTC_FIRST - 1u, fake_write), 0);
    CHECK_EQ(pipos_clock_get(&c, 0, &v), 0);
    CHECK_EQ(v, PIPOS_RTC_FIRST - 1u);
    CHECK_EQ(pipos_time_set(&rom, &c, 0, PIPOS_RTC_LAST + 1u, fake_write), 0);
    CHECK_EQ(pipos_clock_get(&c, 0, &v), 0);
    CHECK_EQ(v, PIPOS_RTC_LAST + 1u);
    CHECK_EQ(pipos_time_set(&rom, &c, 0, 0, fake_write), 0);
    CHECK_EQ(pipos_time_set(&rom, &c, 0, 0xffffffffu, fake_write), 0);
    CHECK_EQ(fake_calls, 0);

    /* A failed write: -PIP_EIO, and the clock keeps the new value. */
    fake_reset(-5);
    CHECK_EQ(pipos_time_set(&rom, &c, 0, 1800000000u, fake_write), -PIP_EIO);
    CHECK_EQ(fake_calls, 1);
    CHECK_EQ(pipos_clock_get(&c, 0, &v), 0);
    CHECK_EQ(v, 1800000000u);
    /* Out of range with a failing chip: not written, so 0. */
    CHECK_EQ(pipos_time_set(&rom, &c, 0, 5, fake_write), 0);
    CHECK_EQ(fake_calls, 1);

    /* A holotape: -PIP_EPERM, nothing written, the clock unchanged. */
    fake_reset(0);
    CHECK_EQ(pipos_time_set(&holo, &c, 0, 1700000000u, fake_write), -PIP_EPERM);
    CHECK_EQ(fake_calls, 0);
    CHECK_EQ(pipos_clock_get(&c, 0, &v), 0);
    CHECK_EQ(v, 5u);
}

TEST(power_table)
{
    CHECK_EQ(pipos_power_action(&rom, 0), PIPOS_POWER_REBOOT);
    CHECK_EQ(pipos_power_action(&rom, 1), PIPOS_POWER_HALT);
    CHECK_EQ(pipos_power_action(&rom, 2), PIPOS_POWER_SCREEN_OFF);
    CHECK_EQ(pipos_power_action(&rom, 3), PIPOS_POWER_SCREEN_ON);
    CHECK_EQ(pipos_power_action(&rom, 4), -PIP_EINVAL);
    CHECK_EQ(pipos_power_action(&rom, 0xffffffffu), -PIP_EINVAL);
    CHECK_EQ(pipos_power_action(&holo, 0), -PIP_EPERM);
    CHECK_EQ(pipos_power_action(&holo, 1), -PIP_EPERM);
    CHECK_EQ(pipos_power_action(&holo, 2), -PIP_EPERM);
    CHECK_EQ(pipos_power_action(&holo, 3), -PIP_EPERM);
    CHECK_EQ(pipos_power_action(&holo, 4), -PIP_EPERM);
    CHECK_EQ(pipos_power_action(&holo, 0xffffffffu), -PIP_EPERM);
}
