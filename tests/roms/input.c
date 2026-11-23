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
#define TICK_STEP_MS    10      /* ticks_ms moves in steps of this size */
#define REPEAT_DELAY_MS 500     /* kernel/robco_config.h */
#define REPEAT_RATE_MS  100
#define REPEAT_SLACK_MS 2000    /* upper bounds only: a loaded host runs late */
#define REPEATS_SEEN    3       /* repeats before the test releases the key */
#define REPEAT_QUIET_MS 300     /* no event may come this long after the release */

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

/*
 * Like wait_one, but skips repeat events. A key the test holds past
 * REPEAT_DELAY_MS repeats, and the phases that wait here check presses
 * and releases only.
 */
static int32_t wait_change(pip_event_t *ev)
{
    int32_t rc;

    do {
        rc = wait_one(ev);
    } while (rc == 1 && ev->type == PIP_EV_BUTTON && ev->value == 2);
    return rc;
}

static void hold(void)
{
    pip_event_t ev;

    phase("ph_hold");
    rt_check_eq("hold_rc", wait_change(&ev), 1);
    rt_check_eq("hold_value", ev.value, 1);
    rt_check_eq("buttons_held", pip_read_buttons(), (int32_t)PIP_BTN_SELECT_BIT);
    phase("ph_hold2");
    rt_check_eq("hold2_rc", wait_change(&ev), 1);
    rt_check_eq("buttons_two", pip_read_buttons(),
                (int32_t)(PIP_BTN_SELECT_BIT | PIP_BTN_LEFT_BIT));
    phase("ph_release");
    rt_check_eq("release_rc", wait_change(&ev), 1);
    rt_check_eq("release_value", ev.value, 0);
    rt_check_eq("buttons_one", pip_read_buttons(), (int32_t)PIP_BTN_LEFT_BIT);
    phase("ph_release2");
    rt_check_eq("release2_rc", wait_change(&ev), 1);
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
 * While the ROM is busy, the test puts 16 CRs in the console FIFO, 32
 * events of select pressed and released, and "dial 1" in the script FIFO.
 * The next poll reads both in one pass: RobCo-Core's queue of 32 drops its
 * oldest event, the first press, and keeps the newest 32 in order.
 */
static void flood(void)
{
    pip_event_t ev;
    int32_t n = 0, in_order = 1, last_type = 0, last_value = 0;

    phase("ph_flood_press");
    rt_check_eq("flood_select_rc", wait_change(&ev), 1);
    rt_check_eq("flood_select_value", ev.value, 0);
    phase("ph_flood");
    busy(BUSY_MS);
    while (pip_poll_event(fresh(&ev)) == 1) {
        n++;
        if (n < PIP_EVENT_QUEUE) {
            /* select released, then pressed and released 15 times */
            if (ev.type != PIP_EV_BUTTON || ev.code != PIP_BTN_SELECT ||
                ev.value != (n % 2 == 1 ? 0 : 1)) {
                in_order = 0;
            }
        }
        last_type = (int32_t)ev.type;
        last_value = ev.value;
    }
    rt_check_eq("flood_count", n, PIP_EVENT_QUEUE);
    rt_check("flood_in_order", in_order);
    rt_check_eq("flood_last_type", last_type, PIP_EV_DIAL);
    rt_check_eq("flood_last_value", last_value, 1);
    rt_check_eq("flood_released", pip_read_buttons(), 0);
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

/*
 * The test holds down until the ROM saw REPEATS_SEEN repeats. Stamps move
 * in tick steps, so the lower bounds allow one step.
 */
static void repeat(void)
{
    pip_event_t ev;
    int32_t t_press, t_last, gap, repeats = 0, codes = 1, early = 0, late = 0;
    int32_t first = -1, min_gap = 0x7fffffff, max_gap = 0;

    phase("ph_repeat");
    rt_check_eq("repeat_press_rc", wait_one(&ev), 1);
    rt_check_eq("repeat_press_value", ev.value, 1);
    t_press = (int32_t)ev.time_ms;
    t_last = t_press;
    for (;;) {
        if (wait_one(&ev) != 1 || ev.type != PIP_EV_BUTTON || ev.code != PIP_BTN_DOWN) {
            codes = 0;
            break;
        }
        if (ev.value != 2) {
            break;
        }
        gap = (int32_t)ev.time_ms - t_last;
        if (repeats == 0) {
            first = gap;
        } else {
            min_gap = gap < min_gap ? gap : min_gap;
            max_gap = gap > max_gap ? gap : max_gap;
        }
        t_last = (int32_t)ev.time_ms;
        if (++repeats == REPEATS_SEEN) {
            phase("ph_repeat_release");
        }
    }
    early = first < REPEAT_DELAY_MS - TICK_STEP_MS || min_gap < REPEAT_RATE_MS - TICK_STEP_MS;
    late = first >= REPEAT_DELAY_MS + REPEAT_SLACK_MS || max_gap >= REPEAT_RATE_MS + REPEAT_SLACK_MS;
    rt_check("repeat_codes", codes);
    rt_check("repeat_count", repeats >= REPEATS_SEEN);
    rt_check("repeat_not_early", !early);
    rt_check("repeat_not_late", !late);
    rt_check_eq("repeat_release_value", ev.value, 0);
    rt_check_eq("repeat_quiet", pip_wait_event(fresh(&ev), REPEAT_QUIET_MS), -PIP_ETIMEDOUT);
    rt_check_eq("repeat_released", pip_read_buttons(), 0);
    rt_begin("repeat");
    rt_dec("first", first);
    rt_dec("min_gap", min_gap);
    rt_dec("max_gap", max_gap);
    rt_dec("count", repeats);
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
    flood();
    stamp();
    repeat();
    rt_checks_done();
    return 0;
}
