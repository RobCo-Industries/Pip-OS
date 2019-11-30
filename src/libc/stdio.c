#include <stdio.h>

/* For the moment standard IO uses UART methods ... */
#include "../kernel/uart.h"

char getchar(void) {
    return uart_getc();
}

void putchar(char c) {
    uart_putc(c);
}

void puts(const char * str) {
    int i;
    for (i = 0; str[i] != '\0'; i ++)
        putchar(str[i]);
}
