/*
 * The boot text that writes itself, one character at a time. Pure code: it
 * builds and runs on the host in the unit tests.
 *
 * Typing makes no sound. With [audio] system_sound on, the boot plays the
 * rest ramp and the soft beep in main.c instead.
 */
#ifndef PIPOS_KEYCLICK_H
#define PIPOS_KEYCLICK_H

#include <stdint.h>

/* [display] char_delay_ms: 0 to this many milliseconds. */
#define PIPOS_CHAR_DELAY_MAX_MS 100u

/*
 * How long writing text takes at delay ms per character, at the least:
 * every character waits delay. 0 when delay is 0: the line appears at once.
 */
uint32_t pipos_type_ms(const char *text, uint32_t delay);

/*
 * The steps that write one character, as calls, so a host test can check
 * their order. draw puts c on the screen; delay keeps the character's pace.
 */
struct pipos_type_ops {
    void (*draw)(char c);
    void (*delay)(uint32_t ms);
};

/* Draws c, then keeps delay ms. No character waits for a sound. */
void pipos_type_char(char c, uint32_t delay, const struct pipos_type_ops *ops);

#endif
