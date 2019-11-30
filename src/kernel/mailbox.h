#ifndef __MAILBOX_H__
#define __MAILBOX_H__

/*
 * Mailbox implementation
 * 
 * Reference : 
 * https://github.com/raspberrypi/firmware/wiki/Mailbox-property-interface
 * https://jsandler18.github.io/extra/mailbox.html
 * 
 */

#include "io.h"
#include <stdint.h>

/* Getters */
#define MAIL_TAG_FIRMWARE_REV   0x00000001
#define MAIL_TAG_BOARD_MODEL    0x00010001
#define MAIL_TAG_BOARD_REV      0x00010002
#define MAIL_TAG_BOARD_MAC      0x00010003
#define MAIL_TAG_BOARD_SERIAL   0x00010004
#define MAIL_TAG_ARM_MEMORY     0x00010005
#define MAIL_TAG_VC_MEMORY      0x00010006
#define MAIL_TAG_CLOCKS         0x00010007
#define MAIL_TAG_COMMANDLINE    0x00050001
#define MAIL_TAG_DMA_CHANNELS   0x00060001
#define MAIL_TAG_POWER_STATE    0x00020001
#define MAIL_TAG_TIMING         0x00020002
#define MAIL_TAG_CLOCK_STATE    0x00030001
#define MAIL_TAG_CLOCK_RATE     0x00030002
#define MAIL_TAG_MAX_CLOCK_RATE 0x00030004
#define MAIL_TAG_MIN_CLOCK_RATE 0x00030007
#define MAIL_TAG_TURBO          0x00030009


/* Setters */
#define MAIL_TAG_SET_POWER_STATE    0x00028001
#define MAIL_TAG_SET_CLOCK_STATE    0x00038001
#define MAIL_TAG_SET_CLOCK_RATE     0x00038002
#define MAIL_TAG_SET_TURBO          0x00038009


/* POWER device UIDs */
#define MAIL_POWER_SDCARD 0x00000000
#define MAIL_POWER_UART0  0x00000001
#define MAIL_POWER_UART1  0x00000002
#define MAIL_POWER_USB    0x00000003
#define MAIL_POWER_I2C0   0x00000004
#define MAIL_POWER_I2C1   0x00000005
#define MAIL_POWER_I2C2   0x00000006
#define MAIL_POWER_SPI    0x00000007
#define MAIL_POWER_CCP2TX 0x00000008

/* CLOCK device UIDs */
#define MAIL_CLOCK_RESERVED 0x00000000
#define MAIL_CLOCK_EMMC     0x00000001
#define MAIL_CLOCK_UART     0x00000002
#define MAIL_CLOCK_ARM      0x00000003
#define MAIL_CLOCK_CORE     0x00000004
#define MAIL_CLOCK_V3D      0x00000005
#define MAIL_CLOCK_H264     0x00000006
#define MAIL_CLOCK_ISP      0x00000007
#define MAIL_CLOCK_SDRAM    0x00000008
#define MAIL_CLOCK_PIXEL    0x00000009
#define MAIL_CLOCK_PWM      0x0000000A
#define MAIL_CLOCK_EMMC2    0x0000000C



typedef struct {
    uint8_t channel: 4;
    uint32_t data: 28;
} mail_message_t;

typedef struct {
    uint32_t reserved: 30;
    uint8_t empty: 1;
    uint8_t full:1;
} mail_status_t;

mail_message_t mailbox_read(uint8_t channel);
void mailbox_send(mail_message_t msg, uint8_t channel);

#endif // __MAILBOX_H__