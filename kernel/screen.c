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
 * Writes text on the screen console one character at a time. A click
 * starts only once the sound before it, the power-on beep or the last
 * click, has ended, and the next character waits for the click to end:
 * the text never runs ahead of its sound and nothing queues.
 */
static void type_line(const char *text)
{
    uint32_t hz;
    unsigned i;

    for (i = 0; text[i] != '\0'; i++) {
        console_putc(text[i]);
        hz = key_click ? pipos_click_hz(text[i], i) : 0;
        if (hz != 0) {
            audio_wait();
            audio_tone(hz, PIPOS_CLICK_MS);
        }
        timer_delay_ms(char_delay_ms);
        if (hz != 0) {
            audio_wait();
        }
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
