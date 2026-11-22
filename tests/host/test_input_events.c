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
    CHECK_EQ(pipos_input_feed(&s, &ev, 1, 10), 1);
    CHECK_EQ(pipos_input_buttons(&s), PIP_BTN_SELECT_BIT);
    ev = key(INPUT_KEY_SELECT, INPUT_REPEAT, 0);
    CHECK_EQ(pipos_input_feed(&s, &ev, 1, 20), 1);
    CHECK_EQ(pipos_input_buttons(&s), PIP_BTN_SELECT_BIT);
    ev = key(INPUT_KEY_SELECT, INPUT_RELEASE, 0);
    CHECK_EQ(pipos_input_feed(&s, &ev, 1, 30), 1);
    CHECK_EQ(pipos_input_buttons(&s), 0u);
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

TEST(mask_two_sources)
{
    pipos_input_state_t s;
    input_event_t ev;

    pipos_input_state_init(&s);
    ev = key(INPUT_KEY_UP, 1, INPUT_SRC_GPIO);
    pipos_input_feed(&s, &ev, 0, 0);
    ev = key(INPUT_KEY_UP, 1, INPUT_SRC_SCRIPT);
    pipos_input_feed(&s, &ev, 0, 0);
    ev = key(INPUT_KEY_LEFT, 1, INPUT_SRC_SERIAL);
    pipos_input_feed(&s, &ev, 0, 0);
    CHECK_EQ(pipos_input_buttons(&s), PIP_BTN_UP_BIT | PIP_BTN_LEFT_BIT);
    ev = key(INPUT_KEY_UP, 0, INPUT_SRC_SCRIPT);
    pipos_input_feed(&s, &ev, 0, 0);
    CHECK_EQ(pipos_input_buttons(&s), PIP_BTN_UP_BIT | PIP_BTN_LEFT_BIT);
    ev = key(INPUT_KEY_UP, 0, INPUT_SRC_GPIO);
    pipos_input_feed(&s, &ev, 0, 0);
    CHECK_EQ(pipos_input_buttons(&s), PIP_BTN_LEFT_BIT);
    /* A release from a source that did not press it changes nothing. */
    ev = key(INPUT_KEY_LEFT, 0, INPUT_SRC_GPIO);
    pipos_input_feed(&s, &ev, 0, 0);
    CHECK_EQ(pipos_input_buttons(&s), PIP_BTN_LEFT_BIT);
    /* An unknown source counts as the last one. */
    ev = key(INPUT_KEY_DIAL_PUSH, 1, 200);
    pipos_input_feed(&s, &ev, 0, 0);
    CHECK_EQ(pipos_input_buttons(&s), PIP_BTN_LEFT_BIT | PIP_BTN_DIAL_BIT);
    ev = key(INPUT_KEY_DIAL_PUSH, 0, 3);
    pipos_input_feed(&s, &ev, 0, 0);
    CHECK_EQ(pipos_input_buttons(&s), PIP_BTN_LEFT_BIT);
    /* Dropped keys never touch the mask. */
    ev = key(INPUT_KEY_BACK, 1, 0);
    pipos_input_feed(&s, &ev, 1, 0);
    CHECK_EQ(pipos_input_buttons(&s), PIP_BTN_LEFT_BIT);
    CHECK_EQ(s.count, 0u);
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
    CHECK_EQ(pipos_input_feed(&s, &ev, 0, 5), 0);
    CHECK_EQ(s.dial_deg, 30u);
    ev = dial(-5);
    CHECK_EQ(pipos_input_feed(&s, &ev, 1, 5), 1);
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
        CHECK_EQ(pipos_input_feed(&s, &ev, 1, (uint32_t)i * 10u), 1);
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
    pipos_input_feed(&s, &ev, 1, 0);
    ev = dial(3);
    pipos_input_feed(&s, &ev, 1, 0);
    pipos_input_queue_flush(&s);
    CHECK_EQ(pipos_input_pop(&s, &out), 0);
    CHECK_EQ(pipos_input_buttons(&s), PIP_BTN_DOWN_BIT);
    CHECK_EQ(s.dial_deg, 45u);
    ev = dial(1);
    pipos_input_feed(&s, &ev, 1, 7);
    CHECK_EQ(pipos_input_pop(&s, &out), 1);
    CHECK_EQ(out.value, 1);
}

TEST(timestamps_are_31_bit)
{
    pipos_input_state_t s;
    input_event_t ev = dial(1);
    pip_event_t out;

    pipos_input_state_init(&s);
    ev.t_ms = 12345; /* RobCo-Core's own stamp is not used */
    pipos_input_feed(&s, &ev, 1, 0xfffffff0u);
    pipos_input_feed(&s, &ev, 1, 0x80000001u);
    pipos_input_feed(&s, &ev, 1, 0x7fffffffu);
    CHECK_EQ(pipos_input_pop(&s, &out), 1);
    CHECK_EQ(out.time_ms, 0x7ffffff0u);
    CHECK_EQ(pipos_input_pop(&s, &out), 1);
    CHECK_EQ(out.time_ms, 1u);
    CHECK_EQ(pipos_input_pop(&s, &out), 1);
    CHECK_EQ(out.time_ms, 0x7fffffffu);
}
