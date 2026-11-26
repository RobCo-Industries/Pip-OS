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
    s->phase = PIPOS_DS_BOOT_QUIET;
    s->since_jump = 0;
    s->quiet = 0;
    s->held = 0;
    s->in_call = 0;
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

int pipos_dsound_in_rom(const struct pipos_dsound *s)
{
    return s->phase >= PIPOS_DS_ROM_HOLD;
}

/* Callers mask IRQs: the tick callback reads the same state. */
void pipos_dsound_add(struct pipos_dsound *s, unsigned clicks)
{
    unsigned cap = pipos_dsound_in_rom(s) ? PIPOS_CLICK_CAP_ROM : PIPOS_CLICK_CAP;

    if (s->phase == PIPOS_DS_BOOT_QUIET) {
        return; /* the gate is closed: the work is not owed */
    }
    s->owed = s->owed + clicks > cap ? cap : s->owed + clicks;
}

void pipos_dsound_open(struct pipos_dsound *s)
{
    if (s->phase == PIPOS_DS_BOOT_QUIET) {
        s->phase = PIPOS_DS_BOOT_OPEN;
    }
}

void pipos_dsound_close(struct pipos_dsound *s)
{
    if (s->phase == PIPOS_DS_BOOT_OPEN) {
        s->phase = PIPOS_DS_BOOT_QUIET;
        s->owed = 0;
        s->left = 0;
    }
}

static void to_rom(struct pipos_dsound *s, int phase)
{
    if (!pipos_dsound_in_rom(s)) {
        s->quiet = PIPOS_ROM_QUIET_TICKS + 1u; /* no ROM sound yet */
        s->held = 0;
    }
    s->phase = phase;
    if (s->owed > PIPOS_CLICK_CAP_ROM) {
        s->owed = PIPOS_CLICK_CAP_ROM;
    }
    s->since_jump = 0;
}

void pipos_dsound_hold(struct pipos_dsound *s)
{
    to_rom(s, PIPOS_DS_ROM_HOLD);
}

void pipos_dsound_rom(struct pipos_dsound *s)
{
    to_rom(s, PIPOS_DS_ROM_WAIT);
}

void pipos_dsound_rom_end(struct pipos_dsound *s)
{
    s->phase = PIPOS_DS_BOOT_QUIET;
    s->owed = 0;
    s->left = 0;
    s->held = 0;
}

int pipos_dsound_rom_tick(struct pipos_dsound *s, int rom_busy, int click_busy)
{
    int waits_for_rom, k;

    if (s->phase == PIPOS_DS_ROM_HOLD) {
        return -1; /* the jump: no click starts and the 700 ms wait for the anchor */
    }
    if (s->phase == PIPOS_DS_ROM_WAIT && ++s->since_jump > PIPOS_ROM_START_TICKS) {
        s->phase = PIPOS_DS_ROM_RUN;
    }
    if (rom_busy) {
        s->quiet = 0;
    } else if (s->quiet <= PIPOS_ROM_QUIET_TICKS) {
        s->quiet++;
    }
    waits_for_rom = rom_busy || s->quiet <= PIPOS_ROM_QUIET_TICKS;
    /*
     * The drop counts only the ticks a ROM sound blocks a debt in ROM_RUN.
     * Silence does not reset it, so a tune with short gaps still drops a
     * debt that never fits. A click that plays, or no debt, resets it.
     */
    if (s->owed == 0) {
        s->held = 0;
    } else if (rom_busy && s->phase == PIPOS_DS_ROM_RUN && ++s->held > PIPOS_ROM_DROP_TICKS) {
        s->owed = 0; /* a long tune is not followed by a stale burst */
        s->left = 0;
        s->held = 0;
    }
    k = pipos_dsound_tick(s, !waits_for_rom && !click_busy);
    if (k >= 0) {
        s->held = 0;
    }
    return k;
}

int pipos_dsound_cut(struct pipos_dsound *s, int click_on, int busy)
{
    s->in_call = 1;
    return click_on && busy;
}

int pipos_dsound_restarts(int outcome)
{
    return outcome == PIPOS_ACALL_STARTED || outcome == PIPOS_ACALL_STOPPED;
}

void pipos_dsound_call_end(struct pipos_dsound *s, int outcome)
{
    s->in_call = 0;
    if (pipos_dsound_in_rom(s) && pipos_dsound_restarts(outcome)) {
        s->quiet = 0;
    }
}

int pipos_dsound_tick(struct pipos_dsound *s, int can_play)
{
    static const uint8_t gap[8] = { 3, 6, 4, 7, 5, 8, 6, 7 }; /* ticks */

    if (s->wait > 0) {
        s->wait--;
    }
    if ((s->phase != PIPOS_DS_BOOT_OPEN && s->phase != PIPOS_DS_ROM_RUN) || s->owed == 0 ||
        s->wait > 0 || !can_play || s->in_call) {
        return -1;
    }
    if (s->left == 0) {
        s->left = pipos_dsound_in_rom(s) ? 1 + rnd(s) % 4 : 3 + rnd(s) % 19;
    }
    s->owed--;
    s->left--;
    s->wait = gap[rnd(s) % 8];
    if (s->left == 0 || s->owed == 0) { /* the burst ends: a pause */
        s->left = 0;
        s->wait += pipos_dsound_in_rom(s) ? 30 + 10 * (rnd(s) % 7) : 15 + 5 * (rnd(s) % 10);
    }
    return (int)(s->n++ % 12);
}
