#include <common/stdio.h>

/* For the moment standard IO uses UART methods ... */
#include <kernel/uart.h>

char getc(void) {
    return uart_getc();
}

void putc(char c) {
    uart_putc(c);
}

void puts(const char * str) {
    int i;
    for (i = 0; str[i] != '\0'; i ++)
        putc(str[i]);
}
