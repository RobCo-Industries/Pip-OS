#if USE_MINI_UART
#include "uart.h"

#include <stddef.h>
#include <stdint.h>

// More info on OSDev wiki : https://wiki.osdev.org/Raspberry_Pi_Bare_Bones

void uart_init()
{
	uint32_t selector;

	//Enable mini uart (this also enables access to it registers)
	mmio_write(AUX_ENABLES, 1);

	//Disable auto flow control and disable receiver and transmitter (for now)
	mmio_write(AUX_MU_CNTL_REG, 0);

	//Disable receive and transmit interrupts
	mmio_write(AUX_MU_IER_REG, 0);

	//Enable 8 bit mode
	mmio_write(AUX_MU_LCR_REG, 3);

	//Set RTS line to be always high
	mmio_write(AUX_MU_MCR_REG, 0);

	// Disable interrupts
	mmio_write(AUX_MU_IIR_REG, 0xc6);

	//Set baud rate to 115200
	mmio_write(AUX_MU_BAUD_REG, 270);
	
	// Setup the GPIO pin 14 && 15.
	selector = mmio_read(GPFSEL1);
	selector &=~((7<<12)|(7<<15)); // gpio14, gpio15
	selector |=(2<<12)|(2<<15);    // alt5
	mmio_write(GPFSEL1, selector);

	// Disable pull up/down for all GPIO pins & delay for 150 cycles.
	mmio_write(GPPUD, 0x00000000);
	delay(150);

	// Disable pull up/down for pin 14,15 & delay for 150 cycles.
	mmio_write(GPPUDCLK0, (1 << 14) | (1 << 15));
	delay(150);

	// Write 0 to GPPUDCLK0 to make it take effect.
	mmio_write(GPPUDCLK0, 0x00000000);

	//Finally, enable transmitter and receiver
	mmio_write(AUX_MU_CNTL_REG, 3);
}

void uart_putc(char byte)
{
	// Wait for mini UART to become ready to transmit.
	while(1) {
		if(mmio_read(AUX_MU_LSR_REG) & 0x20) 
			break;
	}
	mmio_write(AUX_MU_IO_REG, byte);
}

char uart_getc()
{
    // Wait for mini UART to have recieved something.
    while(1) {
		if(mmio_read(AUX_MU_LSR_REG) & 0x01) 
			break;
	}
    return (mmio_read(AUX_MU_IO_REG) & 0xFF);
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

#endif // USE_MINI_UART
