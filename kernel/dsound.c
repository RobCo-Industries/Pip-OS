/*
 * The disk click: the glue between the machine's work and the rules of
 * dsound_rules.c.
 *
 * The card's and the slot's block devices go through a wrapper that counts
 * calls and blocks. Drivers, FatFs and the loader know nothing of sound.
 * Until PIPOS.CFG is parsed the counts wait. With [audio] system_sound off
 * the wrapper only forwards, and no tick callback, marker or click exists.
 *
 * A tick callback asks the limiter every 10 ms. A due click starts only
 * when the voice is idle: a playing click, the ramp or the beep makes it
 * wait a tick, and nothing is lost. A mixed path over the boot note would
 * go in can_play() and start_click().
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

#include "dsound_rules.h"
#include "pipos.h"
#include "sound_table.h"

#define HELD_MAX 4u

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

/* The voice is idle. A note to mix over would be checked here. */
static int can_play(void)
{
    return !audio_busy();
}

static void start_click(int k)
{
    (void)audio_play_pcm(sound_click[k], SOUND_CLICK_LEN, SOUND_RATE_HZ, 0);
}

/* IRQ context, every tick. */
static void dsound_tick(void)
{
    int k;

    if (jumped) {
        return;
    }
    k = pipos_dsound_tick(&rules, can_play());
    if (k >= 0) {
        start_click(k);
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
