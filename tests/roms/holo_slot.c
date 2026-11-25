/*
 * The ROM of tests/test_slot.py: a ROM's slot loop, driven by keys.
 * It logs every PIP_EV_TAPE it gets and acts on a press:
 *
 *   up      ev=mask bits=<holo_eject_event()>
 *   select  ev=entry i= file= flags= for each tape, then ev=list n=
 *   right   holo_launch("SLOT.HTP", "loop"), then ev=launch rc=
 *   left    holo_launch("SLOT.HTP", "ticks"), then ev=launch rc=
 *   down    holo_launch("SLOT.HTP", "eject"), then ev=launch rc=
 *   back    holo_launch("BOOT.HTP", "loop"), then ev=launch rc=
 *   dial +n holo_launch("SLOT.HTP", ""), then ev=launch rc=
 *   dial -n streams through sample_queue in 100 ms chunks: ev=stream
 *           after the first chunk, then ev=streamed n=<chunks queued>.
 *           It waits with sleep_ms(20) while a chunk already waits. It
 *           streams until a PIP_EV_TAPE comes, logged as ev=tapeev, then
 *           300 ms more, so a card inserted after ev=stream lands in a
 *           live stream. It stops at 10 s if no PIP_EV_TAPE comes.
 *   dial_push  plays a 7.5 s sample, then ev=sample rc=
 *
 * It starts with ev=hw flags= abi= from sys_info and ev=wait before each
 * wait_event.
 */
#include "rt.h"

static pip_holo_info_t all[PIP_HOLO_ENUM_MAX];
static uint8_t pcm[60000];

#define CHUNK       800u            /* 100 ms at 8 kHz */
#define STREAM_MAX_MS   10000
#define STREAM_TAIL_MS  300

static uint8_t chunk[CHUNK];

static void say(const char *ev, const char *k, int32_t v);

static void stream(void)
{
    int32_t t0 = pip_ticks_ms();
    int32_t until = STREAM_MAX_MS;
    int32_t n = 0;
    int tape = 0;
    pip_event_t ev;
    uint32_t i;

    for (i = 0; i < CHUNK; i++) {
        chunk[i] = (uint8_t)(i & 0x20u ? 0xC0u : 0x40u);
    }
    while (pip_ticks_ms() - t0 < until) {
        if (!tape && pip_poll_event(&ev) == 1 && ev.type == PIP_EV_TAPE) {
            say("tapeev", "value", ev.value);
            tape = 1;
            until = pip_ticks_ms() - t0 + STREAM_TAIL_MS;
        }
        if (pip_sample_queue(chunk, CHUNK) == 0) {
            if (n++ == 0) {
                pip_log("@RC stage=testrom ev=stream\n");
            }
        } else {
            pip_sleep_ms(20);
        }
    }
    pip_audio_stop();
    say("streamed", "n", n);
}

static void say(const char *ev, const char *k, int32_t v)
{
    char line[96];

    pip_snprintf(line, sizeof(line), "@RC stage=testrom ev=%s %s=%d\n", ev, k, (int)v);
    pip_log(line);
}

static void launch(const char *name, const char *args)
{
    say("launch", "rc", pip_holo_launch(name, args));
}

static void list(void)
{
    char line[96];
    int32_t n = pip_holo_enumerate(all, PIP_HOLO_ENUM_MAX);
    int32_t i;

    for (i = 0; i < n; i++) {
        pip_snprintf(line, sizeof(line), "@RC stage=testrom ev=entry i=%d file=%s flags=%u\n",
                     (int)i, all[i].file, (unsigned)all[i].flags);
        pip_log(line);
    }
    say("list", "n", n);
}

int pip_main(const pipos_startup_t *startup)
{
    pip_sys_info_t si;
    pip_event_t ev;
    char line[96];

    (void)startup;
    pip_sys_info(&si);
    pip_snprintf(line, sizeof(line), "@RC stage=testrom ev=hw flags=%u abi=0x%08x\n",
                 (unsigned)si.hw_flags, (unsigned)si.abi_version);
    pip_log(line);
    for (;;) {
        pip_log("@RC stage=testrom ev=wait\n");
        if (pip_wait_event(&ev, PIP_WAIT_FOREVER) != 1) {
            continue;
        }
        if (ev.type == PIP_EV_TAPE) {
            say("tapeev", "value", ev.value);
            continue;
        }
        if (ev.type == PIP_EV_DIAL && ev.value < 0) {
            stream();
            continue;
        }
        if (ev.type == PIP_EV_DIAL) {
            launch("SLOT.HTP", "");
            continue;
        }
        if (ev.type != PIP_EV_BUTTON || ev.value != PIP_EV_PRESS) {
            continue;
        }
        switch (ev.code) {
        case PIP_BTN_UP:     say("mask", "bits", pip_holo_eject_event()); break;
        case PIP_BTN_SELECT: list(); break;
        case PIP_BTN_RIGHT:  launch("SLOT.HTP", "loop"); break;
        case PIP_BTN_LEFT:   launch("SLOT.HTP", "ticks"); break;
        case PIP_BTN_DOWN:   launch("SLOT.HTP", "eject"); break;
        case PIP_BTN_BACK:   launch("BOOT.HTP", "loop"); break;
        case PIP_BTN_DIAL: {
            uint32_t i;

            for (i = 0; i < sizeof(pcm); i++) {
                pcm[i] = (uint8_t)(i & 0x40u ? 0xC0u : 0x40u);
            }
            say("sample", "rc", pip_sample(pcm, sizeof(pcm)));
            break;
        }
        default: break;
        }
    }
}
