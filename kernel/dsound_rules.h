/*
 * The disk click rules: a debt of clicks and the limiter that pays it.
 * Pure code: it builds and runs on the host in the unit tests.
 *
 * Work adds clicks to the debt. The limiter runs once per 10 ms tick and
 * pays the debt in bursts with pauses between them. dsound.c holds the
 * glue: the block device wrapper, the tick callback and the markers.
 */
#ifndef PIPOS_DSOUND_RULES_H
#define PIPOS_DSOUND_RULES_H

#include <stdint.h>

#define PIPOS_CLICK_CAP     40u /* about 3.7 s of sound at the boot pace */
#define PIPOS_CLICK_CAP_ROM  8u
#define PIPOS_CLICK_SEED     1u

struct pipos_dsound {
    uint32_t rnd;
    unsigned owed, wait, left, n;
    int rom, open;
};

/* Seed 1, nothing owed, the gate closed, the boot pace. */
void pipos_dsound_init(struct pipos_dsound *s);

/* What one read or write call of count blocks adds: 2 + count / 4. */
unsigned pipos_dsound_io_adds(uint32_t count);

/* What one boot line written with typing on adds: 1 + printable / 6. */
unsigned pipos_dsound_text_adds(const char *text);

/* Adds clicks to the debt, up to the cap. A closed gate adds nothing. */
void pipos_dsound_add(struct pipos_dsound *s, unsigned clicks);

/* Opens the gate. */
void pipos_dsound_open(struct pipos_dsound *s);

/* Closes the gate and drops what is owed. */
void pipos_dsound_close(struct pipos_dsound *s);

/* The jump to a ROM: the ROM pace, and the debt cut to its cap. */
void pipos_dsound_rom(struct pipos_dsound *s);

/*
 * One 10 ms tick. can_play is 1 when the voice is free. Returns -1 for
 * nothing, or the click to start now, 0 to 11, in turn.
 */
int pipos_dsound_tick(struct pipos_dsound *s, int can_play);

#endif
