/*
 * The audio group: tone, sample, audio_stop, volume, audio_status,
 * play_seq, sample_queue, sample_rate, sound_kind, sound_level and
 * sound_store.
 *
 * One voice. RobCo-Core's audio driver plays in the background from the
 * tick interrupt; a new sound replaces the one that plays. The library
 * takes requests under irq_save(), so the handlers only add the image's
 * own state on top.
 *
 * With the system sound on, the kernel's disk click shares the voice.
 * A call that may start or stop a sound calls cut_click() after its
 * argument checks. It first syncs owned and pcmq with the driver, then
 * cuts: a click that plays stops and no new one
 * starts until the call ends, so the image's sound starts a new play and
 * sample_queue never queues behind a click. Every call ends with
 * pipos_dsound_call_done() and what it did: only a sound that started or
 * stopped restarts the 300 ms of quiet. audio_status and volume cut
 * nothing: owned is 0 whenever a click plays, so audio_status never
 * reports one, and the library moves the gain of a play smoothly, so a
 * click that plays reaches a new level within 20 ms. sample_rate cuts
 * nothing either: a play keeps the rate it started with. sound_kind,
 * sound_level and sound_store are questions too: they start and stop
 * nothing.
 *
 * Every play has a kind, and its gain comes from the master, the level of
 * that kind and, for a holotape's play, the holotape's trim, see levels.h.
 * The kernel sets the gain after the old play is cut and before the new
 * one starts, and remembers the kind of the play it started and whether a
 * holotape started it. A sample queued behind a play keeps that play's
 * gain. tone and play_seq hand the driver a start request that ends the
 * old sound at its next tick: when the new gain differs from the last
 * one, they stop the old sound first, so it never moves to the new gain.
 */
#include <robco/audio.h>
#include <robco/irq.h>

#include "audio_rules.h"
#include "dsound_rules.h"
#include "levels.h"
#include "pipos.h"
#include "sys.h"

/* The master and the four levels of the device. Set by main.c at boot; the library has no getter. */
static pipos_levels_t levels;
/* The kind of the sounds the running image starts, the ROM's kind and the holotape's trim. */
static pipos_image_snd_t img = PIPOS_IMAGE_SND_INIT;
/*
 * The last play started, the image's or the kernel's: its kind, and 1 when
 * a holotape started it. last_gain is the gain the kernel last gave the
 * driver. Written with IRQs masked.
 */
static volatile int play_snd = PIPOS_SND_SFX;
static volatile int play_tape;
static volatile uint32_t last_gain = PIPOS_GAIN_FULL;
/* 1 when audio_init() left a backend that plays sounds. Set once by main.c. */
static int driver_ok;
/* 1 when the boot rest ramp reached its end. Set once by main.c. */
static int rest_mid;
/*
 * 1 after the image started a sound, until it stops or ends. The disk
 * click's tick clears it when it finds the driver idle.
 */
static volatile int owned;
/* The notes the tick plays. Rewritten only with IRQs masked. */
static audio_note_t seq_buf[PIP_SEQ_MAX];
/*
 * Copies of the caller's PCM data. The PCM IRQ reads the one that plays
 * and the one that waits. pcmq says which: see audio_rules.h. sample
 * always uses buffer 0; sample_queue writes only a free buffer.
 */
static uint8_t pcm_buf[2][PIP_SAMPLE_MAX];
static pipos_pcmq_t pcmq = PIPOS_PCMQ_IDLE;
/*
 * The image's sample rate, and the ROM's while a holotape runs. play_rate
 * is the rate the image's PCM play started with. Main context only.
 */
static pipos_rate_t rate = { PIP_SAMPLE_RATE, PIP_SAMPLE_RATE };
static uint32_t play_rate = PIP_SAMPLE_RATE;
/* play_seq's copy of the caller's notes. Main context only. */
static pip_note_t user_notes[PIP_SEQ_MAX];

/* The gain of a play of `snd`, with the holotape's trim when `tape`. */
static uint32_t gain_for(int snd, int tape)
{
    return pipos_levels_gain_of(&levels, snd, tape ? img.trim : PIPOS_LEVEL_MAX);
}

/*
 * Gives the driver the gain of a play of `snd`, started by a holotape when
 * `tape`, and remembers that play. Call it with IRQs masked, or with
 * nothing playing.
 */
static void set_gain(int snd, int tape)
{
    play_snd = snd;
    play_tape = tape;
    last_gain = gain_for(snd, tape);
    audio_set_gain(last_gain);
}

