#include <stdint.h>
#include <string.h>

#include "input_events.h"
#include "rc_test.h"

static input_event_t key(unsigned code, int32_t value, unsigned source)
{
    input_event_t ev;

    memset(&ev, 0, sizeof(ev));
    ev.type = INPUT_EV_KEY;
    ev.code = (uint16_t)code;
    ev.value = value;
    ev.source = (uint8_t)source;
    return ev;
}

static input_event_t dial(int32_t steps)
{
    input_event_t ev;

    memset(&ev, 0, sizeof(ev));
    ev.type = INPUT_EV_DIAL;
    ev.value = steps;
    return ev;
}

TEST(map_every_key_code)
{
    static const int want[INPUT_KEY_COUNT + 2] = {
        -1, PIP_BTN_UP, PIP_BTN_DOWN, PIP_BTN_LEFT, PIP_BTN_RIGHT, PIP_BTN_SELECT,
        -1, -1, -1, -1, -1, PIP_BTN_DIAL, -1, -1,
    };
    unsigned code;
    int32_t v;

    for (code = 0; code < INPUT_KEY_COUNT + 2; code++) {
        for (v = 0; v <= 2; v++) {
            input_event_t ev = key(code, v, INPUT_SRC_GPIO);
            pip_event_t out;
            int got;

            memset(&out, 0xaa, sizeof(out));
            got = pipos_input_map(&ev, &out);
            CHECK_EQ(got, want[code] >= 0);
            if (got) {
                CHECK_EQ(out.type, PIP_EV_BUTTON);
                CHECK_EQ(out.code, (uint32_t)want[code]);
                CHECK_EQ(out.value, v);
                CHECK_EQ(out.time_ms, 0u);
            }
        }
    }
}

TEST(map_drops_other_events)
{
    input_event_t ev = key(INPUT_KEY_UP, 3, 0);
    pip_event_t out;

    CHECK_EQ(pipos_input_map(&ev, &out), 0);
    ev = key(INPUT_KEY_UP, -1, 0);
    CHECK_EQ(pipos_input_map(&ev, &out), 0);
    ev = key('x', 0, INPUT_SRC_SERIAL);
    ev.type = INPUT_EV_CHAR;
    CHECK_EQ(pipos_input_map(&ev, &out), 0);
    ev.type = 9;
    CHECK_EQ(pipos_input_map(&ev, &out), 0);
    ev = dial(0);
    CHECK_EQ(pipos_input_map(&ev, &out), 0);
}

TEST(map_dial)
{
    input_event_t ev = dial(-7);
    pip_event_t out;

    ev.code = 1; /* second dial: still code 0 */
    CHECK_EQ(pipos_input_map(&ev, &out), 1);
    CHECK_EQ(out.type, PIP_EV_DIAL);
    CHECK_EQ(out.code, 0u);
    CHECK_EQ(out.value, -7);
}

TEST(press_release_repeat)
{
    pipos_input_state_t s;
    input_event_t ev;
    pip_event_t out;

    pipos_input_state_init(&s);
    ev = key(INPUT_KEY_SELECT, INPUT_PRESS, 0);
    ev.t_ms = 10;
    CHECK_EQ(pipos_input_feed(&s, &ev, 1, 10, 10), 1);
    ev = key(INPUT_KEY_SELECT, INPUT_REPEAT, 0);
    ev.t_ms = 20;
    CHECK_EQ(pipos_input_feed(&s, &ev, 1, 20, 20), 1);
    ev = key(INPUT_KEY_SELECT, INPUT_RELEASE, 0);
    ev.t_ms = 30;
    CHECK_EQ(pipos_input_feed(&s, &ev, 1, 30, 30), 1);
    CHECK_EQ(pipos_input_pop(&s, &out), 1);
    CHECK_EQ(out.value, 1);
    CHECK_EQ(out.time_ms, 10u);
    CHECK_EQ(pipos_input_pop(&s, &out), 1);
    CHECK_EQ(out.value, 2);
    CHECK_EQ(out.time_ms, 20u);
    CHECK_EQ(pipos_input_pop(&s, &out), 1);
    CHECK_EQ(out.value, 0);
    CHECK_EQ(out.code, (uint32_t)PIP_BTN_SELECT);
    CHECK_EQ(pipos_input_pop(&s, &out), 0);
}

