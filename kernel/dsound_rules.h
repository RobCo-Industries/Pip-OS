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
#define PIPOS_NOTE_TAIL_MS  30u /* no click joins the note this close to its end */
#define PIPOS_QUIET_MS    1000u /* the gate stays closed this long before the scroll */

/* Under a ROM, in 10 ms ticks. A click needs more than each count. */
#define PIPOS_ROM_START_TICKS  70u /* since the jump: 700 ms */
#define PIPOS_ROM_QUIET_TICKS  30u /* since the ROM's last sound: 300 ms */
#define PIPOS_ROM_DROP_TICKS  300u /* a debt that waits this long in a row is dropped: 3 s */

/* What a due click does now. */
#define PIPOS_CLICK_WAIT 0 /* it waits a tick */
#define PIPOS_CLICK_PLAY 1 /* it is its own play */
#define PIPOS_CLICK_MIX  2 /* it mixes over the note */

struct pipos_dsound {
    uint32_t rnd;
    unsigned owed, wait, left, n;
    int rom, open;
    unsigned since_jump; /* ticks since the jump, up to PIPOS_ROM_START_TICKS + 1 */
    unsigned quiet;      /* ticks since a ROM sound, up to PIPOS_ROM_QUIET_TICKS + 1 */
    unsigned held;       /* ticks in a row the debt waited for a ROM sound */
};

/* Seed 1, nothing owed, the gate closed, the boot pace. */
void pipos_dsound_init(struct pipos_dsound *s);

/* What one read or write call of count blocks adds: 2 + count / 4. */
unsigned pipos_dsound_io_adds(uint32_t count);

/* What one boot line written with typing on adds: 1 + printable / 6. */
unsigned pipos_dsound_text_adds(const char *text);

/* Printable characters in text, 0x20 to 0x7e. */
unsigned pipos_dsound_printable(const char *text);

/*
 * The text the note plays over: every fixed boot line from the second on.
 * head and tail are NULL-terminated, the holotape and ROM lines sit between
 * them. *chars gets the printable characters of head[1] on and of tail.
 * *delays gets the line delays after the second line starts: one per later
 * line, the holotape and ROM lines included.
 */
void pipos_dsound_note_text(const char *const head[], const char *const tail[],
                            unsigned *chars, unsigned *delays);

/*
 * 1 when the text outlasts the note:
 * chars * char_ms + delays * line_ms >= note_ms. Typing off gives 0.
 */
int pipos_dsound_note_fits(unsigned chars, unsigned delays, uint32_t char_ms,
                           uint32_t line_ms, uint32_t note_ms);

/*
 * What a due click does. busy is audio_busy(). note is 1 while the kernel's
 * note plays or waits for its tick, left_ms what is left of it. mix_busy is
 * audio_pcm_mix_busy(). An idle voice plays the click. Over the note it
 * mixes while more than PIPOS_NOTE_TAIL_MS remain and no overlay runs.
 * Anything else makes it wait.
 */
int pipos_dsound_way(int busy, int note, uint32_t left_ms, int mix_busy);

/* Adds clicks to the debt, up to the cap. A closed gate adds nothing. */
void pipos_dsound_add(struct pipos_dsound *s, unsigned clicks);

/* Opens the gate. */
void pipos_dsound_open(struct pipos_dsound *s);

/* Closes the gate and drops what is owed. */
void pipos_dsound_close(struct pipos_dsound *s);

/*
 * The jump to a ROM: the ROM pace, and the debt cut to its cap. Nothing
 * plays for PIPOS_ROM_START_TICKS. A closed gate opens after them.
 */
void pipos_dsound_rom(struct pipos_dsound *s);

/*
 * The ROM ended: the boot pace again, nothing owed, the gate open, as on a
 * boot with no ROM after its last line.
 */
void pipos_dsound_rom_end(struct pipos_dsound *s);

/*
 * One 10 ms tick under a ROM. rom_busy is 1 while a sound the ROM started
 * plays, click_busy while a kernel click plays. A click is due only more
 * than PIPOS_ROM_START_TICKS after the jump and more than
 * PIPOS_ROM_QUIET_TICKS after the ROM's last sound, with nothing playing.
 * A debt that waits for a ROM sound more than PIPOS_ROM_DROP_TICKS in a
 * row is dropped. Returns as pipos_dsound_tick().
 */
int pipos_dsound_rom_tick(struct pipos_dsound *s, int rom_busy, int click_busy);

/*
 * A ROM's audio call, IRQs masked. sound is 1 for a call that starts or
 * stops a sound: it counts as a ROM sound for the 300 ms rule. Returns 1
 * when a kernel click plays and must stop: click_on says the last play the
 * kernel started was a click, busy is audio_busy().
 */
int pipos_dsound_yields(struct pipos_dsound *s, int click_on, int busy, int sound);

/*
 * What is left to wait, in ms at now, before the scroll starts. It starts
 * line_ms after the last line ended at line_end, and with quiet set never
 * sooner than PIPOS_QUIET_MS after the gate closed at quiet_at. Times are
 * one clock in ms and may wrap.
 */
uint32_t pipos_dsound_scroll_wait(uint32_t now, uint32_t line_end, uint32_t line_ms,
                                  int quiet, uint32_t quiet_at);

/*
 * One 10 ms tick. can_play is 1 when the voice is free. Returns -1 for
 * nothing, or the click to start now, 0 to 11, in turn.
 */
int pipos_dsound_tick(struct pipos_dsound *s, int can_play);

#endif
