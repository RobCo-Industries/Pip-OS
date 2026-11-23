/*
 * The audio calls: results, limits, bad pointers, status, replace, stop,
 * sequences and volume. The card sets volume = 70. Each sound uses its own
 * frequency so tests/test_audio.py can find its trace markers; the
 * frequencies of refused calls must never show up.
 * tests/test_audio.py lists the check names in order.
 */
#include "rt.h"

#define SLOT_END    (PIP_ROM_BASE + PIP_ROM_SIZE)
#define KERNEL_ADDR 0x00008000u
#define MMIO_ADDR   0x20200000u
#define WAIT_MAX_MS 3000u

#define CK(name, expr, expected) rt_check_eq(name, (int32_t)(expr), (int32_t)(expected))

static pip_note_t many[PIP_SEQ_MAX + 1];

static const pip_note_t *at(uint32_t addr)
{
    return (const pip_note_t *)(uintptr_t)addr;
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

static void limits(void)
{
    static const pip_note_t bad_last[][3] = {
        { { 777, 10 }, { 0, 10 }, { 19, 10 } },
        { { 777, 10 }, { 0, 10 }, { 20001, 10 } },
        { { 777, 10 }, { 0, 10 }, { 777, 0 } },
        { { 777, 10 }, { 0, 10 }, { 777, 60001 } },
        { { 777, 10 }, { 0, 10 }, { 0, 0 } },
    };
    pip_note_t edge = { 777, 10 };

    CK("tone_hz19", pip_tone(19, 10), -PIP_EINVAL);
    CK("tone_hz20001", pip_tone(20001, 10), -PIP_EINVAL);
    CK("tone_ms0", pip_tone(777, 0), -PIP_EINVAL);
    CK("tone_ms60001", pip_tone(777, 60001), -PIP_EINVAL);
    CK("volume_101", pip_volume(101), -PIP_EINVAL);
    CK("seq_count0", pip_play_seq(&edge, 0), -PIP_EINVAL);
    CK("seq_bad_hz19", pip_play_seq(bad_last[0], 3), -PIP_EINVAL);
    CK("seq_bad_hz20001", pip_play_seq(bad_last[1], 3), -PIP_EINVAL);
    CK("seq_bad_ms0", pip_play_seq(bad_last[2], 3), -PIP_EINVAL);
    CK("seq_bad_ms60001", pip_play_seq(bad_last[3], 3), -PIP_EINVAL);
    CK("seq_bad_rest0", pip_play_seq(bad_last[4], 3), -PIP_EINVAL);
    CK("seq_kernel", pip_play_seq(at(KERNEL_ADDR), 1), -PIP_EFAULT);
    CK("seq_mmio", pip_play_seq(at(MMIO_ADDR), 1), -PIP_EFAULT);
    CK("seq_slot_end", pip_play_seq(at(SLOT_END - 2), 1), -PIP_EFAULT);
    CK("sample_enosys", pip_sample((const uint8_t *)many, 16), -PIP_ENOSYS);
    CK("idle_status", pip_audio_status(), 0);
    CK("idle_stop", pip_audio_stop(), 0);
}

int pip_main(const pipos_startup_t *startup)
{
    static const pip_note_t tune[] = {
        { 1400, 60 }, { 0, 40 }, { 1500, 60 }, { 1600, 50 },
    };
    int32_t ms;
    uint32_t i;

    (void)startup;
    /* Let the boot beep end so it does not mix with the checks. */
    pip_sleep_ms(300);
    limits();

    CK("vol_start", pip_volume(40), 70);
    CK("tone_1050", pip_tone(1050, 100), 0);
    CK("tone_busy", pip_audio_status(), 1);
    ms = wait_quiet();
    rt_check("tone_wait", ms >= 90 && ms < (int32_t)WAIT_MAX_MS);
    CK("tone_done", pip_audio_status(), 0);

    CK("vol_90", pip_volume(90), 40);
    CK("tone_1100", pip_tone(1100, 5000), 0);
    pip_sleep_ms(50);
    CK("tone_1200", pip_tone(1200, 100), 0);
    ms = wait_quiet();
    rt_check("replace_wait", ms >= 0 && ms < 2000);

    CK("tone_1300", pip_tone(1300, 10000), 0);
    pip_sleep_ms(50);
    CK("stop_rc", pip_audio_stop(), 0);
    CK("stop_status", pip_audio_status(), 0);
    CK("stop_again", pip_audio_stop(), 0);
    /* A request waits for the next tick: let the stop through before the next sound. */
    pip_sleep_ms(20);

    CK("seq_rc", pip_play_seq(tune, 4), 0);
    CK("seq_busy", pip_audio_status(), 1);
    ms = wait_quiet();
    rt_check("seq_wait", ms >= 190 && ms < (int32_t)WAIT_MAX_MS);

    for (i = 0; i <= PIP_SEQ_MAX; i++) {
        many[i] = (pip_note_t){ 1800, 1000 };
    }
    CK("seq_257", pip_play_seq(many, PIP_SEQ_MAX + 1), -PIP_EINVAL);
    CK("seq_257_quiet", pip_audio_status(), 0);
    for (i = 0; i < PIP_SEQ_MAX; i++) {
        many[i] = (pip_note_t){ 1700, 1000 };
    }
    CK("seq_256", pip_play_seq(many, PIP_SEQ_MAX), 0);
    /* The kernel copied the notes: changing them now changes nothing. */
    for (i = 0; i < PIP_SEQ_MAX; i++) {
        many[i].freq_hz = 1800;
    }
    pip_sleep_ms(50);
    CK("seq_256_busy", pip_audio_status(), 1);
    CK("seq_256_stop", pip_audio_stop(), 0);
    pip_sleep_ms(20);
    CK("vol_back", pip_volume(70), 90);
    rt_checks_done();
    return 0;
}