TEST(dial_steps)
{
    CHECK_EQ(pipos_input_dial_step(0, 1), 15u);
    CHECK_EQ(pipos_input_dial_step(0, -1), 345u);
    CHECK_EQ(pipos_input_dial_step(345, 1), 0u);
    CHECK_EQ(pipos_input_dial_step(15, -2), 345u);
    CHECK_EQ(pipos_input_dial_step(0, 24), 0u);
    CHECK_EQ(pipos_input_dial_step(0, -24), 0u);
    CHECK_EQ(pipos_input_dial_step(30, 1000000), 270u);  /* 1000000 % 24 = 16 */
    CHECK_EQ(pipos_input_dial_step(30, -1000000), 150u);
    CHECK_EQ(pipos_input_dial_step(0, INT32_MAX), 105u); /* % 24 = 7 */
    CHECK_EQ(pipos_input_dial_step(0, INT32_MIN), 240u); /* % 24 = -8 */
}

TEST(dial_accumulates_without_image)
{
    pipos_input_state_t s;
    input_event_t ev;

    pipos_input_state_init(&s);
    CHECK_EQ(s.dial_deg, 0u);
    ev = dial(2);
    CHECK_EQ(pipos_input_feed(&s, &ev, 0, 5, 5), 0);
    CHECK_EQ(s.dial_deg, 30u);
    ev = dial(-5);
    CHECK_EQ(pipos_input_feed(&s, &ev, 1, 5, 5), 1);
    CHECK_EQ(s.dial_deg, 315u);
    CHECK_EQ(s.count, 1u);
}

TEST(queue_overflow_drops_oldest)
{
    pipos_input_state_t s;
    input_event_t ev;
    pip_event_t out;
    int32_t i;

    pipos_input_state_init(&s);
    for (i = 1; i <= PIP_EVENT_QUEUE + 9; i++) {
        ev = dial(i);
        ev.t_ms = (uint32_t)i * 10u;
        CHECK_EQ(pipos_input_feed(&s, &ev, 1, (uint32_t)i * 10u, (uint32_t)i * 10u), 1);
    }
    CHECK_EQ(s.count, (uint32_t)PIP_EVENT_QUEUE);
    CHECK_EQ(s.dropped, 9u);
    for (i = 10; i <= PIP_EVENT_QUEUE + 9; i++) {
        CHECK_EQ(pipos_input_pop(&s, &out), 1);
        CHECK_EQ(out.value, i);
        CHECK_EQ(out.time_ms, (uint32_t)i * 10u);
    }
    CHECK_EQ(pipos_input_pop(&s, &out), 0);
}

TEST(flush)
{
    pipos_input_state_t s;
    input_event_t ev;
    pip_event_t out;

    pipos_input_state_init(&s);
    ev = key(INPUT_KEY_DOWN, 1, 0);
    pipos_input_feed(&s, &ev, 1, 0, 0);
    ev = dial(3);
    pipos_input_feed(&s, &ev, 1, 0, 0);
    pipos_input_queue_flush(&s);
    CHECK_EQ(pipos_input_pop(&s, &out), 0);
    CHECK_EQ(s.dial_deg, 45u);
    ev = dial(1);
    pipos_input_feed(&s, &ev, 1, 7, 7);
    CHECK_EQ(pipos_input_pop(&s, &out), 1);
    CHECK_EQ(out.value, 1);
}


/* A stand-in for RobCo-Core's queue and held-key state, built from its pure parts. */
static input_queue_t core_q;
static input_hold_t core_hold;

static void core_init(void)
{
    input_queue_init(&core_q);
    input_hold_init(&core_hold);
}

/* Like input_push(): the held state follows the event even when the queue drops it. */
static void core_push(input_event_t ev)
{
    input_hold_update(&core_hold, &ev);
    input_queue_push(&core_q, &ev);
}

static int core_key_down(unsigned code)
{
    return input_hold_down(&core_hold, code);
}

/* Like pipos_poll(): every queued Core event goes to the image's state. */
static void core_drain(pipos_input_state_t *s)
{
    input_event_t ev;

    while (input_queue_pop(&core_q, &ev)) {
        pipos_input_feed(s, &ev, 1, 0, 0);
    }
}

TEST(buttons_from_core_hold)
{
    pipos_input_state_t s;
    unsigned code;

    pipos_input_state_init(&s);
    core_init();
    CHECK_EQ(pipos_input_buttons(core_key_down), 0u);
    for (code = 0; code < INPUT_KEY_COUNT; code++) {
        core_push(key(code, INPUT_PRESS, INPUT_SRC_GPIO));
    }
    /* Back and F1 to F4 have no bit. */
    CHECK_EQ(pipos_input_buttons(core_key_down), 0x3fu);
    core_drain(&s);
    core_push(key(INPUT_KEY_UP, INPUT_PRESS, INPUT_SRC_SCRIPT));
    core_push(key(INPUT_KEY_UP, INPUT_RELEASE, INPUT_SRC_GPIO));
    CHECK_EQ(pipos_input_buttons(core_key_down), 0x3fu);
    core_push(key(INPUT_KEY_UP, INPUT_RELEASE, INPUT_SRC_SCRIPT));
    core_push(key(INPUT_KEY_RIGHT, INPUT_RELEASE, INPUT_SRC_GPIO));
    CHECK_EQ(pipos_input_buttons(core_key_down), 0x3fu & ~(PIP_BTN_UP_BIT | PIP_BTN_RIGHT_BIT));
}

