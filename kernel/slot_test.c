/*
 * The test slot backend, compiled only with -DPIPOS_HOLO_TEST. QEMU has
 * no SD card on SPI0, so this backend serves a tape card from RAM: the
 * test puts card images in guest memory with QEMU's -device loader at
 * PIPOS_TEST_CARD_BASE + n * PIPOS_TEST_CARD_STRIDE, and drives the slot
 * with the holo script command:
 *
 *   holo insert <n> <blocks>  card n goes in, <blocks> of 512 bytes; replies
 *                             t_ms=<tick_ms()>
 *   holo pull                 the card leaves; replies t_ms=<tick_ms()>
 *   holo readfail [k]         read k + 1 from now fails with BLK_ERR_IO, card
 *                             in. Without k, the next read.
 *   holo reads                replies reads=<n>, the reads served or failed
 *                             since boot
 *   holo readpull             the next read pulls the card: BLK_ERR_NOMEDIA
 *   holo refuse               the next attach ends in SD_ERR_CMD
 *   holo slow                 the next attach ends in SD_ERR_SLOW
 *   holo again <k>            the next k attach starts return SD_ERR_AGAIN
 *   holo attachpull           the next attach's first step pulls the card:
 *                             BLK_ERR_NOMEDIA
 *   holo delay <ms>           every attach steps <ms> before its result, as
 *                             a slow card. Each step returns at once.
 *   holo pending              replies attaching=<0|1> t_ms=<tick_ms()>:
 *                             1 while an attach runs
 *   holo pullin <ms>          the card leaves <ms> from now, seen by the
 *                             next card detect read, even while a tape
 *                             keeps the script channel from running;
 *                             replies t_ms=<tick_ms()>
 *
 * The attach is stepped, as holo.h's: a start, then one step per call.
 * A start that runs prints ev=holo_attach_start t_ms=<tick_ms()>. Each
 * attach, a refused start included, prints at its end ev=holo_attach
 * rc=<rc> ms=<ms from start to end> polls=<pipos_poll() passes meanwhile>
 * steps=<step calls> t0_ms=<tick_ms() at its start> t1_ms=<tick_ms() at
 * its end> pcm0= pos0= pcm1= pos1=. pcm0 and pcm1 are audio_busy() at the start
 * and at the end: 1 while a sound plays. pos0 and pos1 are the samples the
 * current or last PCM play has output, from audio_pcm_samples(). Each pair
 * is read with IRQs masked, so the PCM interrupt cannot run between them.
 * The audio trace's t_ms is within one tick of tick_ms(). The shipped
 * image has none of this: tests/test_slot.py checks it.
 */
#ifdef PIPOS_HOLO_TEST

#include <stdint.h>

#include <robco/audio.h>
#include <robco/holo.h>
#include <robco/input.h>
#include <robco/irq.h>
#include <robco/klibc.h>
#include <robco/sd.h>
#include <robco/tick.h>
#include <robco/timer.h>

#include "pipos.h"
#include "slot_backend.h"

#define PIPOS_TEST_CARD_BASE    0x02000000u
#define PIPOS_TEST_CARD_STRIDE  0x00800000u
#define PIPOS_TEST_CARDS        4u

static volatile int card_in;
static uint32_t card_since;     /* timer_now_ms() when it went in */
static unsigned card_no;
static uint32_t card_blocks;
static int attached;
static int read_pull, refuse, slow, attach_pull;
static unsigned again;
static uint32_t read_fail;      /* 0, or 1 + the reads to serve first */
static uint32_t reads;
static uint32_t delay_ms;
static int pull_armed;
static uint32_t pull_at;