/* The play of the image that calls: its kind in force, and whether it is a holotape. */
static int image_tape(const pipos_sys_ctx_t *ctx)
{
    return ctx->kind == PIP_KIND_HOLOTAPE;
}

/*
 * Before a start request of tone or play_seq, with IRQs masked: a sound of
 * another gain stops first, so the driver never moves it to the new gain.
 * With the same gain the request replaces it as before.
 */
static void stop_other_gain(int snd, int tape)
{
    if (gain_for(snd, tape) != last_gain) {
        audio_stop();
    }
}

/* The level or the trim changed: the play that sounds gets its gain again. */
static void regain(void)
{
    uint32_t s = irq_save();

    set_gain(play_snd, play_tape);
    irq_restore(s);
}

static void stop_owned(void)
{
    if (owned) {
        audio_stop();
        owned = 0;
    }
    pipos_pcmq_drop(&pcmq);
}

/*
 * Brings owned and pcmq up to date against the driver: see
 * pipos_audio_reconcile(). Every handler runs it before it reads them.
 */
static void sync_play(void)
{
    uint32_t s = irq_save();

    owned = pipos_audio_reconcile(owned, &pcmq, audio_busy() != 0, pipos_dsound_click_plays(),
                                  audio_pcm_can_queue());
    irq_restore(s);
}

/*
 * The sync, then the click cut, under one irq_save(): no click starts in
 * between. After it owned and pcmq say what the image plays, and a click
 * that played counts as nothing of the image's.
 */
static void cut_click(void)
{
    uint32_t s = irq_save();

    sync_play();
    pipos_dsound_cut_click();
    irq_restore(s);
}

/* What the call that runs did, for pipos_dsound_call_done(). Main context only. */
static int outcome;

/* A ROM call's end: the click rules learn what it did. */
static int32_t done(int32_t rc)
{
    pipos_dsound_call_done(outcome);
    return rc;
}

static int32_t tone(const pipos_sys_ctx_t *ctx, uint32_t hz, uint32_t ms)
{
    int32_t rc = pipos_audio_check_tone(hz, ms);
    int tape = image_tape(ctx);
    uint32_t s;
    int prev, prev_tape;

    if (rc != 0) {
        return rc;
    }
    cut_click();
    outcome = PIPOS_ACALL_FAILED;
    /* audio_tone() replaces the PCM and drops the waiting buffer. */
    pipos_pcmq_drop(&pcmq);
    s = irq_save();
    prev = play_snd;
    prev_tape = play_tape;
    stop_other_gain(img.snd, tape);
    set_gain(img.snd, tape);
    rc = audio_tone(hz, ms);
    if (rc != 0) {
        set_gain(prev, prev_tape);
    }
    irq_restore(s);
    if (rc != 0) {
        return -PIP_EIO;
    }
    owned = 1;
    outcome = PIPOS_ACALL_STARTED;
    return 0;
}

static int32_t sys_tone(const pipos_sys_ctx_t *ctx, uint32_t hz, uint32_t ms, uint32_t a2,
                        uint32_t a3)
{
    (void)a2, (void)a3;
    outcome = PIPOS_ACALL_REFUSED;
    return done(tone(ctx, hz, ms));
}

/*
 * Stop, copy into buffer 0, start: sample, and sample_queue when no sample
 * plays. The library does not copy PCM data. audio_stop() stops the PCM IRQ from
 * reading pcm_buf before it returns, so the copy that follows never races
 * the old play, and the new play starts only after the copy is complete.
 * Argument errors return before the stop: the current sound plays on.
 * The caller cut a click first.
 */
static int32_t start_pcm(const pipos_sys_ctx_t *ctx, uint32_t data, uint32_t len)
{
    uint32_t hz;
    int32_t rc;

    /* A failure from here on stopped what the image played. */
    outcome = owned && audio_busy() ? PIPOS_ACALL_STOPPED : PIPOS_ACALL_FAILED;
    audio_stop();
    owned = 0;
    pipos_pcmq_drop(&pcmq);
    rc = pipos_user_copy_in(ctx, pcm_buf[0], data, len);
    if (rc != 0) {
        return rc;
    }
    hz = pipos_pcmq_rate(PIPOS_PCMQ_START, rate.rate, play_rate);
    set_gain(img.snd, image_tape(ctx)); /* the old play is cut, the new one not started */
    if (audio_play_pcm(pcm_buf[0], len, hz, AUDIO_PRIORITY) != 0) {
        return -PIP_EIO;
    }
    play_rate = hz;
    pipos_pcmq_started(&pcmq, 0);
    owned = 1;
    outcome = PIPOS_ACALL_STARTED;
    return 0;
}

