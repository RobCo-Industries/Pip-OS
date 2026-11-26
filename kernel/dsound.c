/*
 * The disk click: the glue between the machine's work and the rules of
 * dsound_rules.c.
 *
 * The card's and the slot's block devices go through a wrapper that counts
 * calls and blocks. Drivers, FatFs and the loader know nothing of sound.
 * Until PIPOS.CFG is parsed the counts wait. With [audio] system_sound off
 * the wrapper only forwards, and no tick callback, marker or click exists.
 *
 * A tick callback asks the limiter every 10 ms. A due click starts as its
 * own play when the voice is idle. While the boot note plays and more than
 * 30 ms of it remain, the click mixes over it as an overlay. A playing
 * click, the ramp, the beep or the note's last 30 ms make it wait a tick.
 * A click the driver refuses waits a tick too: nothing is lost.
 *
 * The note starts with the second boot line, when the typed text will
 * outlast it. The main context closes the gate, waits for the voice,
 * starts the note and opens the gate again.
 *
 * When the last boot line starts, typing on, the main context closes the
 * gate for good and drops what is owed: the quiet second before the
 * scroll. A click that plays then ends by itself. A boot with no ROM opens
 * the gate again after the last line. The scroll's blips play from the
 * main context while the gate is closed.
 *
 * After the jump to a ROM the kernel plays no click, so a click never meets
 * a ROM's own sound. A click that still plays at the jump ends first.
 *
 * Clicks start from the tick at any moment, and the trace backend queues
 * their records until the main context polls. So the kernel polls through
 * pipos_dsound_poll() wherever it waits or works long: each typed character,
 * the line delay in 100 ms steps, each card or slot call, and before each
 * of its own markers. The fastest pace is one click per 3 ticks, two
 * records per click, so 100 ms adds at most 8 of the queue's 64 records.
 */
#include <robco/audio.h>
#include <robco/blk.h>
#include <robco/irq.h>
#include <robco/klibc.h>
#include <robco/tick.h>
#include <robco/timer.h>

#include "bootlines.h"
#include "dsound_rules.h"
#include "pipos.h"
#include "sound_table.h"

#define HELD_MAX 4u

/* The table's rows are the library's spans, so it goes to the voice as is. */
_Static_assert(sizeof(audio_voice_span_t) == 28u, "a voice span is 28 bytes");
_Static_assert(sizeof(audio_voice_span_t) == sizeof(sound_note_spans[0]),
               "a note row is one voice span");

struct stage {
    const char *at;
    unsigned calls, blocks, owed;
};

struct wrap {
    blk_dev_t dev;
    blk_dev_t *inner;
};

static struct pipos_dsound rules;
static struct stage now;          /* the stage that runs */
static struct stage held[HELD_MAX];
static unsigned nheld;
static int known;                 /* the setting is read */
static int counting = 1;          /* 0 once the setting is off */
static int on;                    /* the setting is on and the tick runs */
static volatile int jumped;
static struct wrap card_wrap, slot_wrap;
static volatile int note_on;      /* the note plays or waits for its tick */
static volatile uint32_t note_end_ms;

/*
 * Prints the queued audio records, so they come before the next marker and
 * the queue never fills. Main context only. With the sound off, or after the
 * jump, it returns at once: on a board the PWM backend has no queue anyway.
 */
void pipos_dsound_poll(void)
{
    if (on && !jumped) {
        audio_poll();
    }
}

/* timer_delay_ms() in steps of at most 100 ms, with a poll after each. */
void pipos_dsound_delay(uint32_t ms)
{
    while (ms > 0) {
        uint32_t step = ms > 100u ? 100u : ms;

        timer_delay_ms(step);
        ms -= step;
        pipos_dsound_poll();
    }
}

static void count_io(uint32_t count)
{
    uint32_t irq;
    unsigned adds;

    if (!counting) {
        return;
    }
    adds = pipos_dsound_io_adds(count);
    irq = irq_save();
    now.calls++;
    now.blocks += count;
    now.owed += adds;
    if (on) {
        pipos_dsound_add(&rules, adds);
    }
    irq_restore(irq);
}

static int wrap_read(blk_dev_t *dev, uint64_t lba, uint32_t count, void *buf)
{
    blk_dev_t *inner = ((struct wrap *)dev)->inner;
    int rc = inner->read(inner, lba, count, buf);

    count_io(count);
    pipos_dsound_poll();
    return rc;
}

static int wrap_write(blk_dev_t *dev, uint64_t lba, uint32_t count, const void *buf)
{
    blk_dev_t *inner = ((struct wrap *)dev)->inner;
    int rc = inner->write(inner, lba, count, buf);

    count_io(count);
    pipos_dsound_poll();
    return rc;
}

static int wrap_sync(blk_dev_t *dev)
{
    blk_dev_t *inner = ((struct wrap *)dev)->inner;

    return inner->sync != NULL ? inner->sync(inner) : 0;
}

static blk_dev_t *wrap(struct wrap *w, blk_dev_t *dev)
{
    if (dev == NULL) {
        return NULL;
    }
    w->dev = *dev;
    w->inner = dev;
    w->dev.read = wrap_read;
    w->dev.write = dev->write != NULL ? wrap_write : NULL;
    w->dev.sync = wrap_sync;
    return &w->dev;
}

blk_dev_t *pipos_dsound_card(blk_dev_t *dev)
{
    return wrap(&card_wrap, dev);
}

blk_dev_t *pipos_dsound_slot(blk_dev_t *dev)
{
    return wrap(&slot_wrap, dev);
}

static void print_stage(const struct stage *st)
{
    pipos_dsound_poll();
    k_printf("@RC stage=" PIPOS_STAGE " ev=disk at=%s calls=%u blocks=%u owed=%u\n", st->at,
             st->calls, st->blocks, st->owed);
}