static int ram_read(blk_dev_t *dev, uint64_t lba, uint32_t count, void *buf)
{
    const uint8_t *src = (const uint8_t *)(uintptr_t)(PIPOS_TEST_CARD_BASE +
                                                      card_no * PIPOS_TEST_CARD_STRIDE);

    (void)dev;
    reads++;
    if (read_pull) {
        read_pull = 0;
        card_in = 0;
    }
    if (!card_in || !attached) {
        attached = 0;
        return BLK_ERR_NOMEDIA;
    }
    if (read_fail != 0 && --read_fail == 0) {
        return BLK_ERR_IO;
    }
    k_memcpy(buf, src + (uint32_t)lba * BLK_SECTOR_SIZE, (size_t)count * BLK_SECTOR_SIZE);
    return 0;
}

static blk_dev_t ram_dev = {
    "holo0", BLK_SECTOR_SIZE, 0, BLK_FLAG_READONLY, ram_read, NULL, NULL, NULL,
};

static int ram_init(void)
{
    return 0;
}

static int ram_card(void)
{
    if (pull_armed && (int32_t)(timer_now_ms() - pull_at) >= 0) {
        pull_armed = 0;
        card_in = 0;
    }
    return card_in;
}

static int ram_present(void)
{
    if (!ram_card()) {
        attached = 0;
        return 0;
    }
    return (uint32_t)(timer_now_ms() - card_since) >= HOLO_SETTLE_MS;
}

/* The PCM state in one IRQ-masked read: *pos gets the play's samples output. */
static int pcm_state(uint32_t *pos)
{
    uint32_t s = irq_save();
    int busy = audio_busy();

    (void)audio_pcm_samples(pos);
    irq_restore(s);
    return busy;
}

/* The running attach, from its start to its final result. */
static struct {
    int running;
    uint32_t t0, k0, p0, pos0, steps;
    int pcm0;
} at;

/* Prints the attach's marker with its final result `rc` and ends it. */
static int attach_end(int rc)
{
    uint32_t pos1;
    int pcm1 = pcm_state(&pos1);
    uint32_t k1 = tick_ms();

    k_printf("@RC stage=" PIPOS_STAGE
             " ev=holo_attach rc=%d ms=%u polls=%u steps=%u t0_ms=%u t1_ms=%u pcm0=%d"
             " pos0=%u pcm1=%d pos1=%u\n", rc,
             (unsigned)(timer_now_ms() - at.t0), (unsigned)(pipos_poll_passes() - at.p0),
             (unsigned)at.steps, (unsigned)at.k0, (unsigned)k1, at.pcm0, (unsigned)at.pos0,
             pcm1, (unsigned)pos1);
    at.running = 0;
    return rc;
}

/* As holo_attach_start(): an error before anything began, or 0 and an attach runs. */
static int ram_attach_start(void)
{
    if (at.running) {
        return 0;
    }
    at.t0 = timer_now_ms();
    at.k0 = tick_ms();
    at.p0 = pipos_poll_passes();
    at.pcm0 = pcm_state(&at.pos0);
    at.steps = 0;
    if (!card_in) {
        return attach_end(BLK_ERR_NOMEDIA);
    }
    if (again != 0) {
        again--;
        return attach_end(SD_ERR_AGAIN);
    }
    at.running = 1;
    k_printf("@RC stage=" PIPOS_STAGE " ev=holo_attach_start t_ms=%u\n", (unsigned)at.k0);
    return 0;
}

/*
 * As holo_attach_step(): never waits. SD_PENDING until delay_ms ran since
 * the start, then the final result. A step after the end returns
 * BLK_ERR_NOMEDIA, as the library's does after a detach.
 */
static int ram_attach_step(void)
{
    if (!at.running) {
        return BLK_ERR_NOMEDIA;
    }
    at.steps++;
    if (attach_pull) {
        attach_pull = 0;
        card_in = 0;
    }
    if (!card_in) {
        return attach_end(BLK_ERR_NOMEDIA);
    }
    if ((uint32_t)(timer_now_ms() - at.t0) < delay_ms) {
        return SD_PENDING;
    }
    if (refuse) {
        refuse = 0;
        return attach_end(SD_ERR_CMD);
    }
    if (slow) {
        slow = 0;
        return attach_end(SD_ERR_SLOW);
    }
    ram_dev.block_count = card_blocks;
    attached = 1;
    return attach_end(0);
}

