/*
 * PCM streaming while every frame redraws the whole screen, for
 * tests/test_tft.py. CHUNKS chunks of CHUNK_LEN samples at 8000 Hz,
 * 8 s each, are queued as the kernel takes them. A chunk must outlast one
 * present: about 128 ms on a panel at 10 MHz. In QEMU's raspi3b it took
 * 3 s on a Mac and 8.8 s on a GitHub runner while cores 1 to 3 spun in an
 * exception loop; the test parks them, and a present then takes about
 * 0.4 s on a Mac. See tests/hosttiming.py. Between queue calls
 * the ROM clears the screen to another colour and presents it, so each
 * present sends a full frame to the panel. The audio trace must show one
 * op=pcm, CHUNKS - 1 op=pcm_next and one op=pcm_end: no underrun.
 *
 * max_gap_ms is the longest time between two queue calls, from the start
 * call to the last queue. When it is shorter than a chunk, every chunk
 * found the next one queued. QEMU's guest clock follows the host, so a
 * stalled host shows up here, and the test cannot judge the run then.
 */
#include "rt.h"

#define CHUNKS    3u
#define CHUNK_LEN 64000u

static uint8_t buf[CHUNK_LEN];

static void fill(uint32_t seed)
{
    uint32_t i;

    for (i = 0; i < CHUNK_LEN; i++) {
        buf[i] = (uint8_t)(i * 7u + seed * 31u);
    }
}

static int32_t max_ms;

/* One full-screen change and its present. Keeps the longest present. */
static void frame(uint32_t n)
{
    int32_t t0;

    pip_clear((n & 1u) ? 0x00ff00ffu : 0x0000ff00u);
    t0 = pip_ticks_ms();
    pip_present();
    t0 = pip_ticks_ms() - t0;
    max_ms = t0 > max_ms ? t0 : max_ms;
}

int pip_main(const pipos_startup_t *startup)
{
    uint32_t sent = 1, frames = 0;
    int32_t rc, bad = 0, last, now, max_gap = 0;

    (void)startup;
    fill(0);
    last = pip_ticks_ms();
    rt_check_eq("start", pip_sample(buf, CHUNK_LEN), 0);
    while (sent < CHUNKS) {
        frame(frames++);
        fill(sent);
        rc = pip_sample_queue(buf, CHUNK_LEN);
        now = pip_ticks_ms();
        max_gap = now - last > max_gap ? now - last : max_gap;
        last = now;
        if (rc == 0) {
            sent++;
        } else if (rc != -PIP_EBUSY && bad == 0) {
            bad = rc;
        }
    }
    while (pip_audio_status() == 1) {
        frame(frames++);
    }
    rt_check_eq("queue", bad, 0);
    rt_begin("stream");
    rt_dec("frames", (int32_t)frames);
    rt_dec("max_ms", max_ms);
    rt_dec("max_gap_ms", max_gap);
    rt_end();
    rt_checks_done();
    return 0;
}
