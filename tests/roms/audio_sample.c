/*
 * The sample call: limits, bad pointers, the copy, replacement in every
 * direction, stop, status and volume. The card sets volume = 70 and
 * beep = no. Every sample is pattern(seed, i) = i * 7 + seed * 31 (mod
 * 256): tests/test_audio_sample.py computes the same bytes and checks the
 * CRC-32 in each op=pcm line. Right after each call that plays, the ROM
 * overwrites the buffer, so a CRC of the original data proves the copy.
 * tests/test_audio_sample.py lists the check names in order.
 */
#include "rt.h"

#define SLOT_END    (PIP_ROM_BASE + PIP_ROM_SIZE)
#define KERNEL_ADDR 0x00008000u
#define MMIO_ADDR   0x20200000u
#define WAIT_MAX_MS 3000u

#define CK(name, expr, expected) rt_check_eq(name, (int32_t)(expr), (int32_t)(expected))

static uint8_t buf[PIP_SAMPLE_MAX + 1];

static void fill(uint32_t seed, uint32_t len)
{
    uint32_t i;

    for (i = 0; i < len; i++) {
        buf[i] = (uint8_t)(i * 7u + seed * 31u);
    }
}

/* Plays buf[0, len) filled with seed, then overwrites it. */
static int32_t play(uint32_t seed, uint32_t len)
{
    int32_t rc;

    fill(seed, len);
    rc = pip_sample(buf, len);
    fill(seed + 100u, len);
    return rc;
}

static const uint8_t *at(uint32_t addr)
{
    return (const uint8_t *)(uintptr_t)addr;
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

static void errors(void)
{
    fill(9, PIP_SAMPLE_MAX + 1);
    CK("len0", pip_sample(buf, 0), -PIP_EINVAL);
    CK("len65537", pip_sample(buf, PIP_SAMPLE_MAX + 1), -PIP_EINVAL);
    CK("len_max_u32", pip_sample(buf, 0xffffffffu), -PIP_EINVAL);
    CK("kernel", pip_sample(at(KERNEL_ADDR), 16), -PIP_EFAULT);
    CK("mmio", pip_sample(at(MMIO_ADDR), 16), -PIP_EFAULT);
    CK("slot_end", pip_sample(at(SLOT_END - 8), 16), -PIP_EFAULT);
    CK("null", pip_sample(at(0), 16), -PIP_EFAULT);
    CK("idle_status", pip_audio_status(), 0);

    /* An error leaves the sound that plays alone. */
    CK("tone_2100", pip_tone(2100, 5000), 0);
    CK("err_len0", pip_sample(buf, 0), -PIP_EINVAL);
    CK("err_kernel", pip_sample(at(KERNEL_ADDR), 16), -PIP_EFAULT);
    CK("err_busy", pip_audio_status(), 1);
    pip_sleep_ms(100);
    CK("err_stop", pip_audio_stop(), 0);
    pip_sleep_ms(20);
}

int pip_main(const pipos_startup_t *startup)
{
    static const pip_note_t note[] = { { 2400, 60 } };
    int32_t ms;

    (void)startup;
    errors();

    /* 800 bytes: 100 ms. */
    CK("s800", play(1, 800), 0);
    CK("s800_busy", pip_audio_status(), 1);
    ms = wait_quiet();
    rt_check("s800_wait", ms >= 90 && ms < (int32_t)WAIT_MAX_MS);
    CK("s800_done", pip_audio_status(), 0);

    CK("s1", play(2, 1), 0);
    ms = wait_quiet();
    rt_check("s1_wait", ms >= 0 && ms < (int32_t)WAIT_MAX_MS);

    /* 65536 bytes: 8.192 s, stopped after about 50 ms. */
    CK("s65536", play(3, PIP_SAMPLE_MAX), 0);
    pip_sleep_ms(50);
    CK("s65536_busy", pip_audio_status(), 1);
    CK("s65536_stop", pip_audio_stop(), 0);
    CK("s65536_quiet", pip_audio_status(), 0);
    pip_sleep_ms(20);

    /* Sample over tone. */
    CK("tone_2200", pip_tone(2200, 5000), 0);
    pip_sleep_ms(50);
    CK("over_tone", play(4, 400), 0);
    ms = wait_quiet();
    rt_check("over_tone_wait", ms >= 40 && ms < (int32_t)WAIT_MAX_MS);

    /* Tone over sample. */
    CK("s4000", play(5, 4000), 0);
    pip_sleep_ms(50);
    CK("tone_2300", pip_tone(2300, 100), 0);
    ms = wait_quiet();
    rt_check("tone_over_wait", ms >= 90 && ms < (int32_t)WAIT_MAX_MS);

    /* Sample over sample: the first is cut, the second plays in full. */
    CK("s8000", play(6, 8000), 0);
    pip_sleep_ms(50);
    CK("over_sample", play(7, 800), 0);
    ms = wait_quiet();
    rt_check("over_sample_wait", ms >= 90 && ms < (int32_t)WAIT_MAX_MS);

    /* play_seq over sample. */
    CK("s8000b", play(8, 8000), 0);
    pip_sleep_ms(50);
    CK("seq_over", pip_play_seq(note, 1), 0);
    ms = wait_quiet();
    rt_check("seq_over_wait", ms >= 50 && ms < (int32_t)WAIT_MAX_MS);

    /* Volume applies to the next sample. */
    CK("vol_40", pip_volume(40), 70);
    CK("s80", play(9, 80), 0);
    ms = wait_quiet();
    rt_check("s80_wait", ms >= 0 && ms < (int32_t)WAIT_MAX_MS);
    CK("vol_back", pip_volume(70), 40);
    rt_checks_done();
    return 0;
}
