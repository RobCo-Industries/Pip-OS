/*
 * The audio group: tone, sample, audio_stop, volume, audio_status,
 * play_seq and sample_queue.
 *
 * One voice. RobCo-Core's audio driver plays in the background from the
 * tick interrupt; a new sound replaces the one that plays. The library
 * takes requests under irq_save(), so the handlers only add the image's
 * own state on top.
 */
#include <robco/audio.h>
#include <robco/irq.h>

#include "audio_rules.h"
#include "pipos.h"
#include "sys.h"

/* The level every call sees; the library has no getter. Set once by main.c at boot. */
static unsigned level;
/* 1 after the image started a tone or a sequence, until it stops or ends. */
static int owned;
/* The notes the tick plays. Rewritten only with IRQs masked. */
static audio_note_t seq_buf[PIP_SEQ_MAX];
/*
 * Copies of the caller's PCM data. The PCM IRQ reads the one that plays
 * and the one that waits. pcmq says which: see audio_rules.h. sample
 * always uses buffer 0; sample_queue writes only a free buffer.
 */
static uint8_t pcm_buf[2][PIP_SAMPLE_MAX];
static pipos_pcmq_t pcmq = PIPOS_PCMQ_IDLE;
/* play_seq's copy of the caller's notes. Main context only. */
static pip_note_t user_notes[PIP_SEQ_MAX];

static void stop_owned(void)
{
    if (owned) {
        audio_stop();
        owned = 0;
    }
    pipos_pcmq_drop(&pcmq);
}

static int32_t sys_tone(const pipos_sys_ctx_t *ctx, uint32_t hz, uint32_t ms, uint32_t a2,
                        uint32_t a3)
{
    int32_t rc = pipos_audio_check_tone(hz, ms);

    (void)ctx, (void)a2, (void)a3;
    if (rc != 0) {
        return rc;
    }
    /* audio_tone() replaces the PCM and drops the waiting buffer. */
    pipos_pcmq_drop(&pcmq);
    if (audio_tone(hz, ms) != 0) {
        return -PIP_EIO;
    }
    owned = 1;
    return 0;
}

/*
 * Stop, copy into buffer 0, start: sample, and sample_queue when no sample
 * plays. The library does not copy PCM data. audio_stop() stops the PCM IRQ from
 * reading pcm_buf before it returns, so the copy that follows never races
 * the old play, and the new play starts only after the copy is complete.
 * Argument errors return before the stop: the current sound plays on.
 */
static int32_t start_pcm(const pipos_sys_ctx_t *ctx, uint32_t data, uint32_t len)
{
    int32_t rc;

    audio_stop();
    owned = 0;
    pipos_pcmq_drop(&pcmq);
    rc = pipos_user_copy_in(ctx, pcm_buf[0], data, len);
    if (rc != 0) {
        return rc;
    }
    if (audio_play_pcm(pcm_buf[0], len, PIP_SAMPLE_RATE, AUDIO_PRIORITY) != 0) {
        return -PIP_EIO;
    }
    pipos_pcmq_started(&pcmq, 0);
    owned = 1;
    return 0;
}

static int32_t sys_sample(const pipos_sys_ctx_t *ctx, uint32_t data, uint32_t len, uint32_t a2,
                          uint32_t a3)
{
    int32_t rc = pipos_audio_check_sample(len);

    (void)a2, (void)a3;
    if (rc != 0) {
        return rc;
    }
    rc = pipos_user_check_in(ctx, data, len);
    if (rc != 0) {
        return rc;
    }
    return start_pcm(ctx, data, len);
}

/*
 * sample_queue. The state is read under one irq_save(), so can_queue and
 * busy agree. The play may still end after that: then audio_pcm_queue()
 * returns AUDIO_ERR_STATE, both buffers are free, and the data starts as a
 * new play. The copy goes only into a buffer the driver does not read, and
 * the queue call comes after the copy is complete.
 */