static struct stage take_stage(const char *at)
{
    struct stage st;
    uint32_t irq = irq_save();

    st = now;
    st.at = at;
    k_memset(&now, 0, sizeof(now));
    irq_restore(irq);
    return st;
}

void pipos_dsound_stage(const char *at)
{
    struct stage st;

    if (!counting) {
        return;
    }
    st = take_stage(at);
    if (!known) {
        if (nheld < HELD_MAX) {
            held[nheld++] = st;
        }
        return;
    }
    print_stage(&st);
}

/* What is left of the note, by the clock. 0 once its end has passed. */
static uint32_t note_left_ms(void)
{
    int32_t left = (int32_t)(note_end_ms - timer_now_ms());

    return left > 0 ? (uint32_t)left : 0u;
}

/* IRQ context, every tick. */
static void dsound_tick(void)
{
    struct pipos_dsound before;
    int busy, way, k, rc;

    if (jumped) {
        return;
    }
    busy = audio_busy();
    if (!busy) {
        note_on = 0;
    }
    way = pipos_dsound_way(busy, note_on, note_on ? note_left_ms() : 0u, audio_pcm_mix_busy());
    before = rules;
    k = pipos_dsound_tick(&rules, way != PIPOS_CLICK_WAIT);
    if (k < 0) {
        return;
    }
    if (way == PIPOS_CLICK_MIX) {
        rc = audio_pcm_mix(sound_click[k], SOUND_CLICK_LEN);
    } else {
        rc = audio_play_pcm(sound_click[k], SOUND_CLICK_LEN, SOUND_RATE_HZ, 0);
    }
    if (rc != 0) {
        rules = before; /* refused, the note's end for one: try again next tick */
    }
}

void pipos_dsound_start(int sound)
{
    unsigned i, owed = 0;
    uint32_t irq;

    known = 1;
    if (!sound) {
        counting = 0;
        nheld = 0;
        return;
    }
    pipos_dsound_init(&rules);
    pipos_dsound_open(&rules);
    for (i = 0; i < nheld; i++) {
        print_stage(&held[i]);
        owed += held[i].owed;
    }
    nheld = 0;
    if (tick_register(dsound_tick) != 0) {
        k_printf("@RC stage=" PIPOS_STAGE " ev=disk rc=tickfull\n");
        counting = 0;
        return;
    }
    irq = irq_save();
    on = 1;
    pipos_dsound_add(&rules, owed + now.owed);
    irq_restore(irq);
}

unsigned pipos_dsound_line(const char *text)
{
    unsigned adds;
    uint32_t irq;

    if (!on || jumped) {
        return 0;
    }
    adds = pipos_dsound_text_adds(text);
    irq = irq_save();
    pipos_dsound_add(&rules, adds);
    irq_restore(irq);
    return adds;
}

void pipos_dsound_line_done(unsigned owed)
{
    if (!on || jumped) {
        return;
    }
    pipos_dsound_poll();
    k_printf("@RC stage=" PIPOS_STAGE " ev=disk at=text calls=0 blocks=0 owed=%u\n", owed);
}

void pipos_dsound_note(uint32_t char_ms, uint32_t line_ms)
{
    unsigned chars, delays;
    uint32_t irq;
    int rc;

    if (!on || jumped) {
        return;
    }
    pipos_dsound_note_text(pipos_boot_head, pipos_boot_tail, &chars, &delays);
    if (!pipos_dsound_note_fits(chars, delays, char_ms, line_ms, SOUND_NOTE_MS)) {
        return;
    }
    irq = irq_save();
    pipos_dsound_close(&rules);
    irq_restore(irq);
    audio_wait();
    rc = audio_play_voice((const audio_voice_span_t *)sound_note_spans, SOUND_NOTE_SPANS,
                          SOUND_RATE_HZ, 0);
    irq = irq_save();
    if (rc == 0) {
        /* The play starts at the next tick, so the real end is a little later. */
        note_end_ms = timer_now_ms() + SOUND_NOTE_MS;
        note_on = 1;
    }
    pipos_dsound_open(&rules);
    irq_restore(irq);
}

int pipos_dsound_quiet(uint32_t at_ms)
{
    unsigned dropped;
    uint32_t irq;

    if (!on || jumped) {
        return 0;
    }
    irq = irq_save();
    dropped = rules.owed;
    pipos_dsound_close(&rules);
    irq_restore(irq);
    pipos_dsound_poll();
    k_printf("@RC stage=" PIPOS_STAGE " ev=quiet dropped=%u t_ms=%u\n", dropped,
             (unsigned)at_ms);
    return 1;
}

void pipos_dsound_reopen(void)
{
    uint32_t irq;

    if (!on || jumped) {
        return;
    }
    irq = irq_save();
    pipos_dsound_open(&rules);
    irq_restore(irq);
}

int pipos_dsound_blip(void)
{
    if (!on || jumped) {
        return 0;
    }
    return audio_play_pcm(sound_blip, SOUND_BLIP_LEN, SOUND_RATE_HZ, 0) == 0;
}

void pipos_dsound_note_wait(void)
{
    while (note_on) {
        pipos_dsound_delay(10);
    }
}

void pipos_dsound_jump(void)
{
    uint32_t irq;

    if (!on || jumped) {
        return;
    }
    irq = irq_save();
    pipos_dsound_rom(&rules);
    jumped = 1;
    irq_restore(irq);
    /*
     * Let a click that plays end, then print every queued record, so none
     * comes after ev=jump. A click lasts 21 ms, so the wait is short.
     */
    audio_wait();
}
