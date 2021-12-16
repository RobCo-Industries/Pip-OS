#include "uart.h"

#include <stddef.h>
#include <stdint.h>

#include "framebuffer.h"
#include "mailbox.h"
#include "k_libc/k_stdio.h"
#include "video.h"

void kernel_main(uint32_t r0, uint32_t r1, uint32_t atags)
{
    (void) r0;
    (void) r1;
    (void) atags;

    uart_init();

    k_printf("*************** PIP-OS(R) V0.1.0.0 ***************\r\n\r\n");
    k_printf("COPYRIGHT 2019 ROBCO(R)\r\n");
    k_printf("LOADER V0.1\r\n");
    k_printf("EXEC VERSION 0.1\r\n");
    k_printf("???K RAM SYSTEM\r\n");
    k_printf("38911 BYTES FREE\r\n");
    k_printf("NO HOLOTAPE FOUND\r\n");
    k_printf("LOAD ROM(1): DEITRIX 303\r\n");
    k_printf("**************************************************\r\n");

    k_printf("Core clock : %d\r\n", mailbox_get_id(MAILBOX_TAG_GET_CLOCK_RATE, MAIL_CLOCK_CORE));
    k_printf("EMMC clock : %d\r\n", mailbox_get_id(MAILBOX_TAG_GET_CLOCK_RATE, MAIL_CLOCK_EMMC));
    k_printf("UART clock : %d\r\n", mailbox_get_id(MAILBOX_TAG_GET_CLOCK_RATE, MAIL_CLOCK_UART));
    k_printf("ARM  clock : %d\r\n", mailbox_get_id(MAILBOX_TAG_GET_CLOCK_RATE, MAIL_CLOCK_ARM));

    k_printf("Firmware version : %d\r\n", mailbox_get(MAILBOX_TAG_GET_VERSION));
    k_printf("Board model : %d\r\n", mailbox_get(MAILBOX_TAG_GET_BOARD_MODEL));
    k_printf("Board rev   : %d\r\n", mailbox_get(MAILBOX_TAG_GET_BOARD_REVISION));
    //k_printf("Board MAC   : %d\r\n", mailbox_get(MAILBOX_TAG_GET_BOARD_MAC_ADDRESS));

    //video_set_resolution(640, 480, 32);

    

    fb_info_t fbInfo;
    initializeFrameBuffer(&fbInfo, 640, 480, 32);

    uint32_t mb_addr = 0x40007000;
    volatile uint32_t *mailbuffer = (volatile uint32_t*)mb_addr;

    /* Get the display size */
    mailbuffer[0] = 8 * 4;              // size of this message
    mailbuffer[1] = RPI_FIRMWARE_STATUS_REQUEST;                  // this is a request
    mailbuffer[2] = MAILBOX_TAG_GET_VIRTUAL_WIDTH_HEIGHT;    // get physical width/height tag
    mailbuffer[3] = 8;                  // value buffer size
    mailbuffer[4] = 0;                  // request/response
    mailbuffer[5] = 0;                  // space to return width
    mailbuffer[6] = 0;                  // space to return height
    mailbuffer[7] = RPI_FIRMWARE_STATUS_REQUEST;

    mailbox_write(mb_addr, MB_CHANNEL_TAGS);

    mailbox_read(MB_CHANNEL_TAGS);

    k_printf("%dx%d\r\n", mailbuffer[5], mailbuffer[6]);

    for (int x = 0; x < 640; x++)
    {
        for (int y = 0; y < 480; y++)
        {
            fbPutPixel(&fbInfo, x, y, rgba_to_uint32(x * 0xFF / 640, 0, 0xFF, 0xFF));
        }
    }

    while (1) {
        uart_putc(uart_getc());
    }
}