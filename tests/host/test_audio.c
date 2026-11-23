/* audio_rules.c: every limit at its edge, the note conversion, the volume swap. */
#include <stdint.h>
#include <string.h>

#include "audio_rules.h"
#include "rc_test.h"

TEST(tone_limits)
{
    CHECK_EQ(pipos_audio_check_tone(20, 1), 0);
    CHECK_EQ(pipos_audio_check_tone(20000, 60000), 0);
    CHECK_EQ(pipos_audio_check_tone(19, 100), -PIP_EINVAL);
    CHECK_EQ(pipos_audio_check_tone(20001, 100), -PIP_EINVAL);
    CHECK_EQ(pipos_audio_check_tone(0, 100), -PIP_EINVAL);
    CHECK_EQ(pipos_audio_check_tone(440, 0), -PIP_EINVAL);
    CHECK_EQ(pipos_audio_check_tone(440, 60001), -PIP_EINVAL);
    CHECK_EQ(pipos_audio_check_tone(0xffffffffu, 0xffffffffu), -PIP_EINVAL);
}

TEST(sample_limits)
{
    CHECK_EQ(pipos_audio_check_sample(0), -PIP_EINVAL);
    CHECK_EQ(pipos_audio_check_sample(1), 0);
    CHECK_EQ(pipos_audio_check_sample(65536), 0);
    CHECK_EQ(pipos_audio_check_sample(65537), -PIP_EINVAL);
    CHECK_EQ(pipos_audio_check_sample(0xffffffffu), -PIP_EINVAL);
}

TEST(count_limits)
{
    CHECK_EQ(pipos_audio_check_count(0), -PIP_EINVAL);
    CHECK_EQ(pipos_audio_check_count(1), 0);
    CHECK_EQ(pipos_audio_check_count(256), 0);
    CHECK_EQ(pipos_audio_check_count(257), -PIP_EINVAL);
    CHECK_EQ(pipos_audio_check_count(0xffffffffu), -PIP_EINVAL);
}

TEST(note_limits)
{
    pip_note_t n;

    n = (pip_note_t){ 0, 1 };
    CHECK_EQ(pipos_audio_check_note(&n), 0);
    n = (pip_note_t){ 20, 60000 };
    CHECK_EQ(pipos_audio_check_note(&n), 0);
    n = (pip_note_t){ 20000, 1 };
    CHECK_EQ(pipos_audio_check_note(&n), 0);
    n = (pip_note_t){ 19, 100 };
    CHECK_EQ(pipos_audio_check_note(&n), -PIP_EINVAL);
    n = (pip_note_t){ 1, 100 };
    CHECK_EQ(pipos_audio_check_note(&n), -PIP_EINVAL);
    n = (pip_note_t){ 20001, 100 };
    CHECK_EQ(pipos_audio_check_note(&n), -PIP_EINVAL);
    n = (pip_note_t){ 440, 0 };
    CHECK_EQ(pipos_audio_check_note(&n), -PIP_EINVAL);
    n = (pip_note_t){ 0, 0 };
    CHECK_EQ(pipos_audio_check_note(&n), -PIP_EINVAL);
    n = (pip_note_t){ 440, 60001 };
    CHECK_EQ(pipos_audio_check_note(&n), -PIP_EINVAL);
    n = (pip_note_t){ 440, 65535 };
    CHECK_EQ(pipos_audio_check_note(&n), -PIP_EINVAL);
}

TEST(convert_notes)
{
    pip_note_t in[3] = { { 440, 100 }, { 0, 50 }, { 20000, 60000 } };
    audio_note_t out[3];

    CHECK_EQ(pipos_audio_convert(in, 3, out), 0);
    CHECK_EQ(out[0].hz, 440);
    CHECK_EQ(out[0].ms, 100);
    CHECK_EQ(out[1].hz, 0);
    CHECK_EQ(out[1].ms, 50);
    CHECK_EQ(out[2].hz, 20000);
    CHECK_EQ(out[2].ms, 60000);
}

