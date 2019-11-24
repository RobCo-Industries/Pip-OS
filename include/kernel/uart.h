#ifndef __UART_H__
#define __UART_H__

#include <stddef.h>

void uart_init();
void uart_putc(char byte);
char uart_getc();
void uart_write(const char *buffer, size_t size);
void uart_puts(const char *str);
void uart_printf(const char *format, ...);

#endif // __UART_H__
