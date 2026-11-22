/*
 * The audio group: tone, audio_stop, volume, audio_status and play_seq.
 * sample (0x21) has no handler and returns -PIP_ENOSYS: RobCo-Core plays
 * square waves only, it has no PCM path. See docs/API.md.
 *
 * One voice. RobCo-Core's audio driver plays in the background from the
 * tick interrupt; a new sound replaces the one that plays. The library
 * takes requests under irq_save(), so the handlers only add the image's
 * own state on top.
 */
#include <robco/audio.h>
#include <robco/fs.h>
#include <robco/irq.h>

#include "audio_rules.h"
#include "pipos.h"
#include "sys.h"

#define CFG_TEXT_MAX    4096u

/* The level every call sees; the library has no getter. Per boot. */
static unsigned level;
/* 1 after the image started a tone or a sequence, until it stops or ends. */
static int owned;
/* The notes the tick plays. Rewritten only with IRQs masked. */
static audio_note_t seq_buf[PIP_SEQ_MAX];
/* play_seq's copy of the caller's notes. Main context only. */
static pip_note_t user_notes[PIP_SEQ_MAX];
static char cfg_text[CFG_TEXT_MAX];

static void stop_owned(void)
{
    if (owned) {
        audio_stop();
        owned = 0;
    }
}

static int32_t sys_tone(const pipos_sys_ctx_t *ctx, uint32_t hz, uint32_t ms, uint32_t a2,
                        uint32_t a3)
{
    int32_t rc = pipos_audio_check_tone(hz, ms);

    (void)ctx, (void)a2, (void)a3;
    if (rc != 0) {
        return rc;
    }
    if (audio_tone(hz, ms) != 0) {
        return -PIP_EIO;
    }
    owned = 1;
    return 0;
}

static int32_t sys_audio_stop(const pipos_sys_ctx_t *ctx, uint32_t a0, uint32_t a1,
                              uint32_t a2, uint32_t a3)
{
    (void)ctx, (void)a0, (void)a1, (void)a2, (void)a3;
    audio_stop();
    owned = 0;
    return 0;
}

static int32_t sys_volume(const pipos_sys_ctx_t *ctx, uint32_t new_level, uint32_t a1,
                          uint32_t a2, uint32_t a3)
{
    int32_t prev = pipos_audio_volume_swap(&level, new_level);

    (void)ctx, (void)a1, (void)a2, (void)a3;
    if (prev >= 0) {
        audio_set_volume(level);
    }
    return prev;
}

static int32_t sys_audio_status(const pipos_sys_ctx_t *ctx, uint32_t a0, uint32_t a1,
                                uint32_t a2, uint32_t a3)
{
    (void)ctx, (void)a0, (void)a1, (void)a2, (void)a3;
    if (owned && !audio_busy()) {
        owned = 0;
    }
    return owned;
}

/*
 * The tick reads seq_buf while a sequence plays. The rewrite and the
 * request happen with IRQs masked, and the tick applies a request before
 * it reads the notes, so it never sees a half-written buffer.
 */
static int32_t sys_play_seq(const pipos_sys_ctx_t *ctx, uint32_t notes, uint32_t count,
                            uint32_t a2, uint32_t a3)
{
    int32_t rc = pipos_audio_check_count(count);
    uint32_t s;
    uint32_t i;
    int played;

    (void)a2, (void)a3;
    if (rc != 0) {
        return rc;
    }
    rc = pipos_user_copy_in(ctx, user_notes, notes, count * sizeof(pip_note_t));
    if (rc != 0) {
        return rc;
    }
    for (i = 0; i < count; i++) {
        if (pipos_audio_check_note(&user_notes[i]) != 0) {
            return -PIP_EINVAL;
        }
    }
    s = irq_save();
    pipos_audio_convert(user_notes, count, seq_buf);
    played = audio_play(seq_buf, count, AUDIO_PRIORITY);
    irq_restore(s);
    if (played != 0) {
        return -PIP_EIO;
    }
    owned = 1;
    return 0;
}

/* PIPOS_SYS_ENTRY(AUDIO, <NAME>, <handler>), one line per served call, in number order. */
static const pipos_sys_entry_t audio_calls[] = {
    PIPOS_SYS_ENTRY(AUDIO, TONE, sys_tone),
    PIPOS_SYS_ENTRY(AUDIO, AUDIO_STOP, sys_audio_stop),
    PIPOS_SYS_ENTRY(AUDIO, VOLUME, sys_volume),
    PIPOS_SYS_ENTRY(AUDIO, AUDIO_STATUS, sys_audio_status),
    PIPOS_SYS_ENTRY(AUDIO, PLAY_SEQ, sys_play_seq),
};

const pipos_sys_table_t pipos_audio_table = PIPOS_SYS_TABLE(AUDIO, "audio", audio_calls);

/*
 * The start level is the volume setting main.c gave the library. The
 * library has no getter, so this reads PIPOS.CFG again with the same
 * parser and no reports: the same text gives the same value.
 */
void pipos_audio_init(void)
{
    pipos_settings_t s;
    size_t len = 0;
    unsigned line = 0;

    pipos_settings_default(&s);
    if (fs_read_file(PIPOS_VOLUME ":" PIPOS_CFG_PATH, cfg_text, sizeof(cfg_text), &len) == FS_OK) {
        pipos_settings_parse(cfg_text, len, &s, NULL, NULL, &line);
    }
    level = s.volume;
    owned = 0;
}

/* Only a sound an image started is stopped: the boot beep plays on. */
void pipos_audio_image_start(int kind)
{
    (void)kind;
    stop_owned();
}

void pipos_audio_image_end(int kind)
{
    (void)kind;
    stop_owned();
}

void pipos_audio_poll(void)
{
}
