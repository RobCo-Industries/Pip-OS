/*
 * The SD card: mount, configuration file, holotape directory, ROM file.
 * Each step prints one marker. No step fails the boot.
 */
#include <pipos/abi.h>
#include <robco/fs.h>
#include <robco/ini.h>
#include <robco/klibc.h>
#include <robco/rimg.h>
#include <robco/sd.h>

#include "bootlines.h"
#include "pipos.h"
#include "storage.h"

#define VOL(path)       PIPOS_VOLUME ":" path
/* The work files of RobCo-Core's atomic write: new content, old content. */
#define SET_NEW         "/PIPBOY/~PIPOS.SET"
#define SET_OLD         "/PIPBOY/#PIPOS.SET"
#define CFG_TEXT_MAX    4096u

static fs_boot_t boot_vol;
static char cfg_text[CFG_TEXT_MAX];
static char set_text[PIPOS_LEVELS_FILE_MAX];

/*
 * A volume that holds one of these as a non-empty file is the Pip-OS card.
 * When no volume holds one, fs_mount_boot() mounts the first that mounts,
 * so a card without them still boots as it did before.
 */
static const char *const card_markers[] = { PIPOS_CFG_PATH, PIPOS_ROM_PATH };

/*
 * Mounts the boot volume with RobCo-Core's shared rule, see fs_mount_boot().
 * A failed block 0 read is rc=nopart; every other failure is rc=nomount,
 * with the rule's name for it in why.
 */
int pipos_card_mount(void)
{
    int rc = sd_init();

    if (rc != 0) {
        k_printf("@RC stage=" PIPOS_STAGE " ev=card rc=nocard sd=%d\n", rc);
        return 0;
    }
    rc = fs_mount_boot(PIPOS_VOLUME, pipos_dsound_card(sd_blkdev()), card_markers,
                       sizeof(card_markers) / sizeof(card_markers[0]), &boot_vol);
    if (rc == PART_BOOT_ERR_RELEASE) {
        /* Only reads happened, so this volume is not the one to use: let it go. */
        (void)fs_unmount(PIPOS_VOLUME);
    }
    if (rc == PART_BOOT_ERR_DEVICE) {
        k_printf("@RC stage=" PIPOS_STAGE " ev=card rc=nopart part=%d\n", boot_vol.last_err);
        return 0;
    }
    if (rc != PART_BOOT_OK) {
        k_printf("@RC stage=" PIPOS_STAGE " ev=card rc=nomount fs=%d why=%s\n",
                 boot_vol.last_err, part_boot_strerror(rc));
        return 0;
    }
    k_printf("@RC stage=" PIPOS_STAGE " ev=card rc=ok\n");
    /* part is the MBR slot 0 to 3, or "whole" for a card without a table. */
    if (boot_vol.slot == FS_BOOT_WHOLE) {
        k_printf("@RC stage=" PIPOS_STAGE " ev=mount part=whole type=0x00 lba=%u blocks=%u"
                 " marker=%u\n",
                 (unsigned)boot_vol.start_lba, (unsigned)boot_vol.block_count,
                 (unsigned)boot_vol.marker);
    } else {
        k_printf("@RC stage=" PIPOS_STAGE " ev=mount part=%u type=0x%02x lba=%u blocks=%u"
                 " marker=%u\n",
                 (unsigned)boot_vol.slot, (unsigned)boot_vol.type, (unsigned)boot_vol.start_lba,
                 (unsigned)boot_vol.block_count, (unsigned)boot_vol.marker);
    }
    pipos_dsound_stage("mount");
    return 1;
}

static void config_report(const char *rc, const char *key, unsigned line, void *ctx)
{
    char word[2 * (INI_MAX_KEY + 1)];

    (void)ctx;
    pipos_marker_word(key, word, sizeof(word));
    k_printf("@RC stage=" PIPOS_STAGE " ev=config rc=%s key=%s line=%u\n", rc, word, line);
}

static const char *config_read(int card, pipos_settings_t *s, unsigned *err_line)
{
    size_t len = 0;
    int rc;

    if (!card) {
        return "nocard";
    }
    rc = fs_read_file(VOL(PIPOS_CFG_PATH), cfg_text, sizeof(cfg_text), &len);
    if (rc == FS_ERR_NOENT) {
        return "nofile";
    }
    if (rc == FS_ERR_TOOBIG) {
        return "toobig";
    }
    if (rc != FS_OK) {
        return "io";
    }
    rc = pipos_settings_parse(cfg_text, len, s, config_report, NULL, err_line);
    if (rc != 0) {
        return "syntax";
    }
    return "ok";
}

/*
 * Reads PIPOS.SET and writes nothing. A power cut inside sound_store's
 * atomic write can leave the file absent, with its old content in the old
 * work file and maybe its new content in the new one. Then this reads the
 * file the next atomic write's recovery would put in place: the new one
 * when both work files exist, else the old one. So a boot sees the whole
 * old file or the whole new one.
 */
static int levels_file_read(size_t *len)
{
    int rc = fs_read_file(VOL(PIPOS_SET_PATH), set_text, sizeof(set_text), len);
    fs_stat_t st;

    if (rc != FS_ERR_NOENT) {
        return rc;
    }
    rc = fs_stat(VOL(SET_OLD), &st);
    if (rc != FS_OK) {
        return rc;
    }
    rc = fs_read_file(VOL(SET_NEW), set_text, sizeof(set_text), len);
    if (rc != FS_ERR_NOENT) {
        return rc;
    }
    return fs_read_file(VOL(SET_OLD), set_text, sizeof(set_text), len);
}

