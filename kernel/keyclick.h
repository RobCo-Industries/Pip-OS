/*
 * The writing sound of the boot text: one short square-wave click per
 * character. Pure code: it builds and runs on the host in the unit tests.
 *
 * The shape, and why:
 * - A click, not a note. 4 ms of square wave is about a dozen cycles: the
 *   ear hears a dry tick, not a pitch it could hum. The driver changes sound
 *   only on a tick, so at the kernel's 100 Hz tick each click sounds for one
 *   tick period, about 10 ms. The 4 ms is the length asked for; the tick
 *   decides what plays. It still ends well inside a 20 ms character.
 * - Bright. 3000 to 3600 Hz sits in the 2 to 6 kHz band where a click has
 *   its bite, where the ear is most sensitive and where a small speaker or a
 *   piezo is loud. A square wave adds odd harmonics at 3, 5 and 7 times the
 *   pitch, so the top of the band gets energy too. It is far above the
 *   1000 Hz power-on beep, so the two do not blur.
 * - It moves. The step is the character's code plus its place in the line,
 *   modulo 5. Words wobble a little, and a run of equal characters, such as
 *   the 15 stars of the title line, walks up the five steps and starts again
 *   instead of droning on one pitch. The same text always sounds the same.
 *   No random source: two boots sound alike and a test can state each pitch.
 * - A space is silent, like a pause between words. So is any character
 *   outside printable ASCII.
 */
#ifndef PIPOS_KEYCLICK_H
#define PIPOS_KEYCLICK_H

#include <stdint.h>

#define PIPOS_CLICK_BASE_HZ   3000u  /* pitch of the lowest step */
#define PIPOS_CLICK_STEP_HZ   150u   /* distance between two steps */
#define PIPOS_CLICK_STEPS     5u     /* steps: 3000, 3150, 3300, 3450, 3600 Hz */
#define PIPOS_CLICK_MS        4u     /* asked length; the tick rounds it up */

/* [display] char_delay_ms: 0 to this many milliseconds. */
#define PIPOS_CHAR_DELAY_MAX_MS 100u

/*
 * The pitch in Hz of the click of c, the character at index pos of its
 * line, or 0 for no click: a space or a character that is not printable.
 */
uint32_t pipos_click_hz(char c, unsigned pos);

/*
 * How long writing text takes at delay ms per character, at the least:
 * every character waits delay. 0 when delay is 0: the line appears at once.
 */
uint32_t pipos_type_ms(const char *text, uint32_t delay);

/* 1 when the boot text is written one character at a time with clicks. */
int pipos_click_on(uint32_t delay, int key_click);

#endif
