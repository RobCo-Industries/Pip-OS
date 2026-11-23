/*
 * The time, sensors, battery and power group: read_sensor (0x30),
 * sensor_available (0x31), get_time (0x40), get_battery (0x41),
 * power (0x47) and set_time (0x4A). The clock and the decision tables
 * are in time_clock.c. See docs/API.md.
 */
#include <robco/audio.h>
#include <robco/boot.h>
#include <robco/console.h>
#include <robco/datetime.h>
#include <robco/klibc.h>
#include <robco/power.h>
#include <robco/rtc.h>
#include <robco/timer.h>
#include <robco/uart.h>

#include "pipos.h"
#include "sys.h"
#include "time_clock.h"

static pipos_clock_t clock;

static int32_t sys_read_sensor(const pipos_sys_ctx_t *ctx, uint32_t sensor, uint32_t out,
                               uint32_t a2, uint32_t a3)
{
    (void)a2, (void)a3;
    return pipos_time_read_sensor(ctx, &clock, timer_now_us(), sensor, out);
}

static int32_t sys_sensor_available(const pipos_sys_ctx_t *ctx, uint32_t sensor, uint32_t a1,
                                    uint32_t a2, uint32_t a3)
{
    (void)ctx, (void)a1, (void)a2, (void)a3;
    return pipos_time_sensor_available(&clock, sensor);
}

static int32_t sys_get_time(const pipos_sys_ctx_t *ctx, uint32_t out, uint32_t a1, uint32_t a2,
                            uint32_t a3)
{
    (void)a1, (void)a2, (void)a3;
    return pipos_time_get(ctx, &clock, timer_now_us(), out);
}

static int32_t sys_get_battery(const pipos_sys_ctx_t *ctx, uint32_t a0, uint32_t a1, uint32_t a2,
                               uint32_t a3)
{
    (void)ctx, (void)a0, (void)a1, (void)a2, (void)a3;
    return -PIP_ENODEV;
}

/* Stops for good: IRQs and FIQs masked, the CPU in wfi. */
static void __attribute__((noreturn)) halt(void)
{
    k_printf("@RC stage=" PIPOS_STAGE " ev=halt\n");
    audio_stop();
    /* Redraws the console over whatever the ROM showed, then one line. */
    console_set_scheme(console_scheme());
    console_puts("\nPIP-OS HALTED. SAFE TO POWER OFF.\n");
    uart_flush();
    robco_halt();
}

static int32_t sys_power(const pipos_sys_ctx_t *ctx, uint32_t op, uint32_t a1, uint32_t a2,
                         uint32_t a3)
{
    int32_t action = pipos_power_action(ctx, op);

    (void)a1, (void)a2, (void)a3;
    switch (action) {
    case PIPOS_POWER_REBOOT:
        k_printf("@RC stage=" PIPOS_STAGE " ev=reboot\n");
        uart_flush();
        power_reboot();
    case PIPOS_POWER_HALT:
        halt();
    case PIPOS_POWER_SCREEN_OFF:
        pipos_gfx_blank(1);
        return 0;
    case PIPOS_POWER_SCREEN_ON:
        pipos_gfx_blank(0);
        return 0;
    default:
        return action;
    }
}

static int32_t sys_set_time(const pipos_sys_ctx_t *ctx, uint32_t seconds, uint32_t a1,
                            uint32_t a2, uint32_t a3)
{
    (void)a1, (void)a2, (void)a3;
    return pipos_time_set(ctx, &clock, timer_now_us(), seconds);
}

static const pipos_sys_entry_t time_calls[] = {
    PIPOS_SYS_ENTRY(TIME, READ_SENSOR, sys_read_sensor),
    PIPOS_SYS_ENTRY(TIME, SENSOR_AVAILABLE, sys_sensor_available),
    PIPOS_SYS_ENTRY(TIME, GET_TIME, sys_get_time),
    PIPOS_SYS_ENTRY(TIME, GET_BATTERY, sys_get_battery),
    PIPOS_SYS_ENTRY(TIME, POWER, sys_power),
    PIPOS_SYS_ENTRY(TIME, SET_TIME, sys_set_time),
};

const pipos_sys_table_t pipos_time_table = PIPOS_SYS_TABLE(TIME, "time", time_calls);

/*
 * start_clock() in main.c probed the chip; rtc_init() seeded RobCo-Core's
 * clock from it when it holds a valid time. Take that time once.
 */
void pipos_time_init(void)
{
    datetime_t now;
    uint32_t s;

    if (clock_get(&now) == 0 && dt_to_seconds(&now, &s) == 0) {
        pipos_clock_set(&clock, s + PIPOS_UNIX_2000, timer_now_us());
    }
}

void pipos_time_image_start(int kind)
{
    (void)kind;
}

void pipos_time_image_end(int kind)
{
    (void)kind;
}

void pipos_time_poll(void)
{
}
