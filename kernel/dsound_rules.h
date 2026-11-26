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
#define PIPOS_ROM_DROP_TICKS  300u /* ROM sound over a debt, in total: 3 s */

/* What a due click does now. */
#define PIPOS_CLICK_WAIT 0 /* it waits a tick */
#define PIPOS_CLICK_PLAY 1 /* it is its own play */
#define PIPOS_CLICK_MIX  2 /* it mixes over the note */

/*
 * Where the clicks stand. Each phase says alone whether work is owed and
 * whether a click may play:
 *   BOOT_OPEN   the boot pace: work is owed, up to PIPOS_CLICK_CAP, clicks play;
 *   BOOT_QUIET  the gate is closed: the note starts or the quiet second runs,
 *               work is not owed, nothing plays;
 *   ROM_HOLD    the jump: a click that plays ends, none starts, the 700 ms
 *               have not started;
 *   ROM_WAIT    the 700 ms after the jump: work is owed, nothing plays;
 *   ROM_RUN     the ROM pace: work is owed, clicks play when the ROM is quiet.
 * In every ROM phase work is owed up to PIPOS_CLICK_CAP_ROM, whatever the
 * boot's phase was at the jump. A ROM sound only holds what is owed.
 */
enum {
    PIPOS_DS_BOOT_OPEN,
    PIPOS_DS_BOOT_QUIET,
    PIPOS_DS_ROM_HOLD,
    PIPOS_DS_ROM_WAIT,
    PIPOS_DS_ROM_RUN,
};

struct pipos_dsound {
    uint32_t rnd;
    unsigned owed, wait, left, n;
    int phase;           /* a PIPOS_DS_ value */
    unsigned since_jump; /* ticks of ROM_WAIT, up to PIPOS_ROM_START_TICKS + 1 */
    unsigned quiet;      /* ticks since a ROM sound, up to PIPOS_ROM_QUIET_TICKS + 1 */
    unsigned held;       /* ROM_RUN ticks a ROM sound played over a debt since a click */
    int in_call;         /* an image's audio call runs between its cut and its end */
};

/* What an image's audio call did. Only STARTED and STOPPED restart the 300 ms. */
#define PIPOS_ACALL_REFUSED 0 /* refused before the cut: an argument, a pointer, EBUSY */
#define PIPOS_ACALL_FAILED  1 /* EFAULT or EIO after the cut: no ROM sound started or stopped */
#define PIPOS_ACALL_STARTED 2 /* a ROM sound started */
#define PIPOS_ACALL_STOPPED 3 /* a ROM sound that played stopped, and none started */
#define PIPOS_ACALL_JOINED  4 /* sample_queue queued behind the ROM's own play */
#define PIPOS_ACALL_IDLE    5 /* audio_stop with no ROM sound playing */
#define PIPOS_ACALL_ASKED   6 /* audio_status and volume: a question, no cut */

/* Seed 1, nothing owed, BOOT_QUIET. */
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

/* 1 in the ROM phases. */
int pipos_dsound_in_rom(const struct pipos_dsound *s);

/* Adds clicks to the debt, up to the phase's cap. BOOT_QUIET adds nothing. */
void pipos_dsound_add(struct pipos_dsound *s, unsigned clicks);

/* BOOT_QUIET to BOOT_OPEN. Does nothing in a ROM phase. */
void pipos_dsound_open(struct pipos_dsound *s);

/* BOOT_OPEN to BOOT_QUIET: what is owed is dropped. Does nothing in a ROM phase. */
void pipos_dsound_close(struct pipos_dsound *s);

/*
 * The jump starts: ROM_HOLD. The debt is cut to the ROM cap and no click
 * starts. The caller then lets a click that plays end.
 */
void pipos_dsound_hold(struct pipos_dsound *s);

/*
 * The 700 ms start: ROM_WAIT, from ROM_HOLD or a boot phase. The debt is
 * cut to the ROM cap. Nothing plays for PIPOS_ROM_START_TICKS, then ROM_RUN.
 */
void pipos_dsound_rom(struct pipos_dsound *s);

/*
 * The ROM returned: BOOT_QUIET with nothing owed, so the kernel's work
 * after it owes nothing. pipos_dsound_open() then gives the boot pace, as
 * on a boot with no ROM after its last line.
 */
void pipos_dsound_rom_end(struct pipos_dsound *s);

/*
 * One 10 ms tick under a ROM. rom_busy is 1 while a sound the ROM started
 * plays, click_busy while a kernel click plays. A click is due only in
 * ROM_RUN, more than PIPOS_ROM_START_TICKS after the anchor, and more than
 * PIPOS_ROM_QUIET_TICKS after the ROM's last sound, with nothing playing.
 * The debt is dropped once ROM sounds have played over it for more than
 * PIPOS_ROM_DROP_TICKS of ROM_RUN in total with no click played. The
 * 300 ms after a sound, the 700 ms after the jump and silence do not count.
 * A click that plays or a debt of zero starts the count again. Returns as
 * pipos_dsound_tick().
 */
int pipos_dsound_rom_tick(struct pipos_dsound *s, int rom_busy, int click_busy);

/*
 * An image's audio call is about to start or stop a sound, IRQs masked.
 * No click starts until pipos_dsound_call_end(). Returns 1 when a kernel
 * click plays and must stop: click_on says the last play the kernel
 * started was a click, busy is audio_busy().
 */
int pipos_dsound_cut(struct pipos_dsound *s, int click_on, int busy);

/* 1 when the outcome, a PIPOS_ACALL_ value, restarts the 300 ms. */
int pipos_dsound_restarts(int outcome);

/*
 * The end of an image's audio call, IRQs masked: clicks may start again.
 * Under a ROM a sound that started or stopped restarts the 300 ms.
 */
void pipos_dsound_call_end(struct pipos_dsound *s, int outcome);

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