TEST(convert_256)
{
    static pip_note_t in[257];
    static audio_note_t out[257];
    unsigned i;

    for (i = 0; i < 257; i++) {
        in[i] = (pip_note_t){ (uint16_t)(100 + i), (uint16_t)(1 + i) };
    }
    CHECK_EQ(pipos_audio_convert(in, 256, out), 0);
    CHECK_EQ(out[255].hz, 355);
    CHECK_EQ(out[255].ms, 256);
    CHECK_EQ(pipos_audio_convert(in, 257, out), -PIP_EINVAL);
    CHECK_EQ(pipos_audio_convert(in, 0, out), -PIP_EINVAL);
}

/* A bad last note refuses the whole sequence and writes nothing. */
TEST(convert_bad_last_note)
{
    pip_note_t in[4] = { { 440, 100 }, { 0, 50 }, { 880, 10 }, { 440, 0 } };
    audio_note_t out[4];
    audio_note_t keep[4];

    memset(out, 0xa5, sizeof(out));
    memcpy(keep, out, sizeof(out));
    CHECK_EQ(pipos_audio_convert(in, 4, out), -PIP_EINVAL);
    CHECK_MEM(out, keep, sizeof(out));
    in[3] = (pip_note_t){ 19, 10 };
    CHECK_EQ(pipos_audio_convert(in, 4, out), -PIP_EINVAL);
    CHECK_MEM(out, keep, sizeof(out));
    in[3] = (pip_note_t){ 20, 10 };
    CHECK_EQ(pipos_audio_convert(in, 4, out), 0);
}

TEST(volume_swap)
{
    unsigned level = 70;

    CHECK_EQ(pipos_audio_volume_swap(&level, 40), 70);
    CHECK_EQ(level, 40);
    CHECK_EQ(pipos_audio_volume_swap(&level, 100), 40);
    CHECK_EQ(pipos_audio_volume_swap(&level, 101), -PIP_EINVAL);
    CHECK_EQ(level, 100);
    CHECK_EQ(pipos_audio_volume_swap(&level, 0xffffffffu), -PIP_EINVAL);
    CHECK_EQ(pipos_audio_volume_swap(&level, 0), 100);
    CHECK_EQ(pipos_audio_volume_swap(&level, 0), 0);
    CHECK_EQ(level, 0);
}

/* sample_queue's buffer state: every transition of audio_rules.h. */
#define PLAY_WAIT(q, p, w) do { CHECK_EQ((q).play, (p)); CHECK_EQ((q).wait, (w)); } while (0)

static pipos_pcmq_t q_state(int play, int wait)
{
    pipos_pcmq_t q = { play, wait };

    return q;
}

TEST(pcmq_idle_starts)
{
    pipos_pcmq_t q = PIPOS_PCMQ_IDLE;
    pipos_pcmq_plan_t p;

    /* Silence, or a tone or a sequence (busy, nothing to queue behind). */
    pipos_pcmq_sync(&q, 0, 0);
    p = pipos_pcmq_plan(&q, 0);
    CHECK_EQ(p.action, PIPOS_PCMQ_START);
    CHECK_EQ(p.buf, 0);
    pipos_pcmq_sync(&q, 0, 1);
    p = pipos_pcmq_plan(&q, 0);
    CHECK_EQ(p.action, PIPOS_PCMQ_START);
    PLAY_WAIT(q, -1, -1);
    pipos_pcmq_started(&q, 0);
    PLAY_WAIT(q, 0, -1);
}

TEST(pcmq_queue_while_playing)
{
    pipos_pcmq_t q;
    pipos_pcmq_plan_t p;
    int b;

    for (b = 0; b < 2; b++) {
        q = q_state(b, -1);
        pipos_pcmq_sync(&q, 1, 1);
        PLAY_WAIT(q, b, -1);
        p = pipos_pcmq_plan(&q, 1);
        CHECK_EQ(p.action, PIPOS_PCMQ_QUEUE);
        CHECK_EQ(p.buf, 1 - b); /* never the buffer that plays */
        pipos_pcmq_queued(&q, p.buf);
        PLAY_WAIT(q, b, 1 - b);
    }
}

