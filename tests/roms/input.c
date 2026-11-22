/*
 * The input calls, driven from tests/test_input_calls.py. The ROM prints
 * "ev=ph_<name>" when it is ready for the next injected input, and
 * "ev=event ..." for each event of the "seq" phase. Everything else is a
 * named check. Each phase waits for exactly the events the test injects
 * after its marker, so no event can land in another phase.
 */
#include <pipos/string.h>

#include "rt.h"

#define KERNEL_ADDR     0x00008000u
#define PERIPH_BCM2835  0x20000000u
#define PERIPH_BCM2836  0x3F000000u
#define SEQ_EVENTS      14
#define BUSY_MS         2000    /* the test's bytes reach the UARTs meanwhile */
#define HELD_MS         250     /* the test holds the event back 300 ms */

static void phase(const char *name)
{
    rt_begin(name);
    rt_end();
}

static int untouched(const pip_event_t *ev)
{
    const uint8_t *p = (const uint8_t *)ev;
    unsigned i;

    for (i = 0; i < sizeof(*ev); i++) {
        if (p[i] != 0x5a) {
            return 0;
        }
    }
    return 1;
}

static pip_event_t *fresh(pip_event_t *ev)
{
    memset(ev, 0x5a, sizeof(*ev));
    return ev;
}

static void start(void)
{
    pip_event_t ev;
    int32_t t0, t1, rc;

    rt_check_eq("dial_boot", pip_read_dial(), 30);
    rt_check_eq("buttons_boot", pip_read_buttons(), 0);
    rt_check_eq("poll_empty", pip_poll_event(fresh(&ev)), 0);
    rt_check("poll_empty_untouched", untouched(&ev));
    rt_check_eq("wait0_empty", pip_wait_event(fresh(&ev), 0), -PIP_ETIMEDOUT);
    rt_check("wait0_untouched", untouched(&ev));

    t0 = pip_ticks_ms();
    rc = pip_wait_event(fresh(&ev), 50);
    t1 = pip_ticks_ms();
    rt_check_eq("wait_short", rc, -PIP_ETIMEDOUT);
    rt_check("wait_short_untouched", untouched(&ev));
    rt_check("wait_short_not_short", t1 - t0 >= 50);
    rt_check("wait_short_not_long", t1 - t0 < 20000);

    rt_check_eq("wait_range_first", pip_wait_event(fresh(&ev), 0x80000000u), -PIP_ERANGE);
    rt_check_eq("wait_range_last", pip_wait_event(&ev, 0xFFFFFFFEu), -PIP_ERANGE);
    rt_check("wait_range_untouched", untouched(&ev));
    rt_check_eq("wait_range_before_fault",
                pip_wait_event((pip_event_t *)0, 0x80000000u), -PIP_ERANGE);
}

/* -PIP_EFAULT with nothing written, before any wait. */
static void faults(void)
{
    volatile uint8_t *top = (volatile uint8_t *)(uintptr_t)(PIP_ROM_STACK_TOP - 8);
    uint8_t keep[8];
    int32_t poll_rc, wait_rc;
    int i, intact = 1;

    rt_check_eq("poll_kernel", pip_poll_event((pip_event_t *)KERNEL_ADDR), -PIP_EFAULT);
    rt_check_eq("poll_null", pip_poll_event((pip_event_t *)0), -PIP_EFAULT);
    rt_check_eq("poll_mmio1", pip_poll_event((pip_event_t *)PERIPH_BCM2835), -PIP_EFAULT);
    rt_check_eq("poll_mmio2", pip_poll_event((pip_event_t *)PERIPH_BCM2836), -PIP_EFAULT);
    rt_check_eq("wait_kernel", pip_wait_event((pip_event_t *)KERNEL_ADDR, 0), -PIP_EFAULT);
    rt_check_eq("wait_null_forever", pip_wait_event((pip_event_t *)0, PIP_WAIT_FOREVER),
                -PIP_EFAULT);
    rt_check_eq("wait_mmio_timed", pip_wait_event((pip_event_t *)PERIPH_BCM2836, 5000),
                -PIP_EFAULT);

    /* The last 8 bytes of the stack: a 16-byte event crosses its end. */
    for (i = 0; i < 8; i++) {
        keep[i] = top[i];
        top[i] = 0x5a;
    }
    poll_rc = pip_poll_event((pip_event_t *)(uintptr_t)top);
    wait_rc = pip_wait_event((pip_event_t *)(uintptr_t)top, PIP_WAIT_FOREVER);
    for (i = 0; i < 8; i++) {
        if (top[i] != 0x5a) {
            intact = 0;
        }
        top[i] = keep[i];
    }
    rt_check_eq("poll_straddle", poll_rc, -PIP_EFAULT);
    rt_check_eq("wait_straddle", wait_rc, -PIP_EFAULT);
    rt_check("straddle_intact", intact);
}

