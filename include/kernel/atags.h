#ifndef __ATAGS_H__
#define __ATAGS_H__

/*
 * ARM Tags
 * 
 * References : 
 * http://www.simtec.co.uk/products/SWLINUX/files/booting_article.html#appendix_tag_reference
 * 
 */

#include <stdint.h>

#define ATAG_NONE       0x00000000
#define ATAG_CORE       0x54410001
#define ATAG_MEM        0x54410002
#define ATAG_VIDEOTEXT  0x54410003
#define ATAG_RAMDISK    0x54410004
#define ATAG_INITRD2    0x54420005
#define ATAG_SERIAL     0x54410006
#define ATAG_REVISION   0x54410007
#define ATAG_VIDEOLFB   0x54410008
#define ATAG_CMDLINE    0x54410009

typedef struct {
    uint32_t size;
    uint32_t tag;
} atag_header_t;

typedef struct {
        uint32_t flags;
        uint32_t pagesize;
        uint32_t rootdev;
} atag_core_t;

typedef struct {
    uint32_t size;
    uint32_t start;
} atag_mem_t;

typedef struct {
        uint8_t         x;
        uint8_t         y;
        uint16_t        video_page;
        uint8_t         video_mode;
        uint8_t         video_cols;
        uint16_t        video_ega_bx;
        uint8_t         video_lines;
        uint8_t         video_isvga;
        uint16_t        video_points;
} atag_videotext_t;

typedef struct  {
        uint32_t flags;
        uint32_t size;
        uint32_t start;
} atag_ramdisk_t;

typedef struct  {
        uint32_t start;
        uint32_t size;
} atag_initrd2_t;

typedef struct  {
        uint32_t low;
        uint32_t high;
} atag_serialnr_t;

typedef struct  {
        uint32_t rev;
} atag_revision_t;

typedef struct {
        uint16_t             lfb_width;
        uint16_t             lfb_height;
        uint16_t             lfb_depth;
        uint16_t             lfb_linelength;
        uint32_t             lfb_base;
        uint32_t             lfb_size;
        uint8_t              red_size;
        uint8_t              red_pos;
        uint8_t              green_size;
        uint8_t              green_pos;
        uint8_t              blue_size;
        uint8_t              blue_pos;
        uint8_t              rsvd_size;
        uint8_t              rsvd_pos;
} atag_videolfb_t;

typedef struct  {
        char    cmdline[1];
} atag_cmdline_t;

typedef struct {
    atag_header_t header;
    union {
        atag_core_t        core;
        atag_mem_t         mem;
        atag_videotext_t   videotext;
        atag_ramdisk_t     ramdisk;
        atag_initrd2_t     initrd2;
        atag_serialnr_t    serialnr;
        atag_revision_t    revision;
        atag_videolfb_t    videolfb;
        atag_cmdline_t     cmdline;
    };
} atag_t;

int get_mem(atag_t tags[], atag_mem_t *mem);

#endif // __ATAGS_H__