TEST(pcmq_busy_while_one_waits)
{
    pipos_pcmq_t q = q_state(0, 1);
    pipos_pcmq_plan_t p;

    pipos_pcmq_sync(&q, 0, 1);
    PLAY_WAIT(q, 0, 1);
    p = pipos_pcmq_plan(&q, 0);
    CHECK_EQ(p.action, PIPOS_PCMQ_BUSY);
    PLAY_WAIT(q, 0, 1);
}

TEST(pcmq_switch_frees_old_buffer)
{
    pipos_pcmq_t q = q_state(0, 1);
    pipos_pcmq_plan_t p;

    pipos_pcmq_sync(&q, 1, 1); /* buffer 1 took over */
    PLAY_WAIT(q, 1, -1);
    p = pipos_pcmq_plan(&q, 1);
    CHECK_EQ(p.action, PIPOS_PCMQ_QUEUE);
    CHECK_EQ(p.buf, 0);
    q = q_state(1, 0);
    pipos_pcmq_sync(&q, 1, 1);
    PLAY_WAIT(q, 0, -1);
    CHECK_EQ(pipos_pcmq_plan(&q, 1).buf, 1);
}

TEST(pcmq_underrun_acts_as_sample)
{
    pipos_pcmq_t q;
    pipos_pcmq_plan_t p;

    q = q_state(1, -1); /* the last chunk ended */
    pipos_pcmq_sync(&q, 0, 0);
    PLAY_WAIT(q, -1, -1);
    p = pipos_pcmq_plan(&q, 0);
    CHECK_EQ(p.action, PIPOS_PCMQ_START);
    CHECK_EQ(p.buf, 0);
    q = q_state(0, 1); /* both chunks ended */
    pipos_pcmq_sync(&q, 0, 0);
    PLAY_WAIT(q, -1, -1);
    CHECK_EQ(pipos_pcmq_plan(&q, 0).action, PIPOS_PCMQ_START);
    /* Nothing waits, but the driver cannot queue: start, never copy beside it. */
    q = q_state(0, -1);
    pipos_pcmq_sync(&q, 0, 1);
    CHECK_EQ(pipos_pcmq_plan(&q, 0).action, PIPOS_PCMQ_START);
}

TEST(pcmq_ended_between_check_and_queue)
{
    pipos_pcmq_t q = q_state(0, -1);
    pipos_pcmq_plan_t p;

    pipos_pcmq_sync(&q, 1, 1);
    p = pipos_pcmq_plan(&q, 1);
    CHECK_EQ(p.buf, 1);
    /* audio_pcm_queue() said AUDIO_ERR_STATE: the kernel starts buffer 1. */
    pipos_pcmq_drop(&q);
    pipos_pcmq_started(&q, p.buf);
    PLAY_WAIT(q, 1, -1);
    CHECK_EQ(pipos_pcmq_plan(&q, 1).buf, 0);
}

TEST(pcmq_drop_in_every_state)
{
    static const int states[][2] = { { -1, -1 }, { 0, -1 }, { 1, -1 }, { 0, 1 }, { 1, 0 } };
    unsigned i;

    /* tone, sample, play_seq, audio_stop and the image hooks. */
    for (i = 0; i < sizeof(states) / sizeof(states[0]); i++) {
        pipos_pcmq_t q = q_state(states[i][0], states[i][1]);

        pipos_pcmq_drop(&q);
        PLAY_WAIT(q, -1, -1);
        CHECK_EQ(pipos_pcmq_plan(&q, 1).action, PIPOS_PCMQ_START);
        /* sample after the drop: buffer 0 plays, nothing waits. */
        pipos_pcmq_started(&q, 0);
        PLAY_WAIT(q, 0, -1);
    }
}
