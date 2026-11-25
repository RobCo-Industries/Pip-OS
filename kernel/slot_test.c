/*
 * The test slot backend, compiled only with -DPIPOS_HOLO_TEST. QEMU has
 * no SD card on SPI0, so this backend serves a tape card from RAM: the
 * test puts card images in guest memory with QEMU's -device loader at
 * PIPOS_TEST_CARD_BASE + n * PIPOS_TEST_CARD_STRIDE, and drives the slot
 * with the holo script command:
 *
 *   holo insert <n> <blocks>  card n goes in, <blocks> of 512 bytes
 *   holo pull                 the card leaves
 *   holo readfail             the next read fails with BLK_ERR_IO, card in
 *   holo readpull             the next read pulls the card: BLK_ERR_NOMEDIA
 *   holo refuse               the next attach returns SD_ERR_CMD
 *   holo again <k>            the next k attaches return SD_ERR_AGAIN
 *   holo attachpull           the next attach pulls the card: BLK_ERR_NOMEDIA
 *   holo delay <ms>           every attach blocks <ms> first, as a slow card
 *
 * Each attach prints ev=holo_attach rc=<rc> ms=<ms blocked> samples=<PCM
 * samples output meanwhile>. The shipped image has none of this:
 * tests/test_slot.py checks it.
 */
#ifdef PIPOS_HOLO_TEST

#include <stdint.h>

#include <robco/audio.h>
#include <robco/holo.h>
#include <robco/input.h>
#include <robco/klibc.h>
#include <robco/sd.h>
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
static int read_fail, read_pull, refuse, attach_pull;
static unsigned again;
static uint32_t delay_ms;

static int ram_read(blk_dev_t *dev, uint64_t lba, uint32_t count, void *buf)
{
    const uint8_t *src = (const uint8_t *)(uintptr_t)(PIPOS_TEST_CARD_BASE +
                                                      card_no * PIPOS_TEST_CARD_STRIDE);

    (void)dev;
    if (read_pull) {
        read_pull = 0;
        card_in = 0;
    }
    if (!card_in || !attached) {
        attached = 0;
        return BLK_ERR_NOMEDIA;
    }
    if (read_fail) {
        read_fail = 0;
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
    return card_in;
}

static int ram_present(void)
{
    if (!card_in) {
        attached = 0;
        return 0;
    }
    return (uint32_t)(timer_now_ms() - card_since) >= HOLO_SETTLE_MS;
}

static int ram_attach_inner(void)
{
    if (delay_ms != 0) {
        timer_delay_ms(delay_ms);
    }
    if (attach_pull) {
        attach_pull = 0;
        card_in = 0;
    }
    if (!card_in) {
        return BLK_ERR_NOMEDIA;
    }
    if (again != 0) {
        again--;
        return SD_ERR_AGAIN;
    }
    if (refuse) {
        refuse = 0;
        return SD_ERR_CMD;
    }
    ram_dev.block_count = card_blocks;
    attached = 1;
    return 0;
}

static int ram_attach(void)
{
    uint32_t t0 = timer_now_ms();
    uint32_t s0 = audio_pcm_samples(NULL);
    int rc = ram_attach_inner();

    k_printf("@RC stage=" PIPOS_STAGE " ev=holo_attach rc=%d ms=%u samples=%u\n", rc,
             (unsigned)(timer_now_ms() - t0), (unsigned)(audio_pcm_samples(NULL) - s0));
    return rc;
}

static void ram_detach(void)
{
    attached = 0;
}

static blk_dev_t *ram_blkdev(void)
{
    return attached ? &ram_dev : NULL;
}

const pipos_slot_backend_t pipos_slot_ram = {
    "ram", ram_init, ram_card, ram_present, ram_attach, ram_detach, ram_blkdev,
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
    } else if (k_strcmp(op, "pull") == 0 && argc == 2) {
        card_in = 0;
    } else if (k_strcmp(op, "readfail") == 0 && argc == 2) {
        read_fail = 1;
    } else if (k_strcmp(op, "readpull") == 0 && argc == 2) {
        read_pull = 1;
    } else if (k_strcmp(op, "refuse") == 0 && argc == 2) {
        refuse = 1;
    } else if (k_strcmp(op, "attachpull") == 0 && argc == 2) {
        attach_pull = 1;
    } else if (k_strcmp(op, "again") == 0 && argc == 3 && number(argv[2], &a) == 0) {
        again = a;
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
