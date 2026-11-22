/*
 * The input group's pure state: the mapping from RobCo-Core events to
 * pip_event_t, the image's event queue, the button mask and the dial
 * position. sys_input.c feeds it; the host tests drive it directly.
 */
#ifndef PIPOS_INPUT_EVENTS_H
#define PIPOS_INPUT_EVENTS_H

#include <stdint.h>

#include <pipos/abi.h>
#include <robco/input.h>

/* Sources whose held buttons are tracked apart; a higher source counts as the last one. */
#define PIPOS_INPUT_SOURCES 4

typedef struct pipos_input_state {
    pip_event_t queue[PIP_EVENT_QUEUE];
    uint32_t head;                          /* oldest event */
    uint32_t count;
    uint32_t dropped;                       /* events lost to a full queue */
    uint32_t held[PIPOS_INPUT_SOURCES];     /* PIP_BTN_BIT mask per source */
    uint32_t dial_deg;                      /* 0 to 359 */
} pipos_input_state_t;

/* Empty queue, no button down, dial at 0. */
void pipos_input_state_init(pipos_input_state_t *s);

/*
 * The ABI event for a RobCo-Core event, with time_ms 0. Returns 1 when
 * images get this event, 0 when they never see it.
 */
int pipos_input_map(const input_event_t *ev, pip_event_t *out);

/*
 * Takes one RobCo-Core event at `now_ms`: always updates the button mask
 * and the dial, and queues the mapped event when `deliver` is set. Returns
 * 1 when it queued an event. A full queue drops its oldest event.
 */
int pipos_input_feed(pipos_input_state_t *s, const input_event_t *ev, int deliver,
                     uint32_t now_ms);

/* Removes the oldest event into *out. Returns 1, or 0 when empty. */
int pipos_input_pop(pipos_input_state_t *s, pip_event_t *out);

/* Empties the queue. Buttons and dial stay. */
void pipos_input_queue_flush(pipos_input_state_t *s);

/* The PIP_BTN_BIT mask of the buttons any source holds down. */
uint32_t pipos_input_buttons(const pipos_input_state_t *s);

/* The dial position after `steps` detents from `deg`: 0 to 359. */
uint32_t pipos_input_dial_step(uint32_t deg, int32_t steps);

#endif
