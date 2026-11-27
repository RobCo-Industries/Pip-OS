#ifndef ROBCO_CONFIG_H
#define ROBCO_CONFIG_H

/* Preprocessor macros only: this file is also forced into asm and the linker script. */

#define ROBCO_STAGE_NAME        "pipos"
#define ROBCO_FIRST_STAGE       1

#define ROBCO_IMG_MAGIC         RIMG_MAGIC_BOOT
#define ROBCO_IMG_SUBTYPE       RIMG_BOOT_PIPOS
#define ROBCO_IMG_ABI_VERSION   (2 << 16)
/* Pip-OS version. Header image version: (major << 16) | (minor << 8) | patch. */
#define PIPOS_VERSION_MAJOR     7
#define PIPOS_VERSION_MINOR     1
#define PIPOS_VERSION_PATCH     0
#define ROBCO_IMG_VERSION       ((PIPOS_VERSION_MAJOR << 16) | (PIPOS_VERSION_MINOR << 8) | \
                                 PIPOS_VERSION_PATCH)
#define ROBCO_IMG_NAME          "PIPOS"

/* The Pip-OS slot, 0x00008000 to 0x003FFFFF. The link fails if the image does not fit. */
#ifndef ROBCO_SLOT_END
#define ROBCO_SLOT_END          0x00400000
#endif

/* Periodic tick rate. The GPIO input backend samples at this rate. */
#define PIPOS_TICK_HZ           100
#define PIPOS_DEBOUNCE_MS       20

/* Key repeat: the first repeat event 500 ms after a press, then one every 100 ms. */
#define PIPOS_REPEAT_DELAY_MS   500
#define PIPOS_REPEAT_RATE_MS    100

/*
 * Button and dial pins (BCM numbers). Each button ties its pin to ground
 * and uses the internal pull-up. The defaults are the example mapping of
 * RobCo-Core's examples/input. Override any of them with -D or here.
 */
#ifndef PIPOS_PIN_UP
#define PIPOS_PIN_UP            5
#endif
#ifndef PIPOS_PIN_DOWN
#define PIPOS_PIN_DOWN          6
#endif
#ifndef PIPOS_PIN_LEFT
#define PIPOS_PIN_LEFT          17
#endif
#ifndef PIPOS_PIN_RIGHT
#define PIPOS_PIN_RIGHT         22
#endif
#ifndef PIPOS_PIN_SELECT
#define PIPOS_PIN_SELECT        26
#endif
#ifndef PIPOS_PIN_BACK
#define PIPOS_PIN_BACK          27
#endif
#ifndef PIPOS_PIN_DIAL_A
#define PIPOS_PIN_DIAL_A        23
#endif
#ifndef PIPOS_PIN_DIAL_B
#define PIPOS_PIN_DIAL_B        24
#endif
#ifndef PIPOS_PIN_DIAL_PUSH
#define PIPOS_PIN_DIAL_PUSH     16
#endif
/* A/B transitions per dial detent: 1, 2 or 4. */
#ifndef PIPOS_DIAL_PER_DETENT
#define PIPOS_DIAL_PER_DETENT   4
#endif

/*
 * Tone height for the library's audio driver. Once the pin rests at the
 * midpoint, a tone at volume 100 swings this many steps each side of it,
 * from 0 to 127. 127 is the library's default. A lower value makes every
 * ROM tone softer against the system sounds. RobCo-Core's
 * tools/audio_preview.py assumes 127.
 */
#ifndef ROBCO_AUDIO_TONE_PEAK
#define ROBCO_AUDIO_TONE_PEAK   127
#endif

#endif