/* Core's full queue drops the release: the mask still shows the button up. */
TEST(buttons_after_dropped_release)
{
    pipos_input_state_t s;
    pip_event_t out;
    int32_t i;
    int saw_release = 0;

    pipos_input_state_init(&s);
    core_init();
    core_push(key(INPUT_KEY_SELECT, INPUT_PRESS, INPUT_SRC_SCRIPT));
    core_drain(&s);
    CHECK_EQ(pipos_input_buttons(core_key_down), PIP_BTN_SELECT_BIT);
    for (i = 1; i <= (int32_t)INPUT_QUEUE_LEN; i++) {
        core_push(dial(i));
    }
    core_push(key(INPUT_KEY_SELECT, INPUT_RELEASE, INPUT_SRC_SCRIPT));
    CHECK_EQ(core_q.dropped, 1u);
    core_drain(&s);
    while (pipos_input_pop(&s, &out)) {
        if (out.type == PIP_EV_BUTTON && out.value == INPUT_RELEASE) {
            saw_release = 1;
        }
    }
    CHECK_EQ(saw_release, 0);
    CHECK_EQ(pipos_input_buttons(core_key_down), 0u);
}

TEST(stamp_uses_event_age)
{
    pipos_input_state_t s;

    pipos_input_state_init(&s);
    /* The timer runs 1000 ms ahead of the tick: the age carries over. */
    CHECK_EQ(pipos_input_stamp(&s, 1500, 1800, 800), 500u);
    CHECK_EQ(pipos_input_stamp(&s, 1800, 1800, 800), 800u);
    /* An event pushed after timer_now was read gets tick_now. */
    CHECK_EQ(pipos_input_stamp(&s, 2005, 2000, 1000), 1000u);
}

TEST(stamp_never_goes_back)
{
    pipos_input_state_t s;

    pipos_input_state_init(&s);
    CHECK_EQ(pipos_input_stamp(&s, 100, 100, 100), 100u);
    /* The tick reads 3 ms behind this time: the stamp holds at 100. */
    CHECK_EQ(pipos_input_stamp(&s, 101, 101, 98), 100u);
    CHECK_EQ(pipos_input_stamp(&s, 110, 110, 110), 110u);
    /* Older than the last stamp: held there too. */
    CHECK_EQ(pipos_input_stamp(&s, 50, 120, 120), 110u);
}

TEST(stamp_clock_wraps)
{
    pipos_input_state_t s;

    pipos_input_state_init(&s);
    /* The event is 20 ms old across the wrap of the timer. */
    CHECK_EQ(pipos_input_stamp(&s, 0xfffffff0u, 4, 1000), 980u);
    pipos_input_state_init(&s);
    /* The tick wraps between the event and now. */
    CHECK_EQ(pipos_input_stamp(&s, 500, 520, 5), 0xfffffff1u);
    /* The last stamp is just below the wrap: a stamp past it still moves on. */
    CHECK_EQ(pipos_input_stamp(&s, 530, 530, 15), 15u);
}

TEST(stamp_is_31_bit_in_events)
{
    pipos_input_state_t s;
    input_event_t ev = dial(1);
    pip_event_t out;

    pipos_input_state_init(&s);
    ev.t_ms = 90;
    pipos_input_feed(&s, &ev, 1, 100, 0x7ffffff5u);
    ev.t_ms = 100;
    pipos_input_feed(&s, &ev, 1, 100, 0x80000001u);
    ev.t_ms = 100;
    pipos_input_feed(&s, &ev, 1, 100, 0xffffffffu);
    ev.t_ms = 100;
    pipos_input_feed(&s, &ev, 1, 100, 0x00000002u);
    CHECK_EQ(pipos_input_pop(&s, &out), 1);
    CHECK_EQ(out.time_ms, 0x7fffffebu);
    CHECK_EQ(pipos_input_pop(&s, &out), 1);
    CHECK_EQ(out.time_ms, 1u);
    CHECK_EQ(pipos_input_pop(&s, &out), 1);
    CHECK_EQ(out.time_ms, 0x7fffffffu);
    CHECK_EQ(pipos_input_pop(&s, &out), 1);
    CHECK_EQ(out.time_ms, 2u);
}
