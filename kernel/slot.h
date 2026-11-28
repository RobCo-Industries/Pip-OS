/*
 * The holotape slot of ABI 1.4: the pure state machine and the listing
 * rule. slot_rules.c has no register access and builds on the host.
 * slot.c runs the machine on a backend, see slot_backend.h.
 *
 * States follow the diagram in PIPOS_ABI.md, "Slot changes":
 *
 *   EMPTY    no card, or a card that left
 *   SETTLING card detect low: the debounce, the attach and the mount.
 *            `attaching` is 1 while a stepped attach runs: each work
 *            pass then takes one step of it.
 *   MOUNTED  the "tape" volume is mounted
 *   BAD      the card cannot be used until it leaves
 *
 * Each input returns what slot.c must do next and the PIP_HOLO_EV_* bits
 * of the change, which are also added to the pending mask for the ROM.
 */
#ifndef PIPOS_SLOT_H
#define PIPOS_SLOT_H

#include <stdint.h>

enum {
    PIPOS_SLOT_EMPTY,
    PIPOS_SLOT_SETTLING,
    PIPOS_SLOT_MOUNTED,
    PIPOS_SLOT_BAD,
};

/* What slot.c does after an input. */
enum {
    PIPOS_SLOT_DO_NOTHING,
    PIPOS_SLOT_DO_START,    /* call the backend's attach_start */
    PIPOS_SLOT_DO_STEP,     /* call the backend's attach_step, once */
    PIPOS_SLOT_DO_MOUNT,    /* mount the backend's device as "tape", read-only */
    PIPOS_SLOT_DO_DETACH,   /* backend detach; no volume is mounted */
    PIPOS_SLOT_DO_EJECT,    /* fs_eject("tape"), then backend detach */
};

/*
 * What the attach start or step returned, mapped by slot.c from
 * SD_PENDING, SD_ERR_* and BLK_ERR_*.
 */
enum {
    PIPOS_ATTACH_OK,        /* a step returned 0: "holo0" is live */
    PIPOS_ATTACH_PENDING,   /* the start returned 0, or a step SD_PENDING */
    PIPOS_ATTACH_AGAIN,     /* SD_ERR_AGAIN: still settling */
    PIPOS_ATTACH_NOMEDIA,   /* BLK_ERR_NOMEDIA: the card left */
    PIPOS_ATTACH_REFUSED,   /* any other error, SD_ERR_SLOW included */
};

typedef struct pipos_slot {
    unsigned state;         /* PIPOS_SLOT_* */
    uint32_t pending;       /* PIP_HOLO_EV_* bits the ROM has not taken */
    int attaching;          /* SETTLING only: a stepped attach runs */
} pipos_slot_t;

typedef struct pipos_slot_out {
    unsigned action;        /* PIPOS_SLOT_DO_* */
    uint32_t bits;          /* PIP_HOLO_EV_* of this change, 0 for none */
} pipos_slot_out_t;

void pipos_slot_reset(pipos_slot_t *s);

/*
 * One work pass: `card` is the raw card detect (1 with a card in),
 * `present` the debounced one (card detect low for HOLO_SETTLE_MS).
 */
pipos_slot_out_t pipos_slot_tick(pipos_slot_t *s, int card, int present);

/* The attach start or one attach step returned `r` (PIPOS_ATTACH_*). */
pipos_slot_out_t pipos_slot_attached(pipos_slot_t *s, int r);

/* The mount returned `ok` (1 or 0); `card` is the raw card detect after it. */
pipos_slot_out_t pipos_slot_mounted(pipos_slot_t *s, int ok, int card);

/* A read on the tape volume failed; `card` is the raw card detect after it. */
pipos_slot_out_t pipos_slot_read_failed(pipos_slot_t *s, int card);

/*
 * holo_eject_event: the pending mask for the ROM, which it clears. A
 * holotape gets 0 and clears nothing.
 */
uint32_t pipos_slot_take(pipos_slot_t *s, int caller_is_rom);

/* ---- The listing rule of holo_enumerate ---------------------------------- */

/* What holo_enumerate does with one folder entry. */
enum {
    PIPOS_LIST_ADD,         /* list it */
    PIPOS_LIST_SKIP,        /* skip it, go on */
};

/*
 * The decision for one entry. Slot entries come first, then the boot
 * card's. `named` is 1 for a file with a valid NAME.HTP listed name that
 * the skip rule keeps. `header_ok` is 1 when its header passed the listing
 * checks. `shadowed` is 1 for a boot card entry whose name a slot file
 * also has, compared without case, whatever that slot file holds. The
 * count is not its business: pipos_holo_list_full() ends a full listing
 * before the next entry is read.
 */
int pipos_holo_list_rule(int named, int header_ok, int shadowed);

/*
 * 1 when the listing holds `max` entries: holo_enumerate then makes no
 * more file system call, no next entry, no shadow lookup, no boot card.
 */
int pipos_holo_list_full(uint32_t count, uint32_t max);

/* pip_holo_info_t.flags for an entry from the slot (1) or the boot card (0). */
uint16_t pipos_holo_flags(int from_slot);

#endif