/* As holo_detach(): ends a running attach, which then reports BLK_ERR_NOMEDIA. */
static void ram_detach(void)
{
    attached = 0;
    if (at.running) {
        (void)attach_end(BLK_ERR_NOMEDIA);
    }
}

static blk_dev_t *ram_blkdev(void)
{
    return attached ? &ram_dev : NULL;
}

const pipos_slot_backend_t pipos_slot_ram = {
    "ram", ram_init, ram_card, ram_present, ram_attach_start, ram_attach_step, ram_detach,
    ram_blkdev,
};

static int number(const char *s, uint32_t *out)
{
    uint32_t v = 0;

    if (*s == '\0') {
        return -1;
    }
    for (; *s != '\0'; s++) {
        if (*s < '0' || *s > '9' || v > 100000000u) {
            return -1;
        }
        v = v * 10u + (uint32_t)(*s - '0');
    }
    *out = v;
    return 0;
}

/* Replies t_ms=<tick_ms()>: when insert or pull took effect, on the attach's clock. */
static void reply_time(struct input_script_reply *reply)
{
    char line[24];

    k_snprintf(line, sizeof(line), "t_ms=%u\n", (unsigned)tick_ms());
    reply->write(line);
}

int pipos_slot_test_command(int argc, char *argv[], struct input_script_reply *reply)
{
    const char *op = argc >= 2 ? argv[1] : "";
    uint32_t a = 0, b = 0;

    if (k_strcmp(op, "insert") == 0 && argc == 4 && number(argv[2], &a) == 0 &&
        number(argv[3], &b) == 0 && a < PIPOS_TEST_CARDS && b > 0 &&
        b <= PIPOS_TEST_CARD_STRIDE / BLK_SECTOR_SIZE) {
        card_no = a;
        card_blocks = b;
        card_since = timer_now_ms();
        card_in = 1;
        reply_time(reply);
    } else if (k_strcmp(op, "pull") == 0 && argc == 2) {
        card_in = 0;
        reply_time(reply);
    } else if (k_strcmp(op, "readfail") == 0 && argc == 2) {
        read_fail = 1;
    } else if (k_strcmp(op, "readfail") == 0 && argc == 3 && number(argv[2], &a) == 0 &&
               a < 100000u) {
        read_fail = a + 1u;
    } else if (k_strcmp(op, "reads") == 0 && argc == 2) {
        char line[24];

        k_snprintf(line, sizeof(line), "reads=%u\n", (unsigned)reads);
        reply->write(line);
    } else if (k_strcmp(op, "readpull") == 0 && argc == 2) {
        read_pull = 1;
    } else if (k_strcmp(op, "refuse") == 0 && argc == 2) {
        refuse = 1;
    } else if (k_strcmp(op, "slow") == 0 && argc == 2) {
        slow = 1;
    } else if (k_strcmp(op, "pending") == 0 && argc == 2) {
        char line[40];

        k_snprintf(line, sizeof(line), "attaching=%d t_ms=%u\n", at.running,
                   (unsigned)tick_ms());
        reply->write(line);
    } else if (k_strcmp(op, "attachpull") == 0 && argc == 2) {
        attach_pull = 1;
    } else if (k_strcmp(op, "again") == 0 && argc == 3 && number(argv[2], &a) == 0) {
        again = a;
    } else if (k_strcmp(op, "pullin") == 0 && argc == 3 && number(argv[2], &a) == 0) {
        pull_at = timer_now_ms() + a;
        pull_armed = 1;
        reply_time(reply);
    } else if (k_strcmp(op, "delay") == 0 && argc == 3 && number(argv[2], &a) == 0 &&
               a <= 5000u) {
        delay_ms = a;
    } else {
        reply->reason = "bad holo command";
        return -1;
    }
    return 0;
}

#endif
