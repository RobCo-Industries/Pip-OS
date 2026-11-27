/*
 * The sound levels: the master and one level for each kind of sound, and
 * the gain of a play. Pure code: it builds and runs on the host in the
 * unit tests.
 */
#ifndef PIPOS_LEVELS_H
#define PIPOS_LEVELS_H

#include <stddef.h>
#include <stdint.h>

/* The kinds of sound, with the numbers of the ABI spec's PIP_SND_* values. */
#define PIPOS_SND_RADIO       0
#define PIPOS_SND_SFX         1
#define PIPOS_SND_RECORDINGS  2
#define PIPOS_SND_GAMES       3
#define PIPOS_SND_KINDS       4

/* Every level, the master and the trim run from 0 to this. */
#define PIPOS_LEVEL_MAX       100u
/* A gain in units of 1/10000: this is full scale. */
#define PIPOS_GAIN_FULL       10000u

typedef struct {
    unsigned master;                    /* the volume call's level */
    unsigned level[PIPOS_SND_KINDS];    /* by PIPOS_SND_* */
} pipos_levels_t;

/* The master and every level at PIPOS_LEVEL_MAX. */
void pipos_levels_default(pipos_levels_t *lv);

/*
 * gain = (master * level * level * trim + 5000) / 10000, each term capped
 * at PIPOS_LEVEL_MAX. The level follows a square law, the master and the
 * trim are linear. At level and trim 100 the gain is master * 100.
 */
uint32_t pipos_levels_gain(unsigned master, unsigned level, unsigned trim);

/* The gain of a play of `kind`. A kind out of range counts as sfx. */
uint32_t pipos_levels_gain_of(const pipos_levels_t *lv, int kind, unsigned trim);

/*
 * The kind an image's sounds take: sfx for a ROM, games for a holotape.
 * image_kind is a PIP_KIND_* value of the ABI.
 */
int pipos_levels_image_snd(int image_kind);

/* The key of a level in [audio] of PIPOS.CFG: "radio", "sfx", "recordings", "games". */
const char *pipos_levels_key(int kind);

/* sound_level's `which` for the master, and its level that reads. Equal to the ABI's. */
#define PIPOS_LEVEL_MASTER    4u
#define PIPOS_LEVEL_ASK       0xFFFFFFFFu

/*
 * The sound state of the image that runs. The kind belongs to the image,
 * like the sample rate: snd is the one in force, rom_snd the ROM's while a
 * holotape it launched runs. trim is the holotape's volume, 0 to 100. It
 * is 100 while no holotape runs.
 */
typedef struct {
    int snd;
    int rom_snd;
    unsigned trim;
} pipos_image_snd_t;

#define PIPOS_IMAGE_SND_INIT { PIPOS_SND_SFX, PIPOS_SND_SFX, PIPOS_LEVEL_MAX }

/*
 * A ROM starts as sfx. A holotape starts as games with a trim of 100, and
 * its start keeps the ROM's kind. image_kind is a PIP_KIND_* value.
 */
void pipos_image_snd_start(pipos_image_snd_t *s, int image_kind);

/* A holotape's end gives the ROM back its kind and sets the trim to 100. */
void pipos_image_snd_end(pipos_image_snd_t *s, int image_kind);

/*
 * sound_kind for a caller of image_kind. -PIP_EINVAL for a kind above 3,
 * then -PIP_EPERM for radio or sfx from a holotape. Else sets the kind in
 * force and returns the kind before. A refused call changes nothing.
 */
int32_t pipos_image_snd_swap(pipos_image_snd_t *s, int image_kind, uint32_t kind);

/*
 * sound_level for a caller of image_kind. which is a kind or
 * PIPOS_LEVEL_MASTER. -PIP_EINVAL for a which above 4 or a level that is
 * neither 0 to 100 nor PIPOS_LEVEL_ASK. PIPOS_LEVEL_ASK returns the level
 * and changes nothing. Then -PIP_EPERM when a holotape sets a level. Else
 * sets it and returns the level before. A refused call changes nothing.
 */
int32_t pipos_levels_swap(pipos_levels_t *lv, int image_kind, uint32_t which, uint32_t level);

/*
 * volume by its caller: a ROM sets the master, a holotape its trim. Each
 * returns the value before. -PIP_EINVAL for a level above 100, which
 * changes nothing.
 */
int32_t pipos_levels_volume(pipos_levels_t *lv, pipos_image_snd_t *s, int image_kind,
                            uint32_t level);

/*
 * The stored levels, /PIPBOY/PIPOS.SET: INI text with one section,
 * [audio], and the keys volume, radio, sfx, recordings and games.
 */
#define PIPOS_LEVELS_FILE_MAX 512u  /* a larger file is not valid */
#define PIPOS_LEVELS_TEXT_MAX 72u   /* what pipos_levels_write_text() writes, at most */

/*
 * Reads the text of the file into *lv. The file counts only as a whole:
 * on success each key present replaces its value and an absent key keeps
 * it. Returns NULL then. Otherwise *lv is unchanged and the result is the
 * reason: "size" past PIPOS_LEVELS_FILE_MAX bytes, "syntax" when the INI
 * parser refuses the text, "key" for another section, another key or a
 * key twice, "value" for a value that is not 0 to 100 in decimal digits.
 * When several apply, the first in that order wins.
 */
const char *pipos_levels_read_text(const char *text, size_t len, pipos_levels_t *lv);

/*
 * Writes *lv as six lines, "[audio]" then "key = N" for volume, radio,
 * sfx, recordings and games, each ended by one line feed. A value above
 * PIPOS_LEVEL_MAX is written as PIPOS_LEVEL_MAX. Returns the length,
 * without the NUL it adds, or 0 when out holds fewer than
 * PIPOS_LEVELS_TEXT_MAX + 1 bytes.
 */
size_t pipos_levels_write_text(const pipos_levels_t *lv, char *out, size_t size);

#endif
