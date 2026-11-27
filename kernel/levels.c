/*
 * The sound levels and the gain of a play. See levels.h.
 */
#include <pipos/abi.h>

#include "levels.h"

static const char *const level_keys[PIPOS_SND_KINDS] = {
    "radio", "sfx", "recordings", "games",
};

void pipos_levels_default(pipos_levels_t *lv)
{
    int k;

    lv->master = PIPOS_LEVEL_MAX;
    for (k = 0; k < PIPOS_SND_KINDS; k++) {
        lv->level[k] = PIPOS_LEVEL_MAX;
    }
}

static uint32_t cap(unsigned v)
{
    return v > PIPOS_LEVEL_MAX ? PIPOS_LEVEL_MAX : (uint32_t)v;
}

/* The largest product is 100^4 = 10^8, under 2^32 with the 5000 added. */
uint32_t pipos_levels_gain(unsigned master, unsigned level, unsigned trim)
{
    uint32_t l = cap(level);

    return (cap(master) * l * l * cap(trim) + 5000u) / 10000u;
}

uint32_t pipos_levels_gain_of(const pipos_levels_t *lv, int kind, unsigned trim)
{
    if (kind < 0 || kind >= PIPOS_SND_KINDS) {
        kind = PIPOS_SND_SFX;
    }
    return pipos_levels_gain(lv->master, lv->level[kind], trim);
}

int pipos_levels_image_snd(int image_kind)
{
    return image_kind == PIP_KIND_HOLOTAPE ? PIPOS_SND_GAMES : PIPOS_SND_SFX;
}

const char *pipos_levels_key(int kind)
{
    if (kind < 0 || kind >= PIPOS_SND_KINDS) {
        return "unknown";
    }
    return level_keys[kind];
}