static int32_t sample(const pipos_sys_ctx_t *ctx, uint32_t data, uint32_t len)
{
    int32_t rc = pipos_audio_check_sample(len);

    if (rc != 0) {
        return rc;
    }
    rc = pipos_user_check_in(ctx, data, len);
    if (rc != 0) {
        return rc;
    }
    cut_click();
    return start_pcm(ctx, data, len);
}

static int32_t sys_sample(const pipos_sys_ctx_t *ctx, uint32_t data, uint32_t len, uint32_t a2,
                          uint32_t a3)
{
    (void)a2, (void)a3;
    outcome = PIPOS_ACALL_REFUSED;
    return done(sample(ctx, data, len));
}

/*
 * sample_queue. The sync, the cut and the plan run under one irq_save(),
 * so the state the plan reads is the image's own play or nothing, never a
 * click, a play that ended or a stale queued buffer. The play may still
 * end after that: then audio_pcm_queue()
 * returns AUDIO_ERR_STATE, both buffers are free, and the data starts as a
 * new play. The copy goes only into a buffer the driver does not read, and
 * the queue call comes after the copy is complete.
 */
static int32_t sample_queue(const pipos_sys_ctx_t *ctx, uint32_t data, uint32_t len)
{
    int32_t rc = pipos_audio_check_sample(len);
    pipos_pcmq_plan_t plan;
    uint32_t s, hz;
    int q;

    if (rc != 0) {
        return rc;
    }
    rc = pipos_user_check_in(ctx, data, len);
    if (rc != 0) {
        return rc;
    }
    s = irq_save();
    cut_click();
    plan = pipos_pcmq_plan(&pcmq, audio_pcm_can_queue());
    irq_restore(s);
    if (plan.action == PIPOS_PCMQ_BUSY) {
        return -PIP_EBUSY;
    }
    if (plan.action == PIPOS_PCMQ_START) {
        return start_pcm(ctx, data, len);
    }
    outcome = PIPOS_ACALL_FAILED;
    rc = pipos_user_copy_in(ctx, pcm_buf[plan.buf], data, len);
    if (rc != 0) {
        return rc;
    }
    /* The driver plays the buffer at the rate of the play it joins: play_rate. */
    q = audio_pcm_queue(pcm_buf[plan.buf], len);
    if (q == 0) {
        pipos_pcmq_queued(&pcmq, plan.buf);
        owned = 1;
        outcome = PIPOS_ACALL_JOINED;
        return 0;
    }
    if (q != AUDIO_ERR_STATE) {
        return -PIP_EIO;
    }
    /* The play ended since the check: nothing reads either buffer. */
    pipos_pcmq_drop(&pcmq);
    hz = pipos_pcmq_rate(PIPOS_PCMQ_START, rate.rate, play_rate);
    set_gain(img.snd, image_tape(ctx)); /* nothing plays: the play ended */
    if (audio_play_pcm(pcm_buf[plan.buf], len, hz, AUDIO_PRIORITY) != 0) {
        return -PIP_EIO;
    }
    play_rate = hz;
    pipos_pcmq_started(&pcmq, plan.buf);
    owned = 1;
    outcome = PIPOS_ACALL_STARTED;
    return 0;
}

static int32_t sys_sample_queue(const pipos_sys_ctx_t *ctx, uint32_t data, uint32_t len,
                                uint32_t a2, uint32_t a3)
{
    (void)a2, (void)a3;
    outcome = PIPOS_ACALL_REFUSED;
    return done(sample_queue(ctx, data, len));
}

/*
 * A stop with no sound of the image playing leaves a kernel click alone.
 * Otherwise it stops the driver as before, which the click rules see as a
 * stop only when the image's sound played. owned is 0 while a click plays.
 */
static int32_t sys_audio_stop(const pipos_sys_ctx_t *ctx, uint32_t a0, uint32_t a1,
                              uint32_t a2, uint32_t a3)
{
    uint32_t s;

    (void)ctx, (void)a0, (void)a1, (void)a2, (void)a3;
    s = irq_save();
    sync_play();
    if (owned) {
        cut_click();
        outcome = PIPOS_ACALL_STOPPED;
        audio_stop();
    } else {
        outcome = PIPOS_ACALL_IDLE;
        if (!pipos_dsound_click_plays()) {
            audio_stop();
        }
    }
    owned = 0;
    pipos_pcmq_drop(&pcmq);
    irq_restore(s);
    return done(0);
}

