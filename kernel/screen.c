/*
 * Boot text goes to the screen console and to the UART. Markers go to the
 * UART only, through k_printf(), so the screen shows only the boot text.
 */
#include <robco/audio.h>
#include <robco/console.h>
#include <robco/uart.h>

#include "display.h"
#include "keyclick.h"
#include "pipos.h"

static uint32_t line_delay_ms;
static uint32_t char_delay_ms;
static int lines_shown;

void pipos_screen_delay(uint32_t ms)
{
    line_delay_ms = ms;
}

void pipos_screen_typing(uint32_t delay)
{
    char_delay_ms = delay;
    /* The block cursor marks the writing place while the text writes itself. */
    console_cursor_show(delay > 0);
}

/* Writes text on the screen console one character at a time, in silence. */
static const struct pipos_type_ops type_ops = {
    console_putc, pipos_dsound_delay,
};

static void type_line(const char *text)
{
    unsigned i;

    for (i = 0; text[i] != '\0'; i++) {
        pipos_type_char(text[i], char_delay_ms, &type_ops);
    }
}

void pipos_screen_line(const char *text)
{
    unsigned owed = 0;

    if (lines_shown++ > 0 && line_delay_ms > 0) {
        pipos_dsound_delay(line_delay_ms);
    }
    if (lines_shown == 2) {
        pipos_dsound_note(char_delay_ms, line_delay_ms); /* the note rises with line 2 */
    }
    audio_poll(); /* prints audio trace records queued so far */
    if (char_delay_ms > 0) {
        owed = pipos_dsound_line(text);
        type_line(text);
    } else {
        console_puts(text);
    }
    console_putc('\n');
    pipos_display_line(text);
    uart_puts(text);
    uart_putc('\n');
    if (char_delay_ms > 0) {
        pipos_dsound_line_done(owed);
    }
}
