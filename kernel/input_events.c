/* Pure input state for the input group. See input_events.h. */
#include "input_events.h"

void pipos_input_state_init(pipos_input_state_t *s)
{
    s->head = 0;
    s->count = 0;
    s->dropped = 0;
    s->last_ms = 0;
    s->stamped = 0;
}

static const uint16_t key_of_button[] = {
    [PIP_BTN_UP] = INPUT_KEY_UP,
    [PIP_BTN_DOWN] = INPUT_KEY_DOWN,
    [PIP_BTN_LEFT] = INPUT_KEY_LEFT,
    [PIP_BTN_RIGHT] = INPUT_KEY_RIGHT,
    [PIP_BTN_SELECT] = INPUT_KEY_SELECT,
    [PIP_BTN_DIAL] = INPUT_KEY_DIAL_PUSH,
    [PIP_BTN_BACK] = INPUT_KEY_BACK,
};
_Static_assert(sizeof(key_of_button) / sizeof(key_of_button[0]) == PIP_BTN_COUNT,
               "one key per button");

static int button_of(unsigned code)
{
    switch (code) {
    case INPUT_KEY_UP:        return PIP_BTN_UP;
    case INPUT_KEY_DOWN:      return PIP_BTN_DOWN;
    case INPUT_KEY_LEFT:      return PIP_BTN_LEFT;
    case INPUT_KEY_RIGHT:     return PIP_BTN_RIGHT;
    case INPUT_KEY_SELECT:    return PIP_BTN_SELECT;
    case INPUT_KEY_DIAL_PUSH: return PIP_BTN_DIAL;
    case INPUT_KEY_BACK:      return PIP_BTN_BACK;
    default:                  return -1;
    }
}

int pipos_input_map(const input_event_t *ev, pip_event_t *out)
{
    int btn;

    if (ev->type == INPUT_EV_KEY) {
        btn = button_of(ev->code);
        if (btn < 0 || ev->value < INPUT_RELEASE || ev->value > INPUT_REPEAT) {
            return 0;
        }
        out->type = PIP_EV_BUTTON;
        out->code = (uint32_t)btn;
        out->value = ev->value;
        out->time_ms = 0;
        return 1;
    }
    if (ev->type == INPUT_EV_DIAL && ev->value != 0) {
        out->type = PIP_EV_DIAL;
        out->code = 0;
        out->value = ev->value;
        out->time_ms = 0;
        return 1;
    }
    return 0;
}

uint32_t pipos_input_dial_deg(int32_t total)
{
    int32_t turns = 360 / PIP_DIAL_DETENT_DEG;
    int32_t d = (total % turns) * PIP_DIAL_DETENT_DEG;

    return (uint32_t)((d + 360) % 360);
}

uint32_t pipos_input_stamp(pipos_input_state_t *s, uint32_t ev_ms, uint32_t timer_now,
                           uint32_t tick_now)
{
    uint32_t age = timer_now - ev_ms;
    uint32_t t;

    if ((int32_t)age < 0) {
        age = 0; /* stamped after timer_now was read */
    }
    t = tick_now - age;
    if (s->stamped && (int32_t)(t - s->last_ms) < 0) {
        t = s->last_ms;
    }
    s->last_ms = t;
    s->stamped = 1;
    return t;
}

static void push(pipos_input_state_t *s, const pip_event_t *pe)
{
    if (s->count == PIP_EVENT_QUEUE) {
        s->head = (s->head + 1) % PIP_EVENT_QUEUE;
        s->count--;
        s->dropped++;
    }
    s->queue[(s->head + s->count) % PIP_EVENT_QUEUE] = *pe;
    s->count++;
}

int pipos_input_feed(pipos_input_state_t *s, const input_event_t *ev, int deliver,
                     uint32_t timer_now, uint32_t tick_now)
{
    pip_event_t pe;

    if (!pipos_input_map(ev, &pe)) {
        return 0;
    }
    if (!deliver) {
        return 0;
    }
    pe.time_ms = pipos_input_stamp(s, ev->t_ms, timer_now, tick_now) & 0x7fffffffu;
    push(s, &pe);
    return 1;
}

void pipos_input_push_tape(pipos_input_state_t *s, uint32_t bits, uint32_t tick_now)
{
    pip_event_t pe;

    pe.type = PIP_EV_TAPE;
    pe.code = 0;
    pe.value = (int32_t)bits;
    /* An event of age 0: the timer and tick clocks read the same instant. */
    pe.time_ms = pipos_input_stamp(s, 0, 0, tick_now) & 0x7fffffffu;
    push(s, &pe);
}

int pipos_input_pop(pipos_input_state_t *s, pip_event_t *out)
{
    if (s->count == 0) {
        return 0;
    }
    *out = s->queue[s->head];
    s->head = (s->head + 1) % PIP_EVENT_QUEUE;
    s->count--;
    return 1;
}

void pipos_input_queue_flush(pipos_input_state_t *s)
{
    s->head = 0;
    s->count = 0;
}

uint32_t pipos_input_buttons(int (*key_down)(unsigned code))
{
    uint32_t m = 0;
    uint32_t b;

    for (b = 0; b < sizeof(key_of_button) / sizeof(key_of_button[0]); b++) {
        if (key_down(key_of_button[b])) {
            m |= PIP_BTN_BIT(b);
        }
    }
    return m;
}