/*
 * A question cuts no click. A ROM sets the master, a holotape its trim.
 * The new value holds for what plays next, and the play that sounds gets
 * its gain again: the library moves it there smoothly, a click that plays
 * included, and prints op=gain only when the gain changes.
 */
static int32_t sys_volume(const pipos_sys_ctx_t *ctx, uint32_t new_level, uint32_t a1,
                          uint32_t a2, uint32_t a3)
{
    int32_t prev = pipos_levels_volume(&levels, &img, ctx->kind, new_level);

    (void)a1, (void)a2, (void)a3;
    if (prev >= 0) {
        regain();
    }
    outcome = PIPOS_ACALL_ASKED;
    return done(prev);
}

/*
 * sample_rate, ABI 1.5. A question like volume: it cuts no click, starts
 * and stops nothing, and drops no waiting buffer. The rate holds for the
 * next play that starts.
 */
static int32_t sys_sample_rate(const pipos_sys_ctx_t *ctx, uint32_t hz, uint32_t a1,
                               uint32_t a2, uint32_t a3)
{
    (void)ctx, (void)a1, (void)a2, (void)a3;
    outcome = PIPOS_ACALL_ASKED;
    return done(pipos_audio_rate_swap(&rate, hz));
}

/*
 * sound_kind, ABI 1.6. A question like sample_rate: the kind holds for the
 * next play that starts, and a play keeps its own, so no gain changes.
 */
static int32_t sys_sound_kind(const pipos_sys_ctx_t *ctx, uint32_t kind, uint32_t a1,
                              uint32_t a2, uint32_t a3)
{
    (void)a1, (void)a2, (void)a3;
    outcome = PIPOS_ACALL_ASKED;
    return done(pipos_image_snd_swap(&img, ctx->kind, kind));
}

/*
 * sound_level, ABI 1.6. A question: a set changes one level of the device,
 * and the play that sounds gets its gain again, which moves only when the
 * level is the master or the level of its kind.
 */
static int32_t sys_sound_level(const pipos_sys_ctx_t *ctx, uint32_t which, uint32_t value,
                               uint32_t a2, uint32_t a3)
{
    int32_t prev = pipos_levels_swap(&levels, ctx->kind, which, value);

    (void)a2, (void)a3;
    if (prev >= 0 && value != PIPOS_LEVEL_ASK) {
        regain();
    }
    outcome = PIPOS_ACALL_ASKED;
    return done(prev);
}

/*
 * sound_store, ABI 1.6. ROM only. Writes the levels in force to the boot
 * card and changes none. The write goes through the card device of every
 * other card call, so the disk click counts it as card work.
 */
static int32_t sys_sound_store(const pipos_sys_ctx_t *ctx, uint32_t a0, uint32_t a1,
                               uint32_t a2, uint32_t a3)
{
    int32_t rc = -PIP_EPERM;

    (void)a0, (void)a1, (void)a2, (void)a3;
    if (!image_tape(ctx)) {
        rc = pipos_levels_store(&levels);
    }
    outcome = PIPOS_ACALL_ASKED;
    return done(rc);
}

/*
 * A click that plays reads as silence: the tick clears owned each time it
 * finds the driver idle, which it does before it starts a click, and only
 * a call of the image sets owned again.
 */
static int32_t sys_audio_status(const pipos_sys_ctx_t *ctx, uint32_t a0, uint32_t a1,
                                uint32_t a2, uint32_t a3)
{
    int32_t rc;
    uint32_t s;

    (void)ctx, (void)a0, (void)a1, (void)a2, (void)a3;
    s = irq_save(); /* the tick neither clears owned nor starts a click in between */
    sync_play();
    rc = pipos_audio_status_of(owned, audio_busy());
    irq_restore(s);
    outcome = PIPOS_ACALL_ASKED;
    return done(rc);
}

/*
 * The tick reads seq_buf while a sequence plays. The rewrite and the
 * request happen with IRQs masked, and the tick applies a request before
 * it reads the notes, so it never sees a half-written buffer.
 */
