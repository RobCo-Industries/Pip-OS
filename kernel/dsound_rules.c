#include <stddef.h>

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

unsigned pipos_dsound_printable(const char *text)
{
    unsigned n = 0;

    for (; *text != '\0'; text++) {
        if (*text >= 0x20 && *text < 0x7f) {
            n++;
        }
    }
    return n;
}

unsigned pipos_dsound_text_adds(const char *text)
{
    return 1u + pipos_dsound_printable(text) / 6u;
}

void pipos_dsound_note_text(const char *const head[], const char *const tail[],
                            unsigned *chars, unsigned *delays)
{
    unsigned i;

    *chars = 0;
    *delays = 2; /* the holotape and ROM lines */
    for (i = 1; head[0] != NULL && head[i] != NULL; i++) {
        *chars += pipos_dsound_printable(head[i]);
        if (i > 1) {
            (*delays)++;
        }
    }
    for (i = 0; tail[i] != NULL; i++) {
        *chars += pipos_dsound_printable(tail[i]);
        (*delays)++;
    }
}

int pipos_dsound_note_fits(unsigned chars, unsigned delays, uint32_t char_ms,
                           uint32_t line_ms, uint32_t note_ms)
{
    uint64_t text = (uint64_t)chars * char_ms + (uint64_t)delays * line_ms;

    return char_ms > 0 && text >= note_ms;
}

/* ms from now to at, 0 when at has passed. */
static uint32_t until(uint32_t now, uint32_t at)
{
    int32_t left = (int32_t)(at - now);

    return left > 0 ? (uint32_t)left : 0u;
}

uint32_t pipos_dsound_scroll_wait(uint32_t now, uint32_t line_end, uint32_t line_ms,
                                  int quiet, uint32_t quiet_at)
{
    uint32_t a = until(now, line_end + line_ms);
    uint32_t b = quiet ? until(now, quiet_at + PIPOS_QUIET_MS) : 0u;

    return a > b ? a : b;
}

int pipos_dsound_way(int busy, int note, uint32_t left_ms, int mix_busy)
{
    if (!busy) {
        return PIPOS_CLICK_PLAY;
    }
    if (note && left_ms > PIPOS_NOTE_TAIL_MS && !mix_busy) {
        return PIPOS_CLICK_MIX;
    }
    return PIPOS_CLICK_WAIT;
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
