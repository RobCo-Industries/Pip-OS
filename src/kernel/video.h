#ifndef __VIDEO__
#define __VIDEO__

#include <stdint.h>

#define RED_CHANNEL    0x000000FF
#define GREEN_CHANNEL  0x0000FF00
#define BLUE_CHANNEL   0x00FF0000
#define ALPHA_CHANNEL  0xFF000000

uint32_t rgba_to_uint32(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
void uint32_to_rgba(uint32_t rgb, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a);
void video_set_resolution(uint32_t width, uint32_t height, uint32_t depth);

#endif // __VIDEO__