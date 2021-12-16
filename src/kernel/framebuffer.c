#include "framebuffer.h"
#include "mailbox.h"

void initializeFrameBuffer (fb_info_t * fbInfo, uint32_t width, uint32_t height, uint32_t depth)
{
  fbInfo->width = width;
  fbInfo->height = height;
  fbInfo->vWidth = width;
  fbInfo->vHeight = height;
  fbInfo->depth = depth;
  fbInfo->yOffset = 0;
  fbInfo->xOffset = 0;
  fbInfo->fbSize = 0;
  fbInfo->pitch = 0;
  fbInfo->fb = 0;
  
  /* write the fbInfo to mailbox 0, FRAMEBUFFER channel and await a response */
  mailbox_write((uint32_t)fbInfo, MB_CHANNEL_FB);
  mailbox_read(MB_CHANNEL_FB);
}

void drawSquareLoop (fb_info_t * fbInfo)
{
  volatile uint8_t * pix = (volatile uint8_t *) (fbInfo->fb & (~MAIL_L2_BYPASS_MASK));

  while (1) {
    for (uint32_t i = 0; i < fbInfo->fbSize; i++) {
      pix[i] = 0xFF;
    }
  }
}

void fbPutPixel (fb_info_t * fbInfo, uint32_t x, uint32_t y, uint32_t color)
{
  /* get the byte offset of the pixel and write in the color */
  uint32_t offset = (y * fbInfo->pitch) + (x << 2);
  uint32_t * pixel = (uint32_t *) (fbInfo->fb + offset);
  *pixel = color;
}