static int32_t sys_sample_queue(const pipos_sys_ctx_t *ctx, uint32_t data, uint32_t len,
                                uint32_t a2, uint32_t a3)
{
    int32_t rc = pipos_audio_check_sample(len);
    pipos_pcmq_plan_t plan;
    uint32_t s;
    int q;

    (void)a2, (void)a3;
    if (rc != 0) {
        return rc;
    }
    rc = pipos_user_check_in(ctx, data, len);
    if (rc != 0) {
        return rc;
    }
    s = irq_save();
    {
        int can_queue = audio_pcm_can_queue();

        pipos_pcmq_sync(&pcmq, can_queue, audio_busy() != 0);
        plan = pipos_pcmq_plan(&pcmq, can_queue);
    }
    irq_restore(s);
    if (plan.action == PIPOS_PCMQ_BUSY) {
        return -PIP_EBUSY;
    }
    if (plan.action == PIPOS_PCMQ_START) {
        return start_pcm(ctx, data, len);
    }
    rc = pipos_user_copy_in(ctx, pcm_buf[plan.buf], data, len);
    if (rc != 0) {
        return rc;
    }
    q = audio_pcm_queue(pcm_buf[plan.buf], len);
    if (q == 0) {
        pipos_pcmq_queued(&pcmq, plan.buf);
        owned = 1;
        return 0;
    }
    if (q != AUDIO_ERR_STATE) {
        return -PIP_EIO;
    }
    /* The play ended since the check: nothing reads either buffer. */
    pipos_pcmq_drop(&pcmq);
    if (audio_play_pcm(pcm_buf[plan.buf], len, PIP_SAMPLE_RATE, AUDIO_PRIORITY) != 0) {
        return -PIP_EIO;
    }
    pipos_pcmq_started(&pcmq, plan.buf);
    owned = 1;
    return 0;
}

static int32_t sys_audio_stop(const pipos_sys_ctx_t *ctx, uint32_t a0, uint32_t a1,
                              uint32_t a2, uint32_t a3)
{
    (void)ctx, (void)a0, (void)a1, (void)a2, (void)a3;
    audio_stop();
    owned = 0;
    pipos_pcmq_drop(&pcmq);
    return 0;
}

static int32_t sys_volume(const pipos_sys_ctx_t *ctx, uint32_t new_level, uint32_t a1,
                          uint32_t a2, uint32_t a3)
{
    int32_t prev = pipos_audio_volume_swap(&level, new_level);

    (void)ctx, (void)a1, (void)a2, (void)a3;
    if (prev >= 0) {
        audio_set_volume(level);
    }
    return prev;
}

static int32_t sys_audio_status(const pipos_sys_ctx_t *ctx, uint32_t a0, uint32_t a1,
                                uint32_t a2, uint32_t a3)
{
    (void)ctx, (void)a0, (void)a1, (void)a2, (void)a3;
    if (owned && !audio_busy()) {
        owned = 0;
    }
    return owned;
}

/*
 * The tick reads seq_buf while a sequence plays. The rewrite and the
 * request happen with IRQs masked, and the tick applies a request before
 * it reads the notes, so it never sees a half-written buffer.
 */
static int32_t sys_play_seq(const pipos_sys_ctx_t *ctx, uint32_t notes, uint32_t count,
                            uint32_t a2, uint32_t a3)
{
    int32_t rc = pipos_audio_check_count(count);
    uint32_t s;
    int played = 0;

    (void)a2, (void)a3;
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
        pipos_pcmq_drop(&pcmq);
        played = audio_play(seq_buf, count, AUDIO_PRIORITY);
    }
    irq_restore(s);
    if (rc != 0) {
        return rc;
    }
    if (played != 0) {
        return -PIP_EIO;
    }
    owned = 1;
    return 0;
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
};

const pipos_sys_table_t pipos_audio_table = PIPOS_SYS_TABLE(AUDIO, "audio", audio_calls);

void pipos_audio_start_volume(unsigned start)
{
    level = start;
}

/* Runs after main.c set the start level: keeps it. */
void pipos_audio_init(void)
{
    owned = 0;
    pipos_pcmq_drop(&pcmq);
}

/* Only a sound an image started is stopped: the boot beep plays on. */
void pipos_audio_image_start(int kind)
{
    (void)kind;
    stop_owned();
}

void pipos_audio_image_end(int kind)
{
    (void)kind;
    stop_owned();
}

void pipos_audio_poll(void)
{
}
