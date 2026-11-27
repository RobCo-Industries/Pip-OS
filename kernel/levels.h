/*
 * The sound levels: the master and one level for each kind of sound, and
 * the gain of a play. Pure code: it builds and runs on the host in the
 * unit tests.
 */
#ifndef PIPOS_LEVELS_H
#define PIPOS_LEVELS_H

#include <stdint.h>

/* The kinds of sound, with the numbers of the ABI spec's PIP_SND_* values. */
#define PIPOS_SND_RADIO       0
#define PIPOS_SND_SFX         1
#define PIPOS_SND_RECORDINGS  2
#define PIPOS_SND_GAMES       3
#define PIPOS_SND_KINDS       4

/* Every level, the master and the trim run from 0 to this. */
#define PIPOS_LEVEL_MAX       100u
/* A gain in units of 1/10000: this is full scale. */
#define PIPOS_GAIN_FULL       10000u

typedef struct {
    unsigned master;                    /* the volume call's level */
    unsigned level[PIPOS_SND_KINDS];    /* by PIPOS_SND_* */
} pipos_levels_t;

/* The master and every level at PIPOS_LEVEL_MAX. */
void pipos_levels_default(pipos_levels_t *lv);

/*
 * gain = (master * level * level * trim + 5000) / 10000, each term capped
 * at PIPOS_LEVEL_MAX. The level follows a square law, the master and the
 * trim are linear. At level and trim 100 the gain is master * 100.
 */
uint32_t pipos_levels_gain(unsigned master, unsigned level, unsigned trim);

/* The gain of a play of `kind`. A kind out of range counts as sfx. */
uint32_t pipos_levels_gain_of(const pipos_levels_t *lv, int kind, unsigned trim);

/*
 * The kind an image's sounds take: sfx for a ROM, games for a holotape.
 * image_kind is a PIP_KIND_* value of the ABI.
 */
int pipos_levels_image_snd(int image_kind);

/* The key of a level in [audio] of PIPOS.CFG: "radio", "sfx", "recordings", "games". */
const char *pipos_levels_key(int kind);

#endif
