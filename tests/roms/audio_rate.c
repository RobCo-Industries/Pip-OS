/*
 * The sample_rate call of ABI 1.5: the two rates, a bad value, the rate
 * a play takes when it starts, and the rate per image. The card sets
 * volume = 70 and beep = no and holds the test holotape as TAPE.HTP. Every
 * sample is pattern(seed, i) = i * 7 + seed * 31 (mod 256), as in
 * audio_sample.c. tests/test_audio_rate.py lists the check names in order.
 */
#include "rt.h"

#define WAIT_MAX_MS 3000u
#define SAME_LEN    3750u   /* 468.75 ms at 8000 Hz, 100 ms at 37500 Hz */

#define CK(name, expr, expected) rt_check_eq(name, (int32_t)(expr), (int32_t)(expected))

static uint8_t buf[PIP_SAMPLE_MAX];

static void fill(uint32_t seed, uint32_t len)
{
    uint32_t i;

    for (i = 0; i < len; i++) {
        buf[i] = (uint8_t)(i * 7u + seed * 31u);
    }
}

/* Waits until audio_status is 0. Returns the ms it took, or -1 after WAIT_MAX_MS. */
static int32_t wait_quiet(void)
{
    uint32_t t0 = pip_ticks_ms();

    while (pip_audio_status() == 1) {
        if (pip_ticks_ms() - t0 > WAIT_MAX_MS) {
            return -1;
        }
        pip_sleep_ms(5);
    }
    return (int32_t)(pip_ticks_ms() - t0);
}

static void quiet(const char *name)
{
    int32_t ms = wait_quiet();

    rt_check(name, ms >= 0);
}

int pip_main(const pipos_startup_t *startup)
{
    (void)startup;

    /* The image starts at 8000 Hz. */
    CK("start_8000", pip_sample_rate(8000), 8000);
    CK("bad_0", pip_sample_rate(0), -PIP_EINVAL);
    CK("bad_44100", pip_sample_rate(44100), -PIP_EINVAL);
    CK("bad_37501", pip_sample_rate(37501), -PIP_EINVAL);
    CK("bad_keeps", pip_sample_rate(8000), 8000);

    /* The same buffer at 8000 Hz, then at 37500 Hz. */
    fill(1, SAME_LEN);
    CK("slow", pip_sample(buf, SAME_LEN), 0);
    quiet("slow_wait");
    CK("to_hi", pip_sample_rate(37500), 8000);
    CK("fast", pip_sample(buf, SAME_LEN), 0);
    quiet("fast_wait");

    /*
     * A play keeps its rate: it starts at 8000, the rate goes to 37500
     * while it plays, and the queued buffer joins at 8000.
     */
    CK("to_lo", pip_sample_rate(8000), 37500);
    fill(2, 1600);
    CK("q_first", pip_sample_queue(buf, 1600), 0);
    CK("q_to_hi", pip_sample_rate(37500), 8000);
    CK("q_playing", pip_audio_status(), 1);
    fill(3, 800);
    CK("q_joins", pip_sample_queue(buf, 800), 0);
    quiet("q_wait");
    /* The next new play takes the rate in force. */
    fill(4, SAME_LEN);
    CK("q_new", pip_sample_queue(buf, SAME_LEN), 0);
    quiet("q_new_wait");

    /* A tape starts at 8000 Hz; the ROM gets its 37500 back after it. */
    CK("tape", pip_holo_launch("TAPE.HTP", "rate"), 80);
    CK("back_hi", pip_sample_rate(37500), 37500);
    fill(5, SAME_LEN);
    CK("after_tape", pip_sample(buf, SAME_LEN), 0);
    quiet("after_tape_wait");
    rt_checks_done();
    return 0;
}
