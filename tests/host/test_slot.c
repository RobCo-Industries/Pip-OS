/*
 * kernel/slot_rules.c: the holotape slot state machine, one test per row
 * of the diagram in PIPOS_ABI.md "Slot changes", and the listing rule.
 */
#include <stdint.h>

#include <pipos/abi.h>

#include "rc_test.h"
#include "slot.h"

#define INS PIP_HOLO_EV_INSERTED
#define REM PIP_HOLO_EV_REMOVED
#define BAD PIP_HOLO_EV_BAD

static pipos_slot_t fresh(void)
{
    pipos_slot_t s;

    pipos_slot_reset(&s);
    return s;
}

/* EMPTY, card in, debounce over, attach ok: the slot asks for the mount. */
static pipos_slot_t settled(void)
{
    pipos_slot_t s = fresh();
    pipos_slot_out_t o;

    o = pipos_slot_tick(&s, 1, 0);
    CHECK_EQ(o.action, PIPOS_SLOT_DO_NOTHING);
    o = pipos_slot_tick(&s, 1, 1);
    CHECK_EQ(o.action, PIPOS_SLOT_DO_ATTACH);
    o = pipos_slot_attached(&s, PIPOS_ATTACH_OK);
    CHECK_EQ(o.action, PIPOS_SLOT_DO_MOUNT);
    CHECK_EQ(o.bits, 0);
    CHECK_EQ(s.state, PIPOS_SLOT_SETTLING);
    return s;
}

static pipos_slot_t mounted(void)
{
    pipos_slot_t s = settled();
    pipos_slot_out_t o = pipos_slot_mounted(&s, 1, 1);

    CHECK_EQ(o.bits, INS);
    CHECK_EQ(s.state, PIPOS_SLOT_MOUNTED);
    CHECK_EQ(pipos_slot_take(&s, 1), INS);
    return s;
}

TEST(empty_stays_empty)
{
    pipos_slot_t s = fresh();
    pipos_slot_out_t o = pipos_slot_tick(&s, 0, 0);

    CHECK_EQ(o.action, PIPOS_SLOT_DO_NOTHING);
    CHECK_EQ(s.state, PIPOS_SLOT_EMPTY);
    CHECK_EQ(pipos_slot_take(&s, 1), 0);
}

/* Empty --> Settling: card detect low. */
TEST(empty_to_settling)
{
    pipos_slot_t s = fresh();
    pipos_slot_out_t o = pipos_slot_tick(&s, 1, 0);

    CHECK_EQ(s.state, PIPOS_SLOT_SETTLING);
    CHECK_EQ(o.bits, 0);
    CHECK_EQ(s.pending, 0);
}

/* Settling --> Empty: the card left in the debounce, no bit. */
TEST(left_in_debounce)
{
    pipos_slot_t s = fresh();
    pipos_slot_out_t o;

    pipos_slot_tick(&s, 1, 0);
    o = pipos_slot_tick(&s, 0, 0);
    CHECK_EQ(s.state, PIPOS_SLOT_EMPTY);
    CHECK_EQ(o.bits, 0);
    CHECK_EQ(pipos_slot_take(&s, 1), 0);
}

/* Settling --> Empty: the attach returned BLK_ERR_NOMEDIA, no bit. */
TEST(left_in_attach)
{
    pipos_slot_t s = fresh();
    pipos_slot_out_t o;

    pipos_slot_tick(&s, 1, 1);
    o = pipos_slot_attached(&s, PIPOS_ATTACH_NOMEDIA);
    CHECK_EQ(o.action, PIPOS_SLOT_DO_DETACH);
    CHECK_EQ(s.state, PIPOS_SLOT_EMPTY);
    CHECK_EQ(pipos_slot_take(&s, 1), 0);
}

/* Settling --> Empty: the card left during the mount, no bit. */
TEST(left_in_mount)
{
    pipos_slot_t s = settled();
    pipos_slot_out_t o = pipos_slot_mounted(&s, 0, 0);

    CHECK_EQ(o.action, PIPOS_SLOT_DO_DETACH);
    CHECK_EQ(s.state, PIPOS_SLOT_EMPTY);
    CHECK_EQ(pipos_slot_take(&s, 1), 0);
}

/* SD_ERR_AGAIN: still settling, try again on the next pass, no bit. */
TEST(attach_again)
{
    pipos_slot_t s = fresh();
    pipos_slot_out_t o;

    pipos_slot_tick(&s, 1, 1);
    o = pipos_slot_attached(&s, PIPOS_ATTACH_AGAIN);
    CHECK_EQ(o.action, PIPOS_SLOT_DO_NOTHING);
    CHECK_EQ(s.state, PIPOS_SLOT_SETTLING);
    CHECK_EQ(pipos_slot_tick(&s, 1, 1).action, PIPOS_SLOT_DO_ATTACH);
    CHECK_EQ(s.pending, 0);
}

/* Settling --> Mounted: debounce over, mount ok / INSERTED. */
TEST(settling_to_mounted)
{
    pipos_slot_t s = mounted();

    CHECK_EQ(pipos_slot_tick(&s, 1, 1).action, PIPOS_SLOT_DO_NOTHING);
    CHECK_EQ(pipos_slot_take(&s, 1), 0);
}

