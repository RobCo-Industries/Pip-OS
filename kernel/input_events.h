/*
 * The input group's pure state: the mapping from RobCo-Core events to
 * pip_event_t, the image's event queue and the event timestamps. The
 * button mask comes from RobCo-Core's held-key state and the dial position
 * from its dial total.
 * sys_input.c feeds it; the host tests drive it directly.
 */
#ifndef PIPOS_INPUT_EVENTS_H
#define PIPOS_INPUT_EVENTS_H

#include <stdint.h>

#include <pipos/abi.h>
#include <robco/input.h>

typedef struct pipos_input_state {
    pip_event_t queue[PIP_EVENT_QUEUE];
    uint32_t head;                          /* oldest event */
    uint32_t count;
    uint32_t dropped;                       /* events lost to a full queue */
    uint32_t last_ms;                       /* last stamp given, tick_ms() clock */
    uint32_t stamped;                       /* last_ms is valid */
} pipos_input_state_t;

/* Empty queue, no stamp given yet. */
void pipos_input_state_init(pipos_input_state_t *s);

/*
 * The ABI event for a RobCo-Core event, with time_ms 0. Returns 1 when
 * images get this event, 0 when they never see it.
 */
int pipos_input_map(const input_event_t *ev, pip_event_t *out);

/*
 * The time of a RobCo-Core event on the tick_ms() clock. ev_ms is the
 * event's t_ms, on the timer_now_ms() clock. timer_now and tick_now are
 * both clocks read now. The result is tick_now minus the event's age,
 * never later than tick_now, and never earlier than the last result, so
 * stamps do not go backwards. All clocks wrap at 2^32.
 */
uint32_t pipos_input_stamp(pipos_input_state_t *s, uint32_t ev_ms, uint32_t timer_now,
                           uint32_t tick_now);

/*
 * Takes one RobCo-Core event: queues the mapped event when `deliver` is set, stamped with pipos_input_stamp()
 * masked to 31 bits. Returns 1 when it queued an event. A full queue drops
 * its oldest event.
 */
int pipos_input_feed(pipos_input_state_t *s, const input_event_t *ev, int deliver,
                     uint32_t timer_now, uint32_t tick_now);

/*
 * Queues a PIP_EV_TAPE event with `bits` in value, stamped tick_now, never
 * before the last stamp. A full queue drops its oldest event.
 */
void pipos_input_push_tape(pipos_input_state_t *s, uint32_t bits, uint32_t tick_now);

/* Removes the oldest event into *out. Returns 1, or 0 when empty. */
int pipos_input_pop(pipos_input_state_t *s, pip_event_t *out);

/* Empties the queue. */
void pipos_input_queue_flush(pipos_input_state_t *s);

/*
 * The PIP_BTN_BIT mask of the buttons down now. key_down(code) is
 * RobCo-Core's input_key_down(): non-zero while any source holds the key.
 * It follows every KEY event pushed, even the ones RobCo-Core's full queue
 * dropped.
 */
uint32_t pipos_input_buttons(int (*key_down)(unsigned code));

/*
 * The dial position, 0 to 359, for RobCo-Core's input_dial_total(): the
 * signed sum of detents since boot, PIP_DIAL_DETENT_DEG degrees each.
 */
uint32_t pipos_input_dial_deg(int32_t total);

#endif
