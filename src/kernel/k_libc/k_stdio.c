#include "k_stdio.h"

#include "../uart.h"

void k_putchar(char c)
{
    uart_putc(c);
}