/*
 * The sample_queue call: chains with no gap, -PIP_EBUSY when one waits,
 * the "acts like sample" path, the calls that drop the waiting chunk,
 * underrun, errors and a long chain. The card sets volume = 70 and
 * beep = no. Every chunk is pattern(seed, i) = i * 7 + seed * 31 (mod
 * 256), and the ROM overwrites its buffer right after each call, so the
 * CRC in each op=pcm and op=pcm_next line proves the kernel copied the
 * data. tests/test_audio_queue.py lists the check names and the trace.
 */
#include "rt.h"

#define SLOT_END    (PIP_ROM_BASE + PIP_ROM_SIZE)
#define KERNEL_ADDR 0x00008000u
#define MMIO_ADDR   0x20200000u
#define WAIT_MAX_MS 6000u
#define LONG_N      12u
#define LONG_LEN    2000u

#define CK(name, expr, expected) rt_check_eq(name, (int32_t)(expr), (int32_t)(expected))

static uint8_t buf[PIP_SAMPLE_MAX + 1];

static void fill(uint32_t seed, uint32_t len)
{
    uint32_t i;

    for (i = 0; i < len; i++) {
        buf[i] = (uint8_t)(i * 7u + seed * 31u);
    }
}

/* sample or sample_queue of buf[0, len) filled with seed, then overwrites it. */
static int32_t play(uint32_t seed, uint32_t len)
{
    int32_t rc;

    fill(seed, len);
    rc = pip_sample(buf, len);
    fill(seed + 100u, len);
    return rc;
}

static int32_t queue(uint32_t seed, uint32_t len)
{
    int32_t rc;

    fill(seed, len);
    rc = pip_sample_queue(buf, len);
    fill(seed + 100u, len);
    return rc;
}

/*
 * The longest time between two queue calls of the long chain, from its
 * first call. Under a chunk's length, every chunk found the next queued.
 * Off (gap_last < 0) outside the long chain.
 */
static int32_t gap_last = -1, gap_max;

static void gap_mark(void)
{
    int32_t now;

    if (gap_last >= 0) {
        now = pip_ticks_ms();
        gap_max = now - gap_last > gap_max ? now - gap_last : gap_max;
        gap_last = now;
    }
}

/* Queues until the kernel takes the chunk. The last rc: 0, or another error. */
static int32_t queue_retry(uint32_t seed, uint32_t len, uint32_t *tries)
{
    uint32_t t0 = pip_ticks_ms();
    int32_t rc;

    *tries = 0;
    while ((rc = queue(seed, len)) == -PIP_EBUSY) {
        gap_mark();
        (*tries)++;
        if (pip_ticks_ms() - t0 > WAIT_MAX_MS) {
            break;
        }
        pip_sleep_ms(10);
    }
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
    CK("len0", pip_sample_queue(buf, 0), -PIP_EINVAL);
    CK("len65537", pip_sample_queue(buf, PIP_SAMPLE_MAX + 1), -PIP_EINVAL);
    CK("len_max_u32", pip_sample_queue(buf, 0xffffffffu), -PIP_EINVAL);
    CK("kernel", pip_sample_queue(at(KERNEL_ADDR), 16), -PIP_EFAULT);
    CK("mmio", pip_sample_queue(at(MMIO_ADDR), 16), -PIP_EFAULT);
    CK("slot_end", pip_sample_queue(at(SLOT_END - 8), 16), -PIP_EFAULT);
    CK("null", pip_sample_queue(at(0), 16), -PIP_EFAULT);
    CK("idle_status", pip_audio_status(), 0);
}

/* One plays, one waits: errors and -PIP_EBUSY change nothing; the waiting chunk plays next. */
static void busy(void)
{
    int32_t ms;

    CK("b_play", play(1, 4000), 0);
    CK("b_queue", queue(2, 4000), 0);
    CK("b_busy", queue(3, 800), -PIP_EBUSY);
    CK("b_len0", pip_sample_queue(buf, 0), -PIP_EINVAL);
    CK("b_kernel", pip_sample_queue(at(KERNEL_ADDR), 16), -PIP_EFAULT);
    /* Length, pointer and busy all apply: the length comes first. */
    CK("b_len0_kernel", pip_sample_queue(at(KERNEL_ADDR), 0), -PIP_EINVAL);
    CK("b_len65537", pip_sample_queue(buf, PIP_SAMPLE_MAX + 1), -PIP_EINVAL);
    CK("b_null", pip_sample_queue(at(0), 16), -PIP_EFAULT);
    CK("b_busy_max", queue(4, PIP_SAMPLE_MAX), -PIP_EBUSY);
    CK("b_status", pip_audio_status(), 1);
    ms = wait_quiet();
    rt_check("b_wait", ms >= 900 && ms < (int32_t)WAIT_MAX_MS);
}

