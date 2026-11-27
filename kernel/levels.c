/*
 * The sound levels and the gain of a play. See levels.h.
 */
#include <pipos/abi.h>
#include <robco/ini.h>
#include <robco/klibc.h>

#include "levels.h"

static const char *const level_keys[PIPOS_SND_KINDS] = {
    "radio", "sfx", "recordings", "games",
};

void pipos_levels_default(pipos_levels_t *lv)
{
    int k;

    lv->master = PIPOS_LEVEL_MAX;
    for (k = 0; k < PIPOS_SND_KINDS; k++) {
        lv->level[k] = PIPOS_LEVEL_MAX;
    }
}

static uint32_t cap(unsigned v)
{
    return v > PIPOS_LEVEL_MAX ? PIPOS_LEVEL_MAX : (uint32_t)v;
}

/* The largest product is 100^4 = 10^8, under 2^32 with the 5000 added. */
uint32_t pipos_levels_gain(unsigned master, unsigned level, unsigned trim)
{
    uint32_t l = cap(level);

    return (cap(master) * l * l * cap(trim) + 5000u) / 10000u;
}

uint32_t pipos_levels_gain_of(const pipos_levels_t *lv, int kind, unsigned trim)
{
    if (kind < 0 || kind >= PIPOS_SND_KINDS) {
        kind = PIPOS_SND_SFX;
    }
    return pipos_levels_gain(lv->master, lv->level[kind], trim);
}

int pipos_levels_image_snd(int image_kind)
{
    return image_kind == PIP_KIND_HOLOTAPE ? PIPOS_SND_GAMES : PIPOS_SND_SFX;
}

const char *pipos_levels_key(int kind)
{
    if (kind < 0 || kind >= PIPOS_SND_KINDS) {
        return "unknown";
    }
    return level_keys[kind];
}

void pipos_image_snd_start(pipos_image_snd_t *s, int image_kind)
{
    if (image_kind == PIP_KIND_HOLOTAPE) {
        s->rom_snd = s->snd;
        s->trim = PIPOS_LEVEL_MAX;
    }
    s->snd = pipos_levels_image_snd(image_kind);
}

void pipos_image_snd_end(pipos_image_snd_t *s, int image_kind)
{
    if (image_kind == PIP_KIND_HOLOTAPE) {
        s->snd = s->rom_snd;
        s->trim = PIPOS_LEVEL_MAX;
    }
}

int32_t pipos_image_snd_swap(pipos_image_snd_t *s, int image_kind, uint32_t kind)
{
    int prev = s->snd;

    if (kind >= PIPOS_SND_KINDS) {
        return -PIP_EINVAL;
    }
    if (image_kind == PIP_KIND_HOLOTAPE && kind != PIPOS_SND_GAMES &&
        kind != PIPOS_SND_RECORDINGS) {
        return -PIP_EPERM;
    }
    s->snd = (int)kind;
    return prev;
}

int32_t pipos_levels_swap(pipos_levels_t *lv, int image_kind, uint32_t which, uint32_t level)
{
    unsigned *slot;
    unsigned prev;

    if (which > PIPOS_LEVEL_MASTER || (level > PIPOS_LEVEL_MAX && level != PIPOS_LEVEL_ASK)) {
        return -PIP_EINVAL;
    }
    slot = which == PIPOS_LEVEL_MASTER ? &lv->master : &lv->level[which];
    prev = *slot;
    if (level == PIPOS_LEVEL_ASK) {
        return (int32_t)prev;
    }
    if (image_kind == PIP_KIND_HOLOTAPE) {
        return -PIP_EPERM;
    }
    *slot = level;
    return (int32_t)prev;
}

int32_t pipos_levels_volume(pipos_levels_t *lv, pipos_image_snd_t *s, int image_kind,
                            uint32_t level)
{
    unsigned *slot = image_kind == PIP_KIND_HOLOTAPE ? &s->trim : &lv->master;
    unsigned prev = *slot;

    if (level > PIPOS_LEVEL_MAX) {
        return -PIP_EINVAL;
    }
    *slot = level;
    return (int32_t)prev;
}

/* The keys of PIPOS.SET in the order of the file. Slot 0 is the master. */
#define FILE_KEYS 5

