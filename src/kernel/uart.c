#if USE_MINI_UART == 0 || !defined USE_MINI_UART
#include "uart.h"

#include <stddef.h>
#include <stdint.h>

// More info on OSDev wiki : https://wiki.osdev.org/Raspberry_Pi_Bare_Bones

void uart_init()
{
	// Disable UART0.
	mmio_write(UART0_CR, 0x00000000);

	// Disable pull up/down for all GPIO pins & delay for 150 cycles.
	mmio_write(GPPUD, 0x00000000);
	delay(150);

	// Disable pull up/down for pin 14,15 & delay for 150 cycles.
	mmio_write(GPPUDCLK0, (1 << 14) | (1 << 15));
	delay(150);

	// Write 0 to GPPUDCLK0 to make it take effect.
	mmio_write(GPPUDCLK0, 0x00000000);

	// Clear pending interrupts.
	mmio_write(UART0_ICR, 0x7FF);

	// Set integer & fractional part of baud rate.
	// Divider = UART_CLOCK/(16 * Baud)
	// Fraction part register = (Fractional part * 64) + 0.5
	// UART_CLOCK = 3000000; Baud = 115200.

	// Divider = 3000000 / (16 * 115200) = 1.627 = ~1.
	// Fractional part register = (.627 * 64) + 0.5 = 40.6 = ~40.
	mmio_write(UART0_IBRD, 1);
	mmio_write(UART0_FBRD, 40);

	// Enable FIFO & 8 bit data transmissio (1 stop bit, no parity).
	mmio_write(UART0_LCRH, (1 << 4) | (1 << 5) | (1 << 6));

	// Mask all interrupts.
	mmio_write(UART0_IMSC, (1 << 1) | (1 << 4) | (1 << 5) | (1 << 6) |
	                       (1 << 7) | (1 << 8) | (1 << 9) | (1 << 10));

	// Enable UART0, receive & transfer part of UART.
	mmio_write(UART0_CR, (1 << 0) | (1 << 8) | (1 << 9));
}

void uart_putc(char byte)
{
	// Wait for UART to become ready to transmit.
	while (mmio_read(UART0_FR) & (1 << 5)) { }
	mmio_write(UART0_DR, byte);
}

char uart_getc()
{
    // Wait for UART to have recieved something.
    while (mmio_read(UART0_FR) & (1 << 4)) { }
    return mmio_read(UART0_DR);
}

void uart_write(const char *buffer, size_t size)
{
	for (size_t i = 0; i < size; i++)
		uart_putc(buffer[i]);
}

void uart_puts(const char *str)
{
    while (*str != 0)
        uart_putc(*str++);
}
#endif // USE_MINI_UART == 0 || !defined USE_MINI_UART