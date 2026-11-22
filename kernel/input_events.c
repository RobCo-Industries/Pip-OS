/* Pure input state for the input group. See input_events.h. */
#include "input_events.h"

void pipos_input_state_init(pipos_input_state_t *s)
{
    uint32_t i;

    s->head = 0;
    s->count = 0;
    s->dropped = 0;
    for (i = 0; i < PIPOS_INPUT_SOURCES; i++) {
        s->held[i] = 0;
    }
    s->dial_deg = 0;
}

static int button_of(unsigned code)
{
    switch (code) {
    case INPUT_KEY_UP:        return PIP_BTN_UP;
    case INPUT_KEY_DOWN:      return PIP_BTN_DOWN;
    case INPUT_KEY_LEFT:      return PIP_BTN_LEFT;
    case INPUT_KEY_RIGHT:     return PIP_BTN_RIGHT;
    case INPUT_KEY_SELECT:    return PIP_BTN_SELECT;
    case INPUT_KEY_DIAL_PUSH: return PIP_BTN_DIAL;
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

uint32_t pipos_input_dial_step(uint32_t deg, int32_t steps)
{
    int32_t turns = 360 / PIP_DIAL_DETENT_DEG;
    int32_t d = (int32_t)(deg % 360u) + (steps % turns) * PIP_DIAL_DETENT_DEG;

    return (uint32_t)((d + 360) % 360);
}

static void track(pipos_input_state_t *s, const input_event_t *ev, const pip_event_t *pe)
{
    unsigned src = ev->source < PIPOS_INPUT_SOURCES ? ev->source : PIPOS_INPUT_SOURCES - 1;

    if (pe->type == PIP_EV_DIAL) {
        s->dial_deg = pipos_input_dial_step(s->dial_deg, pe->value);
    } else if (pe->value == INPUT_PRESS) {
        s->held[src] |= PIP_BTN_BIT(pe->code);
    } else if (pe->value == INPUT_RELEASE) {
        s->held[src] &= ~PIP_BTN_BIT(pe->code);
    }
}

int pipos_input_feed(pipos_input_state_t *s, const input_event_t *ev, int deliver,
                     uint32_t now_ms)
{
    pip_event_t pe;

    if (!pipos_input_map(ev, &pe)) {
        return 0;
    }
    track(s, ev, &pe);
    if (!deliver) {
        return 0;
    }
    pe.time_ms = now_ms & 0x7fffffffu;
    if (s->count == PIP_EVENT_QUEUE) {
        s->head = (s->head + 1) % PIP_EVENT_QUEUE;
        s->count--;
        s->dropped++;
    }
    s->queue[(s->head + s->count) % PIP_EVENT_QUEUE] = pe;
    s->count++;
    return 1;
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

uint32_t pipos_input_buttons(const pipos_input_state_t *s)
{
    uint32_t m = 0;
    uint32_t i;

    for (i = 0; i < PIPOS_INPUT_SOURCES; i++) {
        m |= s->held[i];
    }
    return m;
}