/*
 * The stored levels of PIPOS.SET over those of PIPOS.CFG. Returns NULL
 * when the file is missing or applies, else the reason it does not.
 */
static const char *levels_read(int card, pipos_settings_t *s)
{
    pipos_levels_t lv;
    const char *why;
    size_t len = 0;
    int rc;
    int k;

    if (!card) {
        return NULL;
    }
    rc = levels_file_read(&len);
    if (rc == FS_ERR_NOENT) {
        return NULL;
    }
    if (rc == FS_ERR_TOOBIG) {
        return "size";
    }
    if (rc != FS_OK) {
        return "read";
    }
    lv.master = s->volume;
    for (k = 0; k < PIPOS_SND_KINDS; k++) {
        lv.level[k] = s->levels[k];
    }
    why = pipos_levels_read_text(set_text, len, &lv);
    if (why == NULL) {
        s->volume = lv.master;
        for (k = 0; k < PIPOS_SND_KINDS; k++) {
            s->levels[k] = lv.level[k];
        }
    }
    return why;
}

void pipos_config_load(int card, pipos_settings_t *s)
{
    pipos_settings_t cfg;
    unsigned err_line = 0;
    const char *rc;
    const char *bad;

    pipos_settings_default(s);
    rc = config_read(card, s, &err_line);
    cfg = *s;
    bad = levels_read(card, s);
    if (card) {
        pipos_dsound_stage("config");
    }
    k_printf("@RC stage=" PIPOS_STAGE " ev=config rc=%s path=" PIPOS_CFG_PATH
             " line=%u scheme=%s line_delay_ms=%u volume=%u beep=%d"
             " char_delay_ms=%u key_click=%d radio=%u sfx=%u recordings=%u games=%u\n",
             rc, err_line, pipos_scheme_name(cfg.scheme), (unsigned)cfg.line_delay_ms,
             cfg.volume, cfg.beep, (unsigned)cfg.char_delay_ms, cfg.system_sound,
             cfg.levels[PIPOS_SND_RADIO], cfg.levels[PIPOS_SND_SFX],
             cfg.levels[PIPOS_SND_RECORDINGS], cfg.levels[PIPOS_SND_GAMES]);
    if (bad != NULL) {
        k_printf("@RC stage=" PIPOS_STAGE " ev=levels result=bad reason=%s\n", bad);
    }
}

/*
 * sound_store. The folder exists, since the ROM came from it. The atomic
 * write runs its own recovery first, then replaces the file whole.
 */
int32_t pipos_levels_store(const pipos_levels_t *lv)
{
    char text[PIPOS_LEVELS_TEXT_MAX + 1];
    size_t len = pipos_levels_write_text(lv, text, sizeof(text));
    int rc;

    if (len == 0) {
        return -PIP_EIO;
    }
    rc = fs_write_file_atomic(VOL(PIPOS_SET_PATH), text, len);
    if (rc == FS_OK) {
        return 0;
    }
    return rc == FS_ERR_NOSPC ? -PIP_ENOSPC : -PIP_EIO;
}

unsigned pipos_holo_count(int card)
{
    fs_dir_t d;
    fs_stat_t st;
    unsigned count = 0;
    int rc;

    if (card && fs_opendir(&d, VOL(PIPOS_HOLO_DIR)) == FS_OK) {
        while ((rc = fs_readdir(&d, &st)) == 1) {
            if (!st.is_dir && !pipos_dir_entry_skipped(st.name, st.attr) &&
                pipos_is_holotape_name(st.name)) {
                count++;
            }
        }
        fs_closedir(&d);
    }
    k_printf("@RC stage=" PIPOS_STAGE " ev=holo count=%u\n", count);
    if (card) {
        pipos_dsound_stage("holo");
    }
    return count;
}

int pipos_rom_load(int card, loader_result_t *res)
{
    static const loader_slot_t slot = { PIPOS_ROM_BASE, PIPOS_ROM_SIZE, NULL };
    static const rimg_expect_t expect = { .magic = RIMG_MAGIC_ROM1, .abi_major = 1 };
    char name[PIPOS_ROM_NAME_MAX + 1];
    char version[24];
    int rc;

    k_memset(res, 0, sizeof(*res));
    if (card) {
        rc = loader_load(VOL(PIPOS_ROM_PATH), &slot, &expect, res);
    } else {
        rc = RIMG_E_IO; /* the spec's name for a missing card or mount */
        res->err = rc;
    }
    if (rc == RIMG_OK) {
        pipos_fmt_name(res->hdr->name, sizeof(res->hdr->name), name, sizeof(name), 0);
        pipos_fmt_version(res->hdr->image_version, version, sizeof(version));
        k_printf("@RC stage=" PIPOS_STAGE " ev=load path=" PIPOS_ROM_PATH
                 " size=%u crc=0x%08x addr=0x%08x name=%s\n",
                 (unsigned)res->file_size, (unsigned)res->crc32, (unsigned)res->entry, name);
    } else {
        k_strncpy(name, "-", sizeof(name));
        k_strncpy(version, "-", sizeof(version));
        k_printf("@RC stage=" PIPOS_STAGE " ev=error code=%s path=" PIPOS_ROM_PATH "\n",
                 rimg_strerror(rc));
    }
    k_printf("@RC stage=" PIPOS_STAGE " ev=rom rc=%s name=%s version=%s size=%u crc=0x%08x\n",
             rimg_strerror(rc), name, version, (unsigned)res->file_size,
             (unsigned)res->crc32);
    if (card) {
        pipos_dsound_stage("rom");
    }
    return rc;
}
