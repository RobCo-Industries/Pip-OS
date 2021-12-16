#include "video.h"
#include "mailbox.h"
#include "k_libc/k_stdio.h"

typedef struct {
    mailbox_tag_t tag;
    uint32_t width;
    uint32_t height;
} mailbox_fb_size_t;

typedef struct {
    mailbox_tag_t tag;
    uint32_t value;
} mailbox_fb_depth_t;

typedef struct {
    mailbox_tag_t tag;
    uint32_t base;
    uint32_t screen_size;
} mailbox_fb_buffer_t;

typedef struct {
    mailbox_tag_t tag;
    uint32_t value;
} mailbox_fb_pitch_t;

typedef struct {
    mailbox_fb_size_t native_res;
    mailbox_fb_size_t virtual_res;
    mailbox_fb_depth_t depth;
    mailbox_fb_buffer_t buffer;
    mailbox_fb_pitch_t pitch;
} mailbox_fb_request_t;

static mailbox_fb_request_t fb_req;

uint32_t rgba_to_uint32(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    uint32_t col = r;

    col |= g << 8;
    col |= b << 16;
    col |= a << 24;

    return col;
}

void uint32_to_rgba(uint32_t rgb, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a)
{
    *r = (rgb & 0x000000FF);
    *g = (rgb & 0x0000FF00) >> 8;
    *b = (rgb & 0x00FF0000) >> 16;
    *a = (rgb & 0xFF000000) >> 24;
}

void video_set_resolution(uint32_t width, uint32_t height, uint32_t depth)
{
    mailbox_fb_request_t req;

    fb_req.native_res.tag.id = MAILBOX_TAG_SET_PHYSICAL_WIDTH_HEIGHT;
    fb_req.native_res.tag.buffer_size = 8;
    fb_req.native_res.tag.value_length = 8;
    fb_req.native_res.width = width;
    fb_req.native_res.height = height;

    fb_req.virtual_res.tag.id = MAILBOX_TAG_SET_VIRTUAL_WIDTH_HEIGHT;
    fb_req.virtual_res.tag.buffer_size = 8;
    fb_req.virtual_res.tag.value_length = 8;
    fb_req.virtual_res.width = width;
    fb_req.virtual_res.height = height;

    fb_req.depth.tag.id = MAILBOX_TAG_SET_COLOUR_DEPTH;
    fb_req.depth.tag.buffer_size = 4;
    fb_req.depth.tag.value_length = 4;
    fb_req.depth.value = depth;

    fb_req.buffer.tag.id = MAILBOX_TAG_ALLOCATE_FRAMEBUFFER;
    fb_req.buffer.tag.buffer_size = 8;
    fb_req.buffer.tag.value_length = 4;
    fb_req.buffer.base = 16;
    fb_req.buffer.screen_size = 0;

    fb_req.pitch.tag.id = MAILBOX_TAG_GET_PITCH;
    fb_req.pitch.tag.buffer_size = 4;
    fb_req.pitch.tag.value_length = 4;
    fb_req.pitch.value = 0;

    mailbox_process((mailbox_tag_t *)&fb_req, sizeof(fb_req));
    k_printf("Allocated buffer : %X - %d\n", fb_req.buffer.base, fb_req.buffer.screen_size);
}