static int32_t play_seq(const pipos_sys_ctx_t *ctx, uint32_t notes, uint32_t count)
{
    int32_t rc = pipos_audio_check_count(count);
    int tape = image_tape(ctx);
    uint32_t s;
    int played = 0;
    int prev, prev_tape;

    if (rc != 0) {
        return rc;
    }
    rc = pipos_user_copy_in(ctx, user_notes, notes, count * sizeof(pip_note_t));
    if (rc != 0) {
        return rc;
    }
    s = irq_save();
    rc = pipos_audio_convert(user_notes, count, seq_buf);
    if (rc == 0) {
        cut_click();
        outcome = PIPOS_ACALL_FAILED;
        pipos_pcmq_drop(&pcmq);
        prev = play_snd;
        prev_tape = play_tape;
        stop_other_gain(img.snd, tape);
        set_gain(img.snd, tape);
        played = audio_play(seq_buf, count, AUDIO_PRIORITY);
        if (played != 0) {
            set_gain(prev, prev_tape);
        }
    }
    irq_restore(s);
    if (rc != 0) {
        return rc;
    }
    if (played != 0) {
        return -PIP_EIO;
    }
    owned = 1;
    outcome = PIPOS_ACALL_STARTED;
    return 0;
}

static int32_t sys_play_seq(const pipos_sys_ctx_t *ctx, uint32_t notes, uint32_t count,
                            uint32_t a2, uint32_t a3)
{
    (void)a2, (void)a3;
    outcome = PIPOS_ACALL_REFUSED;
    return done(play_seq(ctx, notes, count));
}

/* PIPOS_SYS_ENTRY(AUDIO, <NAME>, <handler>), one line per served call, in number order. */
static const pipos_sys_entry_t audio_calls[] = {
    PIPOS_SYS_ENTRY(AUDIO, TONE, sys_tone),
    PIPOS_SYS_ENTRY(AUDIO, SAMPLE, sys_sample),
    PIPOS_SYS_ENTRY(AUDIO, AUDIO_STOP, sys_audio_stop),
    PIPOS_SYS_ENTRY(AUDIO, VOLUME, sys_volume),
    PIPOS_SYS_ENTRY(AUDIO, AUDIO_STATUS, sys_audio_status),
    PIPOS_SYS_ENTRY(AUDIO, PLAY_SEQ, sys_play_seq),
    PIPOS_SYS_ENTRY(AUDIO, SAMPLE_QUEUE, sys_sample_queue),
    PIPOS_SYS_ENTRY(AUDIO, SAMPLE_RATE, sys_sample_rate),
    PIPOS_SYS_ENTRY(AUDIO, SOUND_KIND, sys_sound_kind),
    PIPOS_SYS_ENTRY(AUDIO, SOUND_LEVEL, sys_sound_level),
    PIPOS_SYS_ENTRY(AUDIO, SOUND_STORE, sys_sound_store),
};

const pipos_sys_table_t pipos_audio_table = PIPOS_SYS_TABLE(AUDIO, "audio", audio_calls);

void pipos_audio_driver_result(int started)
{
    driver_ok = started != 0;
}

int pipos_audio_driver_ok(void)
{
    return driver_ok;
}

void pipos_audio_rest_result(int done)
{
    rest_mid = done != 0;
}

int pipos_audio_rest_mid(void)
{
    return rest_mid;
}

void pipos_audio_start_levels(unsigned master, const unsigned start[])
{
    int k;

    levels.master = master;
    for (k = 0; k < PIPOS_SND_KINDS; k++) {
        levels.level[k] = start[k];
    }
    set_gain(PIPOS_SND_SFX, 0);
}

/* The play before, as kind | tape << 8, for the undo. */
int pipos_audio_kernel_gain(void)
{
    int prev = play_snd | play_tape << 8;

    set_gain(PIPOS_SND_SFX, 0);
    return prev;
}

void pipos_audio_kernel_gain_undo(int prev, int rc)
{
    if (rc != 0) {
        set_gain(prev & 0xFF, prev >> 8);
    }
}

/* Runs after main.c set the start level: keeps it. */
void pipos_audio_init(void)
{
    owned = 0;
    pipos_pcmq_drop(&pcmq);
}

/*
 * Only a sound an image started is stopped: the boot beep plays on. Every
 * image starts at 8000 Hz, a ROM as sfx and a holotape as games with a
 * trim of 100. A holotape's end gives the ROM its rate and its kind back,
 * and the trim ends with the holotape.
 */
void pipos_audio_image_start(int kind)
{
    stop_owned();
    pipos_audio_rate_image_start(&rate, kind);
    pipos_image_snd_start(&img, kind);
}

void pipos_audio_image_end(int kind)
{
    stop_owned();
    pipos_audio_rate_image_end(&rate, kind);
    pipos_image_snd_end(&img, kind);
}

void pipos_audio_idle(void)
{
    owned = 0;
}

void pipos_audio_poll(void)
{
}