/* Three chunks in a row: the first acts like sample, the next two chain. */
static void chain3(void)
{
    uint32_t tries;
    int32_t ms;

    CK("c_first", queue(10, 2400), 0);
    CK("c_second", queue(11, 1600), 0);
    CK("c_status", pip_audio_status(), 1);
    CK("c_third", queue_retry(12, 800, &tries), 0);
    rt_check("c_third_waited", tries >= 1);
    CK("c_status2", pip_audio_status(), 1);
    ms = wait_quiet();
    rt_check("c_wait", ms >= 0 && ms < (int32_t)WAIT_MAX_MS);
    CK("c_done", pip_audio_status(), 0);
}

/* With a tone playing, sample_queue replaces it, as sample does. */
static void over_tone(void)
{
    int32_t ms;

    CK("t_tone", pip_tone(2200, 5000), 0);
    pip_sleep_ms(50);
    CK("t_queue", queue(13, 400), 0);
    ms = wait_quiet();
    rt_check("t_wait", ms >= 40 && ms < (int32_t)WAIT_MAX_MS);
}

/* sample, tone, play_seq and audio_stop drop the waiting chunk with the playing one. */
static void drops(void)
{
    static const pip_note_t note[] = { { 2400, 60 } };
    int32_t ms;

    CK("d1_play", play(20, 8000), 0);
    CK("d1_queue", queue(30, 800), 0);
    pip_sleep_ms(50);
    CK("d1_sample", play(40, 800), 0);
    ms = wait_quiet();
    rt_check("d1_wait", ms >= 90 && ms < (int32_t)WAIT_MAX_MS);

    CK("d2_play", play(21, 8000), 0);
    CK("d2_queue", queue(31, 800), 0);
    pip_sleep_ms(50);
    CK("d2_tone", pip_tone(2300, 100), 0);
    ms = wait_quiet();
    rt_check("d2_wait", ms >= 90 && ms < (int32_t)WAIT_MAX_MS);

    CK("d3_play", play(22, 8000), 0);
    CK("d3_queue", queue(32, 800), 0);
    pip_sleep_ms(50);
    CK("d3_seq", pip_play_seq(note, 1), 0);
    ms = wait_quiet();
    rt_check("d3_wait", ms >= 50 && ms < (int32_t)WAIT_MAX_MS);

    CK("d4_play", play(23, 8000), 0);
    CK("d4_queue", queue(33, 800), 0);
    pip_sleep_ms(50);
    CK("d4_stop", pip_audio_stop(), 0);
    CK("d4_status", pip_audio_status(), 0);
    pip_sleep_ms(1200);
    CK("d4_quiet", pip_audio_status(), 0);
}

/* The sound ends, then a queue call starts a new play. */
static void underrun(void)
{
    int32_t ms;

    CK("u_first", queue(50, 80), 0);
    ms = wait_quiet();
    rt_check("u_wait", ms >= 0 && ms < (int32_t)WAIT_MAX_MS);
    CK("u_second", queue(51, 80), 0);
    ms = wait_quiet();
    rt_check("u_wait2", ms >= 0 && ms < (int32_t)WAIT_MAX_MS);
}

/* LONG_N chunks queued as fast as the kernel takes them. */
static void long_chain(void)
{
    uint32_t i, tries;
    int32_t rc, bad = 0;
    int32_t ms;

    for (i = 0; i < LONG_N; i++) {
        rc = queue_retry(60 + i, LONG_LEN, &tries);
        gap_mark();
        if (i == 0) {
            gap_last = pip_ticks_ms();
        }
        if (rc != 0 && bad == 0) {
            bad = rc;
        }
    }
    gap_last = -1;
    CK("l_all", bad, 0);
    rt_begin("l_gap");
    rt_dec("max_ms", gap_max);
    rt_end();
    ms = wait_quiet();
    rt_check("l_wait", ms >= 0 && ms < (int32_t)WAIT_MAX_MS);
}

int pip_main(const pipos_startup_t *startup)
{
    (void)startup;
    errors();
    busy();
    chain3();
    over_tone();
    drops();
    underrun();
    long_chain();
    rt_checks_done();
    return 0;
}