/* Every event the test injects, logged as it comes, waiting forever for each. */
static void sequence(void)
{
    pip_event_t ev;
    int32_t rc;
    int i, all = 1;

    phase("ph_seq");
    for (i = 0; i < SEQ_EVENTS; i++) {
        rc = pip_wait_event(fresh(&ev), PIP_WAIT_FOREVER);
        if (rc != 1) {
            all = 0;
        }
        rt_begin("event");
        rt_dec("type", (int32_t)ev.type);
        rt_dec("code", (int32_t)ev.code);
        rt_dec("value", ev.value);
        rt_dec("time", (int32_t)ev.time_ms);
        rt_end();
    }
    rt_check("seq_rc", all);
    rt_check_eq("dial_seq", pip_read_dial(), 45);
}

static int32_t wait_one(pip_event_t *ev)
{
    return pip_wait_event(fresh(ev), PIP_WAIT_FOREVER);
}

static void hold(void)
{
    pip_event_t ev;

    phase("ph_hold");
    rt_check_eq("hold_rc", wait_one(&ev), 1);
    rt_check_eq("hold_value", ev.value, 1);
    rt_check_eq("buttons_held", pip_read_buttons(), (int32_t)PIP_BTN_SELECT_BIT);
    phase("ph_hold2");
    rt_check_eq("hold2_rc", wait_one(&ev), 1);
    rt_check_eq("buttons_two", pip_read_buttons(),
                (int32_t)(PIP_BTN_SELECT_BIT | PIP_BTN_LEFT_BIT));
    phase("ph_release");
    rt_check_eq("release_rc", wait_one(&ev), 1);
    rt_check_eq("release_value", ev.value, 0);
    rt_check_eq("buttons_one", pip_read_buttons(), (int32_t)PIP_BTN_LEFT_BIT);
    phase("ph_release2");
    rt_check_eq("release2_rc", wait_one(&ev), 1);
    rt_check_eq("buttons_released", pip_read_buttons(), 0);
}

static void dial(void)
{
    pip_event_t ev;

    phase("ph_dial");
    rt_check_eq("dial_back_rc", wait_one(&ev), 1);
    rt_check_eq("dial_back_value", ev.value, -10);
    rt_check_eq("dial_back", pip_read_dial(), 255);
    phase("ph_dial2");
    rt_check_eq("dial_big_rc", wait_one(&ev), 1);
    rt_check_eq("dial_big", pip_read_dial(), 135);
}

/*
 * A timed wait that an injected event ends early. The test sends
 * "wait 300" first: the kernel reads it inside this wait and holds the
 * event back, so the wait really blocked. Ticks run at 10 ms steps.
 */
static void timed(void)
{
    pip_event_t ev;
    int32_t t0, t1, rc;

    phase("ph_timed");
    t0 = pip_ticks_ms();
    rc = pip_wait_event(fresh(&ev), 60000);
    t1 = pip_ticks_ms();
    rt_check_eq("timed_rc", rc, 1);
    rt_check("timed_waited", t1 - t0 >= HELD_MS);
    rt_check("timed_early", t1 - t0 < 60000);
    rt_check_eq("timed_event", ev.value, 1);
    phase("ph_max");
    t0 = pip_ticks_ms();
    rc = pip_wait_event(fresh(&ev), PIP_WAIT_MAX_MS);
    t1 = pip_ticks_ms();
    rt_check_eq("max_rc", rc, 1);
    rt_check("max_waited", t1 - t0 >= HELD_MS);
    rt_check_eq("max_event", ev.value, -1);
}

