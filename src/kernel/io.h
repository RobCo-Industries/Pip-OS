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
    PERIPHERAL_BASE = 0x20000000, // 0x7E000000 in documentation ?
#endif

    // The mailbox base address.
    MAIL_BASE       = (PERIPHERAL_BASE + 0xB880),

    // The offsets for each mailbox register.
    MAIL_READ       = (MAIL_BASE + 0x00),
    MAIL_WRITE      = (MAIL_BASE + 0x20),
    MAIL_RSTATUS    = (MAIL_BASE + 0x18),
    MAIL_WSTATUS    = (MAIL_BASE + 0x38),

    // Masks for the mailbox status register.
    MAIL_EMPTY      = 0x40000000,
    MAIL_FULL       = 0x80000000,

    // The GPIO registers base address.
    GPIO_BASE       = (PERIPHERAL_BASE + 0x200000),

    GPFSEL1         = (GPIO_BASE + 0x04),
    GPSET0          = (GPIO_BASE + 0x1C),
    GPCLR0          = (GPIO_BASE + 0x28),

    // Controls actuation of pull up/down to ALL GPIO pins.
    GPPUD           = (GPIO_BASE + 0x94),

    // Controls actuation of pull up/down for specific GPIO pin.
    GPPUDCLK0       = (GPIO_BASE + 0x98),

    // The base address for UART.
    UART0_BASE      = (GPIO_BASE + 0x1000),

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

    // The offsets for each registers of Auxiliaries
    // There are 3 AUX : Mini UART, and 2 SP1 masters
    AUX_BASE        = (GPIO_BASE + 0x15000),
    AUX_IRQ         = (AUX_BASE + 0x00),
    AUX_ENABLES     = (AUX_BASE + 0x04),
    AUX_MU_IO_REG   = (AUX_BASE + 0x40),
    AUX_MU_IER_REG  = (AUX_BASE + 0x44),
    AUX_MU_IIR_REG  = (AUX_BASE + 0x48),
    AUX_MU_LCR_REG  = (AUX_BASE + 0x4C),
    AUX_MU_MCR_REG  = (AUX_BASE + 0x50),
    AUX_MU_LSR_REG  = (AUX_BASE + 0x54),
    AUX_MU_MSR_REG  = (AUX_BASE + 0x58),
    AUX_MU_SCRATCH  = (AUX_BASE + 0x5C),
    AUX_MU_CNTL_REG     = (AUX_BASE + 0x60),
    AUX_MU_STAT_REG     = (AUX_BASE + 0x64),
    AUX_MU_BAUD_REG     = (AUX_BASE + 0x68),
    AUX_SPI0_CNTL0_REG  = (AUX_BASE + 0x80),
    AUX_SPI0_CNTL1_REG  = (AUX_BASE + 0x84),
    AUX_SPI0_STAT_REG   = (AUX_BASE + 0x88),
    AUX_SPI0_IO_REG     = (AUX_BASE + 0x90),
    AUX_SPI0_PEEK_REG   = (AUX_BASE + 0x94),
    AUX_SPI1_CNTL0_REG  = (AUX_BASE + 0xC0),
    AUX_SPI1_CNTL1_REG  = (AUX_BASE + 0xC4),
    AUX_SPI1_STAT_REG   = (AUX_BASE + 0xC8),
    AUX_SPI1_IO_REG     = (AUX_BASE + 0xD0),
    AUX_SPI1_PEEK_REG   = (AUX_BASE + 0xD4),
};

// Mailbox channels.
typedef enum {
    MB_CHANNEL_POWER = 0x0,								// Mailbox Channel 0: Power Management Interface 
	MB_CHANNEL_FB = 0x1,								// Mailbox Channel 1: Frame Buffer
	MB_CHANNEL_VUART = 0x2,								// Mailbox Channel 2: Virtual UART
	MB_CHANNEL_VCHIQ = 0x3,								// Mailbox Channel 3: VCHIQ Interface
	MB_CHANNEL_LEDS = 0x4,								// Mailbox Channel 4: LEDs Interface
	MB_CHANNEL_BUTTONS = 0x5,							// Mailbox Channel 5: Buttons Interface
	MB_CHANNEL_TOUCH = 0x6,								// Mailbox Channel 6: Touchscreen Interface
	MB_CHANNEL_COUNT = 0x7,								// Mailbox Channel 7: Counter
	MB_CHANNEL_TAGS = 0x8,								// Mailbox Channel 8: Tags (ARM to VC)
	MB_CHANNEL_GPU = 0x9,								// Mailbox Channel 9: GPU (VC to ARM)
} MAILBOX_CHANNEL;

typedef enum {
    RPI_FIRMWARE_STATUS_REQUEST = 0,
    RPI_FIRMWARE_STATUS_SUCCESS = 0x80000000,
    RPI_FIRMWARE_STATUS_ERROR   = 0x80000001,
} RPI_FIRMWARE_PROPERTY_STATUS;

#endif // __IO_H__