/* Settling --> Bad: the card refused / BAD. */
TEST(attach_refused)
{
    pipos_slot_t s = fresh();
    pipos_slot_out_t o;

    pipos_slot_tick(&s, 1, 1);
    o = pipos_slot_attached(&s, PIPOS_ATTACH_REFUSED);
    CHECK_EQ(o.bits, BAD);
    CHECK_EQ(s.state, PIPOS_SLOT_BAD);
    /* Not tried again until it leaves. */
    CHECK_EQ(pipos_slot_tick(&s, 1, 1).action, PIPOS_SLOT_DO_NOTHING);
    CHECK_EQ(pipos_slot_take(&s, 1), BAD);
}

/* Settling --> Bad: the mount refused, the card still in / BAD. */
TEST(mount_refused)
{
    pipos_slot_t s = settled();
    pipos_slot_out_t o = pipos_slot_mounted(&s, 0, 1);

    CHECK_EQ(o.action, PIPOS_SLOT_DO_DETACH);
    CHECK_EQ(o.bits, BAD);
    CHECK_EQ(s.state, PIPOS_SLOT_BAD);
}

/* Mounted --> Empty: card pulled / REMOVED, the volume is ejected. */
TEST(mounted_pulled)
{
    pipos_slot_t s = mounted();
    pipos_slot_out_t o = pipos_slot_tick(&s, 0, 0);

    CHECK_EQ(o.action, PIPOS_SLOT_DO_EJECT);
    CHECK_EQ(o.bits, REM);
    CHECK_EQ(s.state, PIPOS_SLOT_EMPTY);
    CHECK_EQ(pipos_slot_take(&s, 1), REM);
}

/* Mounted --> Bad: read error, card still in / BAD. */
TEST(read_error_card_in)
{
    pipos_slot_t s = mounted();
    pipos_slot_out_t o = pipos_slot_read_failed(&s, 1);

    CHECK_EQ(o.action, PIPOS_SLOT_DO_EJECT);
    CHECK_EQ(o.bits, BAD);
    CHECK_EQ(s.state, PIPOS_SLOT_BAD);
}

/* A pull during a call: the read fails, card detect is high: REMOVED only. */
TEST(read_error_card_pulled)
{
    pipos_slot_t s = mounted();
    pipos_slot_out_t o = pipos_slot_read_failed(&s, 0);

    CHECK_EQ(o.action, PIPOS_SLOT_DO_EJECT);
    CHECK_EQ(o.bits, REM);
    CHECK_EQ(s.state, PIPOS_SLOT_EMPTY);
    CHECK_EQ(pipos_slot_take(&s, 1), REM);
}

/* Bad --> Empty: card pulled / REMOVED. */
TEST(bad_pulled)
{
    pipos_slot_t s = mounted();
    pipos_slot_out_t o;

    pipos_slot_read_failed(&s, 1);
    o = pipos_slot_tick(&s, 0, 0);
    CHECK_EQ(o.bits, REM);
    CHECK_EQ(s.state, PIPOS_SLOT_EMPTY);
    /* Several changes between two calls: several bits. */
    CHECK_EQ(pipos_slot_take(&s, 1), BAD | REM);
}

/* A read failure outside MOUNTED changes nothing. */
TEST(read_error_elsewhere)
{
    pipos_slot_t s = fresh();

    CHECK_EQ(pipos_slot_read_failed(&s, 1).action, PIPOS_SLOT_DO_NOTHING);
    CHECK_EQ(s.state, PIPOS_SLOT_EMPTY);
    s = mounted();
    pipos_slot_read_failed(&s, 1);
    CHECK_EQ(pipos_slot_read_failed(&s, 1).bits, 0);
}

/* A second card after the first. */
TEST(second_card)
{
    pipos_slot_t s = mounted();

    pipos_slot_tick(&s, 0, 0);
    pipos_slot_tick(&s, 1, 1);
    CHECK_EQ(pipos_slot_attached(&s, PIPOS_ATTACH_OK).action, PIPOS_SLOT_DO_MOUNT);
    pipos_slot_mounted(&s, 1, 1);
    CHECK_EQ(pipos_slot_take(&s, 1), REM | INS);
}

/* A holotape gets 0 and the bits wait for the ROM. */
TEST(holotape_take)
{
    pipos_slot_t s = mounted();

    pipos_slot_tick(&s, 0, 0);
    CHECK_EQ(pipos_slot_take(&s, 0), 0);
    CHECK_EQ(pipos_slot_take(&s, 1), REM);
    CHECK_EQ(pipos_slot_take(&s, 1), 0);
}

TEST(list_rule)
{
    CHECK_EQ(pipos_holo_list_rule(1, 1, 0, 0, 32), PIPOS_LIST_ADD);
    CHECK_EQ(pipos_holo_list_rule(1, 1, 0, 31, 32), PIPOS_LIST_ADD);
    CHECK_EQ(pipos_holo_list_rule(1, 1, 0, 32, 32), PIPOS_LIST_STOP);
    CHECK_EQ(pipos_holo_list_rule(1, 1, 0, 1, 1), PIPOS_LIST_STOP);
    /* Shadowed by a slot file with the same name, even a damaged one. */
    CHECK_EQ(pipos_holo_list_rule(1, 1, 1, 0, 32), PIPOS_LIST_SKIP);
    /* A failed header or a name outside the rule is skipped. */
    CHECK_EQ(pipos_holo_list_rule(1, 0, 0, 0, 32), PIPOS_LIST_SKIP);
    CHECK_EQ(pipos_holo_list_rule(0, 1, 0, 0, 32), PIPOS_LIST_SKIP);
    CHECK_EQ(pipos_holo_flags(1), PIP_HOLO_SLOT);
    CHECK_EQ(pipos_holo_flags(0), 0);
}
