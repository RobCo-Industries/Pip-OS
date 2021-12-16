#ifndef __FRAMEBUFFER_H__
#define __FRAMEBUFFER_H__

#include <stdint.h>

typedef struct {
    uint32_t width;   // frame width in pixels
    uint32_t height;  // frame height in pixels
    uint32_t vWidth;  // virtual width in pixels
    uint32_t vHeight; // virtual hieght in pixels
    uint32_t pitch;   // pitch (bytes per row)
    uint32_t depth;   // pixel bit depth 
    uint32_t xOffset; // horizontal offset in pixels
    uint32_t yOffset; // vertical offset in pixels
    uint32_t fb;      // pointer to the framebuffer to write to
    uint32_t fbSize;  // size of the framebuffer, ^, in bytes
} fb_info_t __attribute__((aligned(16)));

void initializeFrameBuffer (fb_info_t * fbInfo, uint32_t width, uint32_t height, uint32_t depth);
void drawSquareLoop (fb_info_t * fbInfo);
void fbPutPixel (fb_info_t * fbInfo, uint32_t x, uint32_t y, uint32_t color);

#endif // __FRAMEBUFFER_H__