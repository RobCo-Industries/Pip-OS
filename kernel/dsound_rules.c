#include "dsound_rules.h"

static unsigned rnd(struct pipos_dsound *s)
{
    s->rnd = s->rnd * 1103515245u + 12345u;
    return s->rnd >> 16;
}

void pipos_dsound_init(struct pipos_dsound *s)
{
    s->rnd = PIPOS_CLICK_SEED;
    s->owed = 0;
    s->wait = 0;
    s->left = 0;
    s->n = 0;
    s->rom = 0;
    s->open = 0;
}

unsigned pipos_dsound_io_adds(uint32_t count)
{
    return 2u + count / 4u;
}

unsigned pipos_dsound_text_adds(const char *text)
{
    unsigned n = 0;

    for (; *text != '\0'; text++) {
        if (*text >= 0x20 && *text < 0x7f) {
            n++;
        }
    }
    return 1u + n / 6u;
}

/* Callers mask IRQs: the tick callback reads the same state. */
void pipos_dsound_add(struct pipos_dsound *s, unsigned clicks)
{
    unsigned cap = s->rom ? PIPOS_CLICK_CAP_ROM : PIPOS_CLICK_CAP;

    if (!s->open) {
        return; /* gate closed: the work is not owed */
    }
    s->owed = s->owed + clicks > cap ? cap : s->owed + clicks;
}

void pipos_dsound_open(struct pipos_dsound *s)
{
    s->open = 1;
}

void pipos_dsound_close(struct pipos_dsound *s)
{
    s->open = 0;
    s->owed = 0;
    s->left = 0;
}

void pipos_dsound_rom(struct pipos_dsound *s)
{
    s->rom = 1;
    if (s->owed > PIPOS_CLICK_CAP_ROM) {
        s->owed = PIPOS_CLICK_CAP_ROM;
    }
}

int pipos_dsound_tick(struct pipos_dsound *s, int can_play)
{
    static const uint8_t gap[8] = { 3, 6, 4, 7, 5, 8, 6, 7 }; /* ticks */

    if (s->wait > 0) {
        s->wait--;
    }
    if (!s->open || s->owed == 0 || s->wait > 0 || !can_play) {
        return -1;
    }
    if (s->left == 0) {
        s->left = s->rom ? 1 + rnd(s) % 4 : 3 + rnd(s) % 19;
    }
    s->owed--;
    s->left--;
    s->wait = gap[rnd(s) % 8];
    if (s->left == 0 || s->owed == 0) { /* the burst ends: a pause */
        s->left = 0;
        s->wait += s->rom ? 30 + 10 * (rnd(s) % 7) : 15 + 5 * (rnd(s) % 10);
    }
    return (int)(s->n++ % 12);
}
