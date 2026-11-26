/*
 * Boot text goes to the screen console and to the UART. Markers go to the
 * UART only, through k_printf(), so the screen shows only the boot text.
 */
#include <robco/audio.h>
#include <robco/console.h>
#include <robco/timer.h>
#include <robco/uart.h>

#include "display.h"
#include "keyclick.h"
#include "pipos.h"

static uint32_t line_delay_ms;
static uint32_t char_delay_ms;
static int key_click;
static int lines_shown;

void pipos_screen_delay(uint32_t ms)
{
    line_delay_ms = ms;
}

void pipos_screen_typing(uint32_t delay, int click)
{
    char_delay_ms = delay;
    key_click = pipos_click_on(delay, click);
    /* The block cursor marks the writing place while the text writes itself. */
    console_cursor_show(delay > 0);
}

/*
 * Writes text on the screen console one character at a time. A click waits
 * for the sound before it, the power-on beep or the last click, to end;
 * then the character and its click come together. The block cursor already
 * stands at the writing place during that wait. A space waits for nothing.
 */
static const struct pipos_type_ops type_ops = {
    audio_wait, console_putc, audio_tone, timer_delay_ms,
};

static void type_line(const char *text)
{
    unsigned i;

    for (i = 0; text[i] != '\0'; i++) {
        pipos_type_char(text[i], i, char_delay_ms, key_click, &type_ops);
    }
    if (key_click) {
        audio_wait(); /* the line's last click ends before the line goes out */
    }
}

void pipos_screen_line(const char *text)
{
    if (lines_shown++ > 0 && line_delay_ms > 0) {
        timer_delay_ms(line_delay_ms);
    }
    audio_poll(); /* prints audio trace records queued so far */
    if (char_delay_ms > 0) {
        type_line(text);
    } else {
        console_puts(text);
    }
    console_putc('\n');
    pipos_display_line(text);
    uart_puts(text);
    uart_putc('\n');
}
