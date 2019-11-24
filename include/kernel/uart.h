#ifndef __UART_H__
#define __UART_H__

#include <kernel/io.h>

#include <stddef.h>
#include <stdint.h>

// Memory-Mapped I/O output
static inline void mmio_write(uint32_t reg, uint32_t data)
{
	*(volatile uint32_t*)reg = data;
}

// Memory-Mapped I/O input
static inline uint32_t mmio_read(uint32_t reg)
{
	return *(volatile uint32_t*)reg;
}

// Loop <delay> times in a way that the compiler won't optimize away
static inline void delay(int32_t count)
{
	asm volatile("__delay_%=: subs %[count], %[count], #1; bne __delay_%=\n"
		 : "=r"(count): [count]"0"(count) : "cc");
}

void uart_init();
void uart_putc(char byte);
char uart_getc();
void uart_write(const char *buffer, size_t size);
void uart_puts(const char *str);

#endif // __UART_H__
