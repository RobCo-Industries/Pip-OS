/*
 * Boot text goes to the screen console and to the UART. Markers go to the
 * UART only, through k_printf(), so the screen shows only the boot text.
 *
 * With typing on and a ROM to run, the text scrolls away after the last
 * line. The screen keeps the lines and writes them again when the ROM ends.
 */
#include <robco/audio.h>
#include <robco/console.h>
#include <robco/klibc.h>
#include <robco/tick.h>
#include <robco/uart.h>

#include "bootlines.h"
#include "display.h"
#include "dsound_rules.h"
#include "keyclick.h"
#include "pipos.h"

#define KEEP_MAX     32u /* boot lines kept for the screen after a ROM */
#define SCROLL_MS    40u /* one line up each step */

static uint32_t line_delay_ms;
static uint32_t char_delay_ms;
static int lines_shown;
static int note_next;       /* the next line starts the boot note */
static int last_next;       /* the next line is the last boot line */
static int quiet;           /* the gate closed at the last line */
static uint32_t quiet_ms;   /* tick_ms() when it closed */
static uint32_t end_ms;     /* tick_ms() when the last line ended */
static char kept[KEEP_MAX][PIPOS_LINE_MAX];
static const char *kept_at[KEEP_MAX];
static unsigned nkept;
static int scrolled;        /* the text left: write it again after the ROM */

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

void pipos_screen_note_next(void)
{
    note_next = 1;
}

void pipos_screen_last_next(void)
{
    last_next = 1;
}

static void keep(const char *text)
{
    if (nkept < KEEP_MAX) {
        k_snprintf(kept[nkept], PIPOS_LINE_MAX, "%s", text);
        kept_at[nkept] = kept[nkept];
        nkept++;
    }
}

void pipos_screen_line(const char *text)
{
    unsigned owed = 0;
    int last = last_next;

    last_next = 0;
    if (lines_shown++ > 0 && line_delay_ms > 0) {
        pipos_dsound_delay(line_delay_ms);
    }
    if (note_next) {
        note_next = 0;
        pipos_dsound_note(char_delay_ms, line_delay_ms); /* the note rises with line 2 */
    }
    if (last && char_delay_ms > 0) {
        quiet_ms = tick_ms();
        quiet = pipos_dsound_quiet(quiet_ms); /* the quiet second starts */
    }
    keep(text);
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
    if (last) {
        end_ms = tick_ms();
    }
}

/*
 * One line up every SCROLL_MS until the kept text has left the console, a
 * blip for each line that leaves and is not blank. Then one clear for each
 * screen.
 */
static void scroll(void)
{
    unsigned rows = console_rows();
    unsigned n = nkept < rows ? nkept : rows;
    unsigned first = nkept - n;
    unsigned i, blips = 0;
    uint32_t wait;

    while ((wait = pipos_dsound_scroll_wait(tick_ms(), end_ms, line_delay_ms, quiet,
                                            quiet_ms)) > 0) {
        pipos_dsound_delay(wait);
    }
    for (i = 0; i < n; i++) {
        console_set_cursor(0, rows - 1);
        console_putc('\n');
        if (!pipos_line_blank(kept[first + i])) {
            blips += (unsigned)pipos_dsound_blip();
        }
        pipos_dsound_delay(SCROLL_MS);
    }
    console_clear();
    pipos_display_text(NULL, 0);
    scrolled = 1;
    pipos_dsound_poll();
    k_printf("@RC stage=" PIPOS_STAGE " ev=scroll lines=%u blips=%u\n", n, blips);
}

void pipos_screen_end(int rom)
{
    pipos_dsound_note_wait();
    if (rom && char_delay_ms > 0) {
        scroll();
    } else if (quiet) {
        quiet = 0;
        pipos_dsound_reopen(); /* no ROM: the clicks go on */
    }
}

void pipos_screen_restore(void)
{
    unsigned i;

    if (!scrolled) {
        return;
    }
    scrolled = 0;
    console_clear();
    for (i = 0; i < nkept; i++) {
        console_puts(kept[i]);
        console_putc('\n');
    }
    pipos_display_text(kept_at, nkept);
}
