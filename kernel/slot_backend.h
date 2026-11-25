/*
 * The backends under slot.c. Each one is the slot's card detect, its
 * attach and detach, and the block device of the attached card.
 *
 *   slot_holo.c  the real one, on RobCo-Core's holo.h
 *   slot_test.c  a card served from RAM, in test builds only
 *                (-DPIPOS_HOLO_TEST), driven by the holo script command
 *
 * Every function runs in main context or in a system call, never in an
 * interrupt handler.
 */
#ifndef PIPOS_SLOT_BACKEND_H
#define PIPOS_SLOT_BACKEND_H

#include <robco/blk.h>

typedef struct pipos_slot_backend {
    const char *name;
    /* Sets up the pins. 0, or a negative code: the slot stays off. */
    int (*init)(void);
    /* Raw card detect: 1 with a card in, not debounced. Never blocks. */
    int (*card)(void);
    /* Card detect has read "in" for HOLO_SETTLE_MS. Samples the pins too. */
    int (*present)(void);
    /* 0, SD_ERR_AGAIN, BLK_ERR_NOMEDIA or another error. May block. */
    int (*attach)(void);
    void (*detach)(void);
    /* The attached card's read-only device, or NULL. */
    blk_dev_t *(*blkdev)(void);
} pipos_slot_backend_t;

extern const pipos_slot_backend_t pipos_slot_holo;

#ifdef PIPOS_HOLO_TEST
extern const pipos_slot_backend_t pipos_slot_ram;
struct input_script_reply;
/* The holo script command of the test build. 0, or -1 for a bad command. */
int pipos_slot_test_command(int argc, char *argv[], struct input_script_reply *reply);
#endif

#endif
