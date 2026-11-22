/*
 * Boot text goes to the screen console and to the UART. Markers go to the
 * UART only, through k_printf(), so the screen shows only the boot text.
 */
#include <robco/audio.h>
#include <robco/console.h>
#include <robco/timer.h>
#include <robco/uart.h>

#include "pipos.h"

static uint32_t line_delay_ms;
static int lines_shown;

void pipos_screen_delay(uint32_t ms)
{
    line_delay_ms = ms;
}

void pipos_screen_line(const char *text)
{
    if (lines_shown++ > 0 && line_delay_ms > 0) {
        timer_delay_ms(line_delay_ms);
    }
    audio_poll(); /* prints audio trace records queued so far */
    console_puts(text);
    console_putc('\n');
    uart_puts(text);
    uart_putc('\n');
}
