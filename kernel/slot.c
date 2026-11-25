/*
 * The holotape slot module: it owns the slot's state, the pending bits
 * for the ROM, the mount and the eject. It runs the pure machine of
 * slot_rules.c on a backend from slot_backend.h. See docs/ARCHITECTURE.md,
 * "Holotape slot".
 *
 * Context: main context and system calls only. pipos_slot_work() runs
 * inside pipos_poll(), so inside the calls that do main-loop work. A card
 * attaches in steps, one holo_attach_step() per pass, so no pass holds
 * the loop for the card's whole init: see docs/ARCHITECTURE.md.
 * pipos_slot_eject_due() runs at every system call entry.
 *
 * SPI0 is shared with the panel. Every panel transfer starts in main
 * context (present, power, boot and panic text); Pip-OS has no interrupt
 * handler that draws. Every card transfer runs here or in the file reads
 * of sys_holo.c, also main context. Neither calls pipos_poll() during a
 * transfer, so a panel transfer never starts during a card read.
 */
#include <pipos/abi.h>
#include <robco/blk.h>
#include <robco/fs.h>
#include <robco/klibc.h>
#include <robco/sd.h>

#include "pipos.h"
#include "slot.h"
#include "slot_backend.h"

static const pipos_slot_backend_t *be;  /* NULL: slot off, ABI 1.3 behaviour */
static pipos_slot_t slot;
static int tape_running;                /* a slot tape runs now */
static int tape_ejected;                /* ... and its card left */

static const char *state_name(unsigned s)
{
    switch (s) {
    case PIPOS_SLOT_EMPTY:    return "empty";
    case PIPOS_SLOT_SETTLING: return "settling";
    case PIPOS_SLOT_MOUNTED:  return "mounted";
    default:                  return "bad";
    }
}

void pipos_slot_start(int on)
{
    const pipos_slot_backend_t *b = &pipos_slot_holo;
    int rc;

    pipos_slot_reset(&slot);
    be = NULL;
    if (!on) {
        return;
    }
#ifdef PIPOS_HOLO_TEST
    b = &pipos_slot_ram;
#endif
    rc = b->init();
    if (rc == 0) {
        be = b;
    }
    k_printf("@RC stage=" PIPOS_STAGE " ev=holo_slot backend=%s rc=%d\n", b->name, rc);
}

int pipos_slot_enabled(void)
{
    return be != NULL;
}

/* A step's code. SD_PENDING is positive: an attach still runs. */
static int attach_class(int rc)
{
    if (rc == SD_PENDING) {
        return PIPOS_ATTACH_PENDING;
    }
    if (rc == 0) {
        return PIPOS_ATTACH_OK;
    }
    if (rc == SD_ERR_AGAIN) {
        return PIPOS_ATTACH_AGAIN;
    }
    return rc == BLK_ERR_NOMEDIA ? PIPOS_ATTACH_NOMEDIA : PIPOS_ATTACH_REFUSED;
}

/* Carries out what the machine asked, which may feed it again. */
static void act(pipos_slot_out_t o)
{
    for (;;) {
        if (o.bits != 0) {
            k_printf("@RC stage=" PIPOS_STAGE " ev=holo_slot state=%s bits=%u\n",
                     state_name(slot.state), (unsigned)o.bits);
            pipos_input_tape(o.bits);
            if ((o.bits & PIP_HOLO_EV_REMOVED) != 0 && tape_running) {
                tape_ejected = 1;
            }
        }
        switch (o.action) {
        case PIPOS_SLOT_DO_START: {
            int rc = be->attach_start();

            /* 0 from the start means an attach runs, not that it is done. */
            o = pipos_slot_attached(&slot, rc == 0 ? PIPOS_ATTACH_PENDING : attach_class(rc));
            continue;
        }
        case PIPOS_SLOT_DO_STEP:
            o = pipos_slot_attached(&slot, attach_class(be->attach_step()));
            continue;
        case PIPOS_SLOT_DO_MOUNT: {
            int rc = fs_mount_flags(PIPOS_TAPE_VOLUME, be->blkdev(), FS_MOUNT_RDONLY);

            if (rc != FS_OK) {
                k_printf("@RC stage=" PIPOS_STAGE " ev=holo_mount rc=%s\n", fs_strerror(rc));
            }
            o = pipos_slot_mounted(&slot, rc == FS_OK, be->card());
            continue;
        }
        case PIPOS_SLOT_DO_EJECT:
            fs_eject(PIPOS_TAPE_VOLUME);
            be->detach();
            break;
        case PIPOS_SLOT_DO_DETACH:
            be->detach();
            break;
        default:
            break;
        }
        break;
    }
}

void pipos_slot_work(void)
{
    int present;

    if (be == NULL) {
        return;
    }
    present = be->present();
    act(pipos_slot_tick(&slot, be->card(), present));
}

int pipos_slot_mounted_now(void)
{
    return be != NULL && slot.state == PIPOS_SLOT_MOUNTED;
}

int pipos_slot_read_error(void)
{
    int card;

    if (be == NULL) {
        return 0;
    }
    card = be->card();
    act(pipos_slot_read_failed(&slot, card));
    return card;
}

uint32_t pipos_slot_take_bits(int caller_is_rom)
{
    return be == NULL ? 0 : pipos_slot_take(&slot, caller_is_rom);
}

void pipos_slot_tape_running(int running)
{
    tape_running = running;
    tape_ejected = 0;
}

int pipos_slot_eject_due(void)
{
    if (!tape_running || be == NULL) {
        return 0;
    }
    if (!be->card()) {
        tape_ejected = 1;
    }
    return tape_ejected;
}

const char *pipos_slot_state_name(void)
{
    return be == NULL ? "off" : state_name(slot.state);
}
