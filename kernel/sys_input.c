/*
 * The input group: read_buttons, read_dial, poll_event and wait_event
 * (0x10-0x13). main.c hands every RobCo-Core event it takes to
 * pipos_input_event(); input_events.c maps it and keeps the queue.
 * read_buttons asks RobCo-Core's held-key state and read_dial its dial
 * total; both stay right when RobCo-Core's own queue drops events. See docs/API.md.
 */
#include <robco/input.h>
#include <robco/tick.h>
#include <robco/timer.h>

#include "input_events.h"
#include "pipos.h"
#include "sys.h"
#include "sysabi.h"

static pipos_input_state_t state;
static int image_running;

void pipos_input_event(const input_event_t *ev)
{
    uint32_t timer_now = timer_now_ms();

    pipos_input_feed(&state, ev, image_running, timer_now, tick_ms());
}

void pipos_input_flush(void)
{
    pipos_input_queue_flush(&state);
}

static int32_t sys_read_buttons(const pipos_sys_ctx_t *ctx, uint32_t a0, uint32_t a1,
                                uint32_t a2, uint32_t a3)
{
    (void)ctx, (void)a0, (void)a1, (void)a2, (void)a3;
    pipos_poll();
    return (int32_t)pipos_input_buttons(input_key_down);
}

static int32_t sys_read_dial(const pipos_sys_ctx_t *ctx, uint32_t a0, uint32_t a1, uint32_t a2,
                             uint32_t a3)
{
    (void)ctx, (void)a0, (void)a1, (void)a2, (void)a3;
    pipos_poll();
    return (int32_t)pipos_input_dial_deg(input_dial_total(0));
}

static int32_t sys_poll_event(const pipos_sys_ctx_t *ctx, uint32_t out, uint32_t a1, uint32_t a2,
                              uint32_t a3)
{
    pip_event_t ev;
    int32_t rc = pipos_user_check_out(ctx, out, sizeof(ev));

    (void)a1, (void)a2, (void)a3;
    if (rc != 0) {
        return rc;
    }
    pipos_poll();
    if (!pipos_input_pop(&state, &ev)) {
        return 0;
    }
    rc = pipos_user_copy_out(ctx, out, &ev, sizeof(ev));
    return rc != 0 ? rc : 1;
}

/*
 * Like sleep_ms: the deadline is on the 1 MHz system timer, so a timeout
 * is never short, and the kernel's main-loop work runs while it waits.
 */
static int32_t sys_wait_event(const pipos_sys_ctx_t *ctx, uint32_t out, uint32_t timeout_ms,
                              uint32_t a2, uint32_t a3)
{
    int forever = timeout_ms == PIP_WAIT_FOREVER;
    uint64_t deadline = 0;
    pip_event_t ev;
    int32_t rc;

    (void)a2, (void)a3;
    if (!forever && timeout_ms > PIP_WAIT_MAX_MS) {
        return -PIP_ERANGE;
    }
    rc = pipos_user_check_out(ctx, out, sizeof(ev));
    if (rc != 0) {
        return rc;
    }
    if (!forever) {
        deadline = pipos_sleep_deadline_us(timer_now_us(), timeout_ms);
    }
    for (;;) {
        pipos_poll();
        if (pipos_input_pop(&state, &ev)) {
            rc = pipos_user_copy_out(ctx, out, &ev, sizeof(ev));
            return rc != 0 ? rc : 1;
        }
        if (!forever && (timeout_ms == 0 || timer_expired(deadline))) {
            return -PIP_ETIMEDOUT;
        }
        __asm__ volatile("wfi");
    }
}

static const pipos_sys_entry_t input_calls[] = {
    PIPOS_SYS_ENTRY(INPUT, READ_BUTTONS, sys_read_buttons),
    PIPOS_SYS_ENTRY(INPUT, READ_DIAL, sys_read_dial),
    PIPOS_SYS_ENTRY(INPUT, POLL_EVENT, sys_poll_event),
    PIPOS_SYS_ENTRY(INPUT, WAIT_EVENT, sys_wait_event),
};

const pipos_sys_table_t pipos_input_table = PIPOS_SYS_TABLE(INPUT, "input", input_calls);

void pipos_input_init(void)
{
    pipos_input_state_init(&state);
}

/*
 * Events already waiting in RobCo-Core happened before the image: take
 * them now, as kernel events, so the image never sees them.
 */
void pipos_input_image_start(int kind)
{
    (void)kind;
    image_running = 0;
    pipos_poll();
    pipos_input_flush();
    image_running = 1;
}

void pipos_input_image_end(int kind)
{
    (void)kind;
    image_running = 0;
    pipos_input_flush();
}

void pipos_input_poll(void)
{
}
