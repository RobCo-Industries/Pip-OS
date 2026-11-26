/*
 * The audio group's argument rules and the note conversion. Pure code: it
 * builds and runs on the host in the unit tests.
 */
#ifndef PIPOS_AUDIO_RULES_H
#define PIPOS_AUDIO_RULES_H

#include <stddef.h>
#include <stdint.h>

#include <pipos/abi.h>
#include <robco/audio.h>

/* 0, or -PIP_EINVAL unless hz is 20 to 20000 and ms is 1 to 60000. */
int32_t pipos_audio_check_tone(uint32_t hz, uint32_t ms);

/* sample and sample_queue: 0, or -PIP_EINVAL unless len is 1 to PIP_SAMPLE_MAX (65536) bytes. */
int32_t pipos_audio_check_sample(uint32_t len);

/* 0, or -PIP_EINVAL unless count is 1 to 256. */
int32_t pipos_audio_check_count(uint32_t count);

/* 0, or -PIP_EINVAL unless hz is 0 (a rest) or 20 to 20000 and ms is 1 to 60000. */
int32_t pipos_audio_check_note(const pip_note_t *n);

/*
 * Checks all `count` notes, then converts them into out[count]. On a bad
 * note it returns -PIP_EINVAL and leaves out untouched. count is checked
 * as in pipos_audio_check_count().
 */
int32_t pipos_audio_convert(const pip_note_t *in, uint32_t count, audio_note_t *out);

/*
 * Sets *level to `level` and returns the previous one, or returns
 * -PIP_EINVAL and keeps *level when level is above 100.
 */
int32_t pipos_audio_volume_swap(unsigned *level, uint32_t new_level);

/*
 * sample_queue's two PCM buffers, 0 and 1. The driver does not copy, so
 * the kernel must know which buffer it may still read:
 *   play  the buffer that plays, or -1 when no sample of the image plays;
 *   wait  the buffer queued behind it, or -1;
 *   free  a buffer that is neither: the only one a copy may write.
 *
 * pipos_pcmq_sync() first brings the state up to date from two driver
 * facts read under one irq_save(): can_queue = audio_pcm_can_queue() and
 * busy = audio_busy(). Both come from the driver, so they cannot go stale:
 *   busy 0                 the sound ended: both free (idle);
 *   wait set, can_queue 1  the waiting buffer took over: play = wait, the
 *                          old one is free (the driver's lifetime rule).
 * Then pipos_pcmq_plan() picks the action:
 *   wait set               BUSY: nothing changes;
 *   play set, can_queue 1  QUEUE: copy into the free buffer, then queue it;
 *   otherwise              START as sample: stop, copy into buffer 0, start.
 * The caller then reports what the driver did: pipos_pcmq_queued() sets
 * wait, pipos_pcmq_started() sets play and clears wait. tone, sample's
 * stop, play_seq, audio_stop and the image hooks call pipos_pcmq_drop().
 */
typedef struct {
    int play;
    int wait;
} pipos_pcmq_t;

#define PIPOS_PCMQ_IDLE { -1, -1 }

enum { PIPOS_PCMQ_BUSY, PIPOS_PCMQ_QUEUE, PIPOS_PCMQ_START };

typedef struct {
    int action; /* PIPOS_PCMQ_* */
    int buf;    /* the buffer to copy into, for QUEUE and START */
} pipos_pcmq_plan_t;

void pipos_pcmq_sync(pipos_pcmq_t *q, int can_queue, int busy);
pipos_pcmq_plan_t pipos_pcmq_plan(const pipos_pcmq_t *q, int can_queue);
void pipos_pcmq_queued(pipos_pcmq_t *q, int buf);
void pipos_pcmq_started(pipos_pcmq_t *q, int buf);
void pipos_pcmq_drop(pipos_pcmq_t *q);

/*
 * Brings the state the handlers keep about the image's own play up to
 * date against the driver, in one place, before a click is cut or a
 * question is answered. owned is 1 after the image started a sound,
 * busy is audio_busy(), click is 1 while a kernel click plays, can_queue
 * is audio_pcm_can_queue(), all read under one irq_save(). Returns the new
 * owned. The image's play goes on only when owned and busy hold and no
 * click plays: then the queue syncs as in pipos_pcmq_sync(). Anything else
 * means nothing of the image plays: a play that ended, long ago or one
 * tick ago, a click that plays, or the pending stop of a click just cut.
 * Then owned is 0 and both buffers are free, so a cut leaves the state
 * "the image has nothing playing" and sample_queue starts a new play.
 */
int pipos_audio_reconcile(int owned, pipos_pcmq_t *q, int busy, int click, int can_queue);

/*
 * audio_status: owned is 1 after the image started a sound, busy is
 * audio_busy(). Returns the new owned, which is also the answer: 0 once
 * the driver went idle. The disk click's tick clears owned when it finds
 * the driver idle, before it starts a click, so a click reads as silence.
 */
int pipos_audio_status_of(int owned, int busy);

#endif