/* 41 events while the ROM sleeps: it reads the newest 32. */
static void overflow(void)
{
    pip_event_t ev;
    int32_t n = 0, first = 0, last_dial = 0, in_order = 1, last_type = 0, last_code = -1;

    phase("ph_overflow");
    while (!(pip_read_buttons() & PIP_BTN_SELECT_BIT)) {
        pip_sleep_ms(20);
    }
    while (pip_poll_event(fresh(&ev)) == 1) {
        if (ev.type == PIP_EV_DIAL) {
            if (n == 0) {
                first = ev.value;
            } else if (ev.value != last_dial + 1) {
                in_order = 0;
            }
            last_dial = ev.value;
        }
        last_type = (int32_t)ev.type;
        last_code = (int32_t)ev.code;
        n++;
    }
    rt_check_eq("overflow_count", n, PIP_EVENT_QUEUE);
    rt_check_eq("overflow_first", first, 10);
    rt_check_eq("overflow_last_dial", last_dial, 40);
    rt_check("overflow_in_order", in_order);
    rt_check_eq("overflow_last_type", last_type, PIP_EV_BUTTON);
    rt_check_eq("overflow_last_code", last_code, PIP_BTN_SELECT);
    rt_check_eq("overflow_empty", pip_poll_event(fresh(&ev)), 0);
}

/* Spins without a system call that polls input, so RobCo-Core keeps what comes. */
static int32_t busy(int32_t ms)
{
    int32_t t0 = pip_ticks_ms();

    while (pip_ticks_ms() - t0 < ms) {
    }
    return t0;
}

/*
 * While the ROM is busy, the test puts 31 events' worth of bytes in the
 * console FIFO and "key up" in the script FIFO. The next poll reads both
 * in one pass: RobCo-Core's queue of 32 takes the press and drops the
 * release. read_buttons must still see the button up.
 */
static void stale(void)
{
    pip_event_t ev;
    int32_t presses = 0, releases = 0;

    phase("ph_stale_press");
    rt_check_eq("stale_select_rc", wait_one(&ev), 1);
    rt_check_eq("stale_select_value", ev.value, 0);
    phase("ph_stale");
    busy(BUSY_MS);
    while (pip_poll_event(fresh(&ev)) == 1) {
        if (ev.type == PIP_EV_BUTTON && ev.code == PIP_BTN_UP) {
            if (ev.value == 1) {
                presses++;
            } else if (ev.value == 0) {
                releases++;
            }
        }
    }
    rt_check_eq("stale_press_seen", presses, 1);
    rt_check_eq("stale_release_lost", releases, 0);
    rt_check_eq("stale_released", pip_read_buttons(), 0);
}

/*
 * The test injects a dial step while the ROM is busy. The kernel reads it
 * in the wait after the busy loop. The stamp lies between the start of
 * the busy phase and the ticks_ms read after the wait.
 */
static void stamp(void)
{
    pip_event_t ev;
    int32_t t0, t1, t2, rc;

    phase("ph_stamp");
    t0 = busy(BUSY_MS);
    t1 = pip_ticks_ms();
    rc = pip_wait_event(fresh(&ev), PIP_WAIT_FOREVER);
    t2 = pip_ticks_ms();
    rt_check_eq("stamp_rc", rc, 1);
    rt_check("stamp_not_before", (int32_t)ev.time_ms - t0 >= 0);
    rt_check("stamp_not_after", t2 - (int32_t)ev.time_ms >= 0);
    rt_begin("stamp");
    rt_dec("t0", t0);
    rt_dec("t1", t1);
    rt_dec("time", (int32_t)ev.time_ms);
    rt_dec("t2", t2);
    rt_end();
}

int pip_main(const pipos_startup_t *startup)
{
    (void)startup;
    start();
    faults();
    sequence();
    hold();
    dial();
    timed();
    overflow();
    stale();
    stamp();
    rt_checks_done();
    return 0;
}
