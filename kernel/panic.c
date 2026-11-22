#include <robco/boot.h>
#include <robco/console.h>
#include <robco/irq.h>
#include <robco/klibc.h>
#include <robco/uart.h>

#include "bootlines.h"
#include "pipos.h"

void pipos_panic(const char *why)
{
    char word[PIPOS_LINE_MAX];

    irq_cpu_disable();
    pipos_marker_word(why, word, sizeof(word));
    k_printf("\n@RC stage=" PIPOS_STAGE " ev=panic msg=%s\n", word);
    /* Does nothing when the console never came up. */
    console_set_attr(CONSOLE_ATTR_BRIGHT);
    console_puts("\nKERNEL PANIC: ");
    console_puts(why);
    console_puts("\nSYSTEM HALTED\n");
    uart_flush();
    robco_halt();
}
