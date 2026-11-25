/*
 * The real slot backend: RobCo-Core's holo.h with the Pip-Boy HAT pins,
 * HOLO_CONFIG_DEFAULT. Only slot.c calls it, and only when PIPOS.CFG
 * sets [holotape] slot = on, so with the default off no slot pin is
 * touched.
 */
#include <robco/holo.h>

#include "slot_backend.h"

static int holo_be_init(void)
{
    return holo_init(NULL);
}

static int holo_be_card(void)
{
    return (holo_status() & HOLO_ST_CARD) != 0;
}

/* holo_poll() samples card detect and detaches at once on a pull. */
static int holo_be_present(void)
{
    (void)holo_poll();
    return holo_present();
}

const pipos_slot_backend_t pipos_slot_holo = {
    "holo", holo_be_init, holo_be_card, holo_be_present, holo_attach, holo_detach, holo_blkdev,
};
