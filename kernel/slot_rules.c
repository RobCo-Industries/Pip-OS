/*
 * Pure rules of the holotape slot. See slot.h.
 */
#include <pipos/abi.h>

#include "slot.h"

static pipos_slot_out_t out(unsigned action, uint32_t bits)
{
    pipos_slot_out_t o = { action, bits };

    return o;
}

/* Moves to `state` and records the bits of the change for the ROM. */
static pipos_slot_out_t go(pipos_slot_t *s, unsigned state, unsigned action, uint32_t bits)
{
    s->state = state;
    s->pending |= bits;
    return out(action, bits);
}

void pipos_slot_reset(pipos_slot_t *s)
{
    s->state = PIPOS_SLOT_EMPTY;
    s->pending = 0;
}

pipos_slot_out_t pipos_slot_tick(pipos_slot_t *s, int card, int present)
{
    switch (s->state) {
    case PIPOS_SLOT_EMPTY:
        return card ? go(s, PIPOS_SLOT_SETTLING, PIPOS_SLOT_DO_NOTHING, 0)
                    : out(PIPOS_SLOT_DO_NOTHING, 0);
    case PIPOS_SLOT_SETTLING:
        /* Left in the debounce or between attach tries: never reported, no bit. */
        if (!card) {
            return go(s, PIPOS_SLOT_EMPTY, PIPOS_SLOT_DO_DETACH, 0);
        }
        return out(present ? PIPOS_SLOT_DO_ATTACH : PIPOS_SLOT_DO_NOTHING, 0);
    case PIPOS_SLOT_MOUNTED:
        return card ? out(PIPOS_SLOT_DO_NOTHING, 0)
                    : go(s, PIPOS_SLOT_EMPTY, PIPOS_SLOT_DO_EJECT, PIP_HOLO_EV_REMOVED);
    case PIPOS_SLOT_BAD:
        return card ? out(PIPOS_SLOT_DO_NOTHING, 0)
                    : go(s, PIPOS_SLOT_EMPTY, PIPOS_SLOT_DO_DETACH, PIP_HOLO_EV_REMOVED);
    default:
        return out(PIPOS_SLOT_DO_NOTHING, 0);
    }
}

pipos_slot_out_t pipos_slot_attached(pipos_slot_t *s, int r)
{
    if (s->state != PIPOS_SLOT_SETTLING) {
        return out(PIPOS_SLOT_DO_NOTHING, 0);
    }
    switch (r) {
    case PIPOS_ATTACH_OK:
        return out(PIPOS_SLOT_DO_MOUNT, 0);
    case PIPOS_ATTACH_AGAIN:
        return out(PIPOS_SLOT_DO_NOTHING, 0);
    case PIPOS_ATTACH_NOMEDIA:
        return go(s, PIPOS_SLOT_EMPTY, PIPOS_SLOT_DO_DETACH, 0);
    default:
        return go(s, PIPOS_SLOT_BAD, PIPOS_SLOT_DO_DETACH, PIP_HOLO_EV_BAD);
    }
}

pipos_slot_out_t pipos_slot_mounted(pipos_slot_t *s, int ok, int card)
{
    if (s->state != PIPOS_SLOT_SETTLING) {
        return out(PIPOS_SLOT_DO_NOTHING, 0);
    }
    if (ok) {
        return go(s, PIPOS_SLOT_MOUNTED, PIPOS_SLOT_DO_NOTHING, PIP_HOLO_EV_INSERTED);
    }
    /* A mount that failed because the card left is no report. */
    return card ? go(s, PIPOS_SLOT_BAD, PIPOS_SLOT_DO_DETACH, PIP_HOLO_EV_BAD)
                : go(s, PIPOS_SLOT_EMPTY, PIPOS_SLOT_DO_DETACH, 0);
}

pipos_slot_out_t pipos_slot_read_failed(pipos_slot_t *s, int card)
{
    if (s->state != PIPOS_SLOT_MOUNTED) {
        return out(PIPOS_SLOT_DO_NOTHING, 0);
    }
    /* Card detect tells a pull, REMOVED only, from a bad card, BAD. */
    return card ? go(s, PIPOS_SLOT_BAD, PIPOS_SLOT_DO_EJECT, PIP_HOLO_EV_BAD)
                : go(s, PIPOS_SLOT_EMPTY, PIPOS_SLOT_DO_EJECT, PIP_HOLO_EV_REMOVED);
}

uint32_t pipos_slot_take(pipos_slot_t *s, int caller_is_rom)
{
    uint32_t bits = s->pending;

    if (!caller_is_rom) {
        return 0;
    }
    s->pending = 0;
    return bits;
}

int pipos_holo_list_rule(int named, int header_ok, int shadowed, uint32_t count, uint32_t max)
{
    if (count >= max) {
        return PIPOS_LIST_STOP;
    }
    if (!named || shadowed || !header_ok) {
        return PIPOS_LIST_SKIP;
    }
    return PIPOS_LIST_ADD;
}

uint16_t pipos_holo_flags(int from_slot)
{
    return from_slot ? (uint16_t)PIP_HOLO_SLOT : 0u;
}