static const char *const file_keys[FILE_KEYS] = {
    "volume", "radio", "sfx", "recordings", "games",
};

/* After the master, the file's keys follow the PIPOS_SND_* numbers. */
_Static_assert(PIPOS_SND_RADIO == 0 && PIPOS_SND_SFX == 1 && PIPOS_SND_RECORDINGS == 2 &&
               PIPOS_SND_GAMES == 3, "file key i is level i - 1");

/* The reasons, in the order the spec ranks them. 0 is none. */
enum { BAD_NONE, BAD_SIZE, BAD_SYNTAX, BAD_KEY, BAD_VALUE };

typedef struct {
    unsigned value[FILE_KEYS];
    unsigned seen;          /* bit i: file_keys[i] was read */
    int bad;                /* the first BAD_* found, by rank */
} file_ctx_t;

static void mark(file_ctx_t *f, int bad)
{
    if (f->bad == BAD_NONE || bad < f->bad) {
        f->bad = bad;
    }
}

/* Decimal digits only, 0 to 100. Leading zeros are fine. Returns 0 or -1. */
static int file_value(const char *text, unsigned *out)
{
    unsigned v = 0;

    if (*text == '\0') {
        return -1;
    }
    for (; *text != '\0'; text++) {
        if (*text < '0' || *text > '9') {
            return -1;
        }
        v = v * 10u + (unsigned)(*text - '0');
        if (v > PIPOS_LEVEL_MAX) {
            return -1;
        }
    }
    *out = v;
    return 0;
}

static int file_section(const char *section, unsigned line, void *ctx)
{
    (void)line;
    if (k_strcmp(section, "audio") != 0) {
        mark(ctx, BAD_KEY);
    }
    return 0;
}

static int file_key(const char *section, const char *key, const char *value, unsigned line,
                    void *ctx)
{
    file_ctx_t *f = ctx;
    int i;

    (void)line;
    if (k_strcmp(section, "audio") != 0) {
        mark(f, BAD_KEY);
        return 0;
    }
    for (i = 0; i < FILE_KEYS; i++) {
        if (k_strcmp(key, file_keys[i]) == 0) {
            break;
        }
    }
    if (i == FILE_KEYS || (f->seen & (1u << i)) != 0) {
        mark(f, BAD_KEY);
        return 0;
    }
    f->seen |= 1u << i;
    if (file_value(value, &f->value[i]) != 0) {
        mark(f, BAD_VALUE);
    }
    return 0;
}

const char *pipos_levels_read_text(const char *text, size_t len, pipos_levels_t *lv)
{
    static const char *const reasons[] = { NULL, "size", "syntax", "key", "value" };
    file_ctx_t f;
    int i;

    k_memset(&f, 0, sizeof(f));
    if (len > PIPOS_LEVELS_FILE_MAX) {
        return reasons[BAD_SIZE];
    }
    /* Keys reach the handlers up to a bad line: a syntax error outranks them. */
    if (ini_parse_sections(text, len, file_key, file_section, &f, NULL) != 0) {
        return reasons[BAD_SYNTAX];
    }
    if (f.bad != BAD_NONE) {
        return reasons[f.bad];
    }
    for (i = 0; i < FILE_KEYS; i++) {
        if ((f.seen & (1u << i)) == 0) {
            continue;
        }
        if (i == 0) {
            lv->master = f.value[i];
        } else {
            lv->level[i - 1] = f.value[i];
        }
    }
    return NULL;
}

size_t pipos_levels_write_text(const pipos_levels_t *lv, char *out, size_t size)
{
    int n;

    if (out == NULL || size < PIPOS_LEVELS_TEXT_MAX + 1u) {
        return 0;
    }
    n = k_snprintf(out, size,
                   "[audio]\nvolume = %u\nradio = %u\nsfx = %u\nrecordings = %u\ngames = %u\n",
                   (unsigned)cap(lv->master), (unsigned)cap(lv->level[PIPOS_SND_RADIO]),
                   (unsigned)cap(lv->level[PIPOS_SND_SFX]),
                   (unsigned)cap(lv->level[PIPOS_SND_RECORDINGS]),
                   (unsigned)cap(lv->level[PIPOS_SND_GAMES]));
    return n > 0 ? (size_t)n : 0;
}
