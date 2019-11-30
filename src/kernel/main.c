#include "uart.h"

#include <stddef.h>
#include <stdint.h>

void kernel_main(uint32_t r0, uint32_t r1, uint32_t atags)
{
    (void) r0;
    (void) r1;
    (void) atags;

    uart_init();

    uart_puts("*************** PIP-OS(R) V0.1.0.0 ***************\r\n\r\n");
    uart_puts("COPYRIGHT 2019 ROBCO(R)\r\n");
    uart_puts("LOADER V0.1\r\n");
    uart_puts("EXEC VERSION 0.1\r\n");
    uart_puts("???K RAM SYSTEM\r\n");
    uart_puts("38911 BYTES FREE\r\n");
    uart_puts("NO HOLOTAPE FOUND\r\n");
    uart_puts("LOAD ROM(1): DEITRIX 303\r\n");
    uart_puts("**************************************************\r\n");

    while (1) {
        uart_putc(uart_getc());
    }
}