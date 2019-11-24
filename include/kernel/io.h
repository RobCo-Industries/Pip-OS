#ifndef __IO_H__
#define __IO_H__

enum
{
#if BCM2836
    PERIPHERAL_BASE = 0x3F000000,
#elif BCM2837
    PERIPHERAL_BASE = 0x3F000000,
#elif BCM2711
    PERIPHERAL_BASE = 0xFE000000,
#else // BCM2835
    PERIPHERAL_BASE = 0x20000000,
#endif

    // The mailbox base address.
    MAIL_BASE       = PERIPHERAL_BASE + 0xB880,

    // The offsets for each mailbox register.
    MAIL_READ       = 0x00,
    MAIL_WRITE      = 0x20,
    MAIL_RSTATUS    = 0x18,
    MAIL_WSTATUS    = 0x38,

    // Masks for the mailbox status register.
    MAIL_EMPTY      = 0x40000000,
    MAIL_FULL       = 0x80000000,

    // Mailbox channels.
    MAIL_POWER      = 0,
    MAIL_FB         = 1,
    MAIL_VUART      = 2,
    MAIL_VCHIQ      = 3,
    MAIL_LED        = 4,
    MAIL_BUTTON     = 5,
    MAIL_TOUCH      = 6,
    MAIL_COUNT      = 7,
    MAIL_TAGS       = 8,

    // The GPIO registers base address.
    GPIO_BASE       = PERIPHERAL_BASE + 0x200000,

    // Controls actuation of pull up/down to ALL GPIO pins.
    GPPUD           = (GPIO_BASE + 0x94),

    // Controls actuation of pull up/down for specific GPIO pin.
    GPPUDCLK0       = (GPIO_BASE + 0x98),

    // The base address for UART.
    UART0_BASE      = GPIO_BASE + 0x1000,

    // The offsets for each register for the UART.
    UART0_DR        = (UART0_BASE + 0x00),
    UART0_RSRECR    = (UART0_BASE + 0x04),
    UART0_FR        = (UART0_BASE + 0x18),
    UART0_ILPR      = (UART0_BASE + 0x20),
    UART0_IBRD      = (UART0_BASE + 0x24),
    UART0_FBRD      = (UART0_BASE + 0x28),
    UART0_LCRH      = (UART0_BASE + 0x2C),
    UART0_CR        = (UART0_BASE + 0x30),
    UART0_IFLS      = (UART0_BASE + 0x34),
    UART0_IMSC      = (UART0_BASE + 0x38),
    UART0_RIS       = (UART0_BASE + 0x3C),
    UART0_MIS       = (UART0_BASE + 0x40),
    UART0_ICR       = (UART0_BASE + 0x44),
    UART0_DMACR     = (UART0_BASE + 0x48),
    UART0_ITCR      = (UART0_BASE + 0x80),
    UART0_ITIP      = (UART0_BASE + 0x84),
    UART0_ITOP      = (UART0_BASE + 0x88),
    UART0_TDR       = (UART0_BASE + 0x8C),

};

#endif // __IO_H__