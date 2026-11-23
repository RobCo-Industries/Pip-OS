/*
 * Pip-OS ABI 1.3: syscall numbers, error codes, the startup block and
 * the structs system calls fill. Shared by the kernel and by ROM and
 * holotape code. The contract is RobCo-Core's docs/spec/PIPOS_ABI.md.
 *
 * Builds freestanding for ARM and on the host. Assembly files may
 * include it: they see the numbers only.
 */
#ifndef PIPOS_ABI_H
#define PIPOS_ABI_H

#define PIP_ABI_VERSION     0x00010003  /* (major << 16) | minor */

/* log: at most this many bytes, NUL included. */
#define PIP_LOG_MAX         256

/* Memory of a ROM, from the spec's memory map. */
#define PIP_ROM_BASE        0x00400000  /* load address and entry */
#define PIP_ROM_SIZE        0x00400000
#define PIP_ROM_HEAP_BASE   0x01000000  /* heap and stack region */
#define PIP_ROM_HEAP_REGION 0x00800000
#define PIP_ROM_STACK_TOP   0x01800000
#define PIP_STACK_SIZE      0x00010000  /* at the top of the region */
#define PIP_ROM_HEAP_SIZE   (PIP_ROM_HEAP_REGION - PIP_STACK_SIZE)

/* pipos_startup_t.kind */
#define PIP_KIND_ROM        0
#define PIP_KIND_HOLOTAPE   1

/* Error codes. A call returns the negative value. */
#define PIP_EPERM       1   /* not allowed for this caller */
#define PIP_ENOENT      2   /* file, save or holotape not found */
#define PIP_EIO         5   /* SD card or device error */
#define PIP_EBADF       9   /* bad file handle */
#define PIP_ENOMEM      12  /* out of memory or handles */
#define PIP_EFAULT      14  /* bad pointer or range */
#define PIP_EBUSY       16  /* resource in use */
#define PIP_EEXIST      17  /* already exists */
#define PIP_EINVAL      22  /* bad argument */
#define PIP_ENOSPC      28  /* card full */
#define PIP_ERANGE      34  /* value out of range */
#define PIP_ENOSYS      38  /* unknown syscall */
#define PIP_ENOEXEC     40  /* holotape failed validation */
#define PIP_ENODEV      41  /* hardware not present */
#define PIP_ETIMEDOUT   42  /* wait timed out */

/*
 * Syscall numbers, in r7. The whole table of the spec.
 * PIP_SYSCALLS(X) lists every call as X(NAME, name) in number order, for
 * code that walks the table: the number is PIP_SYS_##NAME, the wrapper
 * pip_##name. A new call needs a define and a line here.
 */
#define PIP_SYS_DRAW_LINE               0x01
#define PIP_SYS_DRAW_POINT              0x02
#define PIP_SYS_DRAW_TEXT               0x03
#define PIP_SYS_CLEAR                   0x04
#define PIP_SYS_GFX_INFO                0x05
#define PIP_SYS_RECT                    0x06
#define PIP_SYS_POLYLINE                0x07
#define PIP_SYS_ARC                     0x08
#define PIP_SYS_BLIT_1BPP               0x09
#define PIP_SYS_BLIT_RGB                0x0A
#define PIP_SYS_SET_CLIP                0x0B
#define PIP_SYS_SET_COLOR               0x0C
#define PIP_SYS_TEXT_METRICS            0x0D
#define PIP_SYS_PRESENT                 0x0E
#define PIP_SYS_SET_FONT                0x0F
#define PIP_SYS_READ_BUTTONS            0x10
#define PIP_SYS_READ_DIAL               0x11
#define PIP_SYS_POLL_EVENT              0x12
#define PIP_SYS_WAIT_EVENT              0x13
#define PIP_SYS_TONE                    0x20
#define PIP_SYS_SAMPLE                  0x21
#define PIP_SYS_AUDIO_STOP              0x22
#define PIP_SYS_VOLUME                  0x23
#define PIP_SYS_AUDIO_STATUS            0x24
#define PIP_SYS_PLAY_SEQ                0x25
#define PIP_SYS_SAMPLE_QUEUE            0x26  /* ABI 1.1 */
#define PIP_SYS_READ_SENSOR             0x30
#define PIP_SYS_SENSOR_AVAILABLE        0x31
#define PIP_SYS_GET_TIME                0x40
#define PIP_SYS_GET_BATTERY             0x41
#define PIP_SYS_TICKS_MS                0x42
#define PIP_SYS_SLEEP_MS                0x43
#define PIP_SYS_EXIT                    0x44
#define PIP_SYS_SYS_INFO                0x45
#define PIP_SYS_MEM_INFO                0x46
#define PIP_SYS_POWER                   0x47
#define PIP_SYS_LOG                     0x48
#define PIP_SYS_RAND                    0x49
#define PIP_SYS_SET_TIME                0x4A
#define PIP_SYS_READ_SAVE               0x50
#define PIP_SYS_WRITE_SAVE              0x51
#define PIP_SYS_SAVE_INFO               0x52
#define PIP_SYS_SAVE_DELETE             0x53
#define PIP_SYS_FILE_OPEN               0x54
#define PIP_SYS_FILE_READ               0x55
#define PIP_SYS_FILE_CLOSE              0x56
#define PIP_SYS_DIR_LIST                0x57
#define PIP_SYS_HOLO_ENUMERATE          0x60
#define PIP_SYS_HOLO_LAUNCH             0x61
#define PIP_SYS_HOLO_LAST_STATUS        0x62
#define PIP_SYS_HOLO_EJECT_EVENT        0x63

#define PIP_SYSCALLS(X) \
    X(DRAW_LINE, draw_line) \
    X(DRAW_POINT, draw_point) \
    X(DRAW_TEXT, draw_text) \
    X(CLEAR, clear) \
    X(GFX_INFO, gfx_info) \
    X(RECT, rect) \
    X(POLYLINE, polyline) \
    X(ARC, arc) \
    X(BLIT_1BPP, blit_1bpp) \
    X(BLIT_RGB, blit_rgb) \
    X(SET_CLIP, set_clip) \
    X(SET_COLOR, set_color) \
    X(TEXT_METRICS, text_metrics) \
    X(PRESENT, present) \
    X(SET_FONT, set_font) \
    X(READ_BUTTONS, read_buttons) \
    X(READ_DIAL, read_dial) \
    X(POLL_EVENT, poll_event) \
    X(WAIT_EVENT, wait_event) \
    X(TONE, tone) \
    X(SAMPLE, sample) \
    X(AUDIO_STOP, audio_stop) \
    X(VOLUME, volume) \
    X(AUDIO_STATUS, audio_status) \
    X(PLAY_SEQ, play_seq) \
    X(SAMPLE_QUEUE, sample_queue) \
    X(READ_SENSOR, read_sensor) \
    X(SENSOR_AVAILABLE, sensor_available) \
    X(GET_TIME, get_time) \
    X(GET_BATTERY, get_battery) \
    X(TICKS_MS, ticks_ms) \
    X(SLEEP_MS, sleep_ms) \
    X(EXIT, exit) \
    X(SYS_INFO, sys_info) \
    X(MEM_INFO, mem_info) \
    X(POWER, power) \
    X(LOG, log) \
    X(RAND, rand) \
    X(SET_TIME, set_time) \
    X(READ_SAVE, read_save) \
    X(WRITE_SAVE, write_save) \
    X(SAVE_INFO, save_info) \
    X(SAVE_DELETE, save_delete) \
    X(FILE_OPEN, file_open) \
    X(FILE_READ, file_read) \
    X(FILE_CLOSE, file_close) \
    X(DIR_LIST, dir_list) \
    X(HOLO_ENUMERATE, holo_enumerate) \
    X(HOLO_LAUNCH, holo_launch) \
    X(HOLO_LAST_STATUS, holo_last_status) \
    X(HOLO_EJECT_EVENT, holo_eject_event)

/* Number of calls in the table. */
#define PIP_SYSCALL_COUNT           51
/* Every number is below this. */
#define PIP_SYS_NR_LIMIT            0x80

/* Colours are 0x00RRGGBB. As a background, PIP_COLOR_NONE is transparent. */
#define PIP_COLOR_NONE      0x01000000u

/* Display */
#define PIP_SCREEN_W        320
#define PIP_SCREEN_H        240
#define PIP_GFX_FORMAT_XRGB 0           /* pip_gfx_info_t.format: 0x00RRGGBB */
#define PIP_GFX_BACKEND_HDMI 0          /* pip_gfx_info_t.backend */
#define PIP_GFX_BACKEND_SPI  1
#define PIP_WH(w, h)        ((((uint32_t)(w) & 0xffffu) << 16) | ((uint32_t)(h) & 0xffffu))
#define PIP_TEXT_MAX        255         /* draw_text, text_metrics: bytes before the NUL */
#define PIP_SCALE_MIN       1
#define PIP_SCALE_MAX       8
#define PIP_TM_WIDTH(v)     (((uint32_t)(v) >> 16) & 0xffffu)  /* text_metrics result */
#define PIP_TM_HEIGHT(v)    ((uint32_t)(v) & 0xffffu)
#define PIP_POLY_MIN        2           /* polyline count */
#define PIP_POLY_MAX        256
#define PIP_ARC_RADIUS_MIN  1
#define PIP_ARC_RADIUS_MAX  512
#define PIP_ARC_START_MAX   359
#define PIP_ARC_END_MAX     360
#define PIP_ARC_OUTLINE     0           /* pip_arc_t.fill */
#define PIP_ARC_FILLED      1
#define PIP_RECT_OUTLINE    0           /* rect fill */
#define PIP_RECT_FILLED     1
#define PIP_BLIT_W_MAX      320
#define PIP_BLIT_H_MAX      240
#define PIP_FONT_SYSTEM     0           /* 8x16, the only font of ABI 1.0 */
#define PIP_FONT_W          8
#define PIP_FONT_H          16

/* Input */
#define PIP_BTN_UP          0           /* button numbers: bit positions of read_buttons */
#define PIP_BTN_DOWN        1
#define PIP_BTN_LEFT        2
#define PIP_BTN_RIGHT       3
#define PIP_BTN_SELECT      4
#define PIP_BTN_DIAL        5           /* dial press */
#define PIP_BTN_BACK        6           /* ABI 1.2: never seen on a 1.0 or 1.1 Pip-OS */
#define PIP_BTN_COUNT       7
#define PIP_BTN_BIT(n)      (1u << (n))
#define PIP_BTN_UP_BIT      PIP_BTN_BIT(PIP_BTN_UP)
#define PIP_BTN_DOWN_BIT    PIP_BTN_BIT(PIP_BTN_DOWN)
#define PIP_BTN_LEFT_BIT    PIP_BTN_BIT(PIP_BTN_LEFT)
#define PIP_BTN_RIGHT_BIT   PIP_BTN_BIT(PIP_BTN_RIGHT)
#define PIP_BTN_SELECT_BIT  PIP_BTN_BIT(PIP_BTN_SELECT)
#define PIP_BTN_DIAL_BIT    PIP_BTN_BIT(PIP_BTN_DIAL)
#define PIP_BTN_BACK_BIT    PIP_BTN_BIT(PIP_BTN_BACK)
#define PIP_DIAL_MAX        359         /* read_dial: 0 to 359 degrees */
#define PIP_DIAL_DETENT_DEG 15
#define PIP_EV_BUTTON       1           /* pip_event_t.type */
#define PIP_EV_DIAL         2
#define PIP_EV_RELEASE      0           /* PIP_EV_BUTTON value */
#define PIP_EV_PRESS        1
#define PIP_EV_REPEAT       2
#define PIP_EVENT_QUEUE     32
#define PIP_WAIT_FOREVER    0xFFFFFFFFu /* wait_event timeout */
#define PIP_WAIT_MAX_MS     0x7FFFFFFFu /* larger timeouts other than forever: -PIP_ERANGE */

/* Audio */
#define PIP_TONE_FREQ_MIN   20
#define PIP_TONE_FREQ_MAX   20000
#define PIP_TONE_MS_MIN     1
#define PIP_TONE_MS_MAX     60000
#define PIP_NOTE_REST       0           /* pip_note_t.freq_hz */
#define PIP_SAMPLE_RATE     8000
#define PIP_SAMPLE_MAX      65536
#define PIP_SEQ_MIN         1           /* play_seq count */
#define PIP_SEQ_MAX         256
#define PIP_VOLUME_MAX      100

/* Sensors for read_sensor and sensor_available */
#define PIP_SENSOR_RTC      0
#define PIP_SENSOR_BATTERY  1
#define PIP_SENSOR_GEIGER   2
#define PIP_SENSOR_GPS      3
#define PIP_SENSOR_ACCEL    4
#define PIP_SENSOR_TEMP     5
#define PIP_SENSOR_HEART    6
#define PIP_SENSOR_COUNT    7

/* power ops */
#define PIP_POWER_REBOOT    0
#define PIP_POWER_HALT      1
#define PIP_POWER_SCREEN_OFF 2
#define PIP_POWER_SCREEN_ON 3

/* sleep_ms */
#define PIP_SLEEP_MAX_MS    0x7FFFFFFFu

/* Saves */
#define PIP_SAVE_MAX        65536       /* bytes */
#define PIP_SAVE_NAME_MAX   8           /* characters, A-Z 0-9 _ */

/* Files */
#define PIP_PATH_MAX        127         /* bytes before the NUL */
#define PIP_FILES_MAX       8           /* open files per image, handles 0 to 7 */
#define PIP_DIR_LIST_MAX    64          /* dir_list max */
#define PIP_DIRENT_DIR      0x01        /* pip_dirent_t.flags */
#define PIP_NAME83_SIZE     13          /* 8.3 name and NUL */

/* Holotapes */
#define PIP_HOLO_ENUM_MAX   32          /* holo_enumerate max */
#define PIP_HOLO_GAME       0           /* pip_holo_info_t.subtype */
#define PIP_HOLO_UTILITY    1
#define PIP_HOLO_DATA       2
#define PIP_ARGS_MAX        99          /* holo_launch args: bytes before the NUL */
#define PIP_ARGS_SIZE       100         /* pipos_startup_t.args */
#define PIP_EXIT_STATUS_MAX 255

#ifndef __ASSEMBLER__
#include <stddef.h>
#include <stdint.h>

/* Size and field offsets of an ABI struct, checked at build time. */
#define PIP_ABI_SIZE(t, n)      _Static_assert(sizeof(t) == (n), #t " must be " #n " bytes")
#define PIP_ABI_AT(t, f, off)   _Static_assert(offsetof(t, f) == (off), #t "." #f " must be at " #off)

/* Written by Pip-OS at stack_top - PIP_STACK_SIZE; r0 points at it on entry. */
typedef struct __attribute__((packed)) pipos_startup {
    uint32_t abi_version;
    uint32_t size;          /* 128 */
    uint32_t heap_base;
    uint32_t heap_size;
    uint32_t stack_top;
    uint32_t kind;          /* PIP_KIND_* */
    uint32_t flags;         /* 0 */
    char     args[PIP_ARGS_SIZE]; /* NUL-terminated */
} pipos_startup_t;

PIP_ABI_SIZE(pipos_startup_t, 128);
PIP_ABI_AT(pipos_startup_t, abi_version, 0x00);
PIP_ABI_AT(pipos_startup_t, size, 0x04);
PIP_ABI_AT(pipos_startup_t, heap_base, 0x08);
PIP_ABI_AT(pipos_startup_t, heap_size, 0x0C);
PIP_ABI_AT(pipos_startup_t, stack_top, 0x10);
PIP_ABI_AT(pipos_startup_t, kind, 0x14);
PIP_ABI_AT(pipos_startup_t, flags, 0x18);
PIP_ABI_AT(pipos_startup_t, args, 0x1C);

/* gfx_info */
typedef struct __attribute__((packed)) pip_gfx_info {
    uint16_t width;         /* 320 */
    uint16_t height;        /* 240 */
    uint32_t format;        /* PIP_GFX_FORMAT_XRGB */
    uint32_t backend;       /* PIP_GFX_BACKEND_* */
    uint32_t fg_default;    /* colour of the configured screen scheme */
} pip_gfx_info_t;

PIP_ABI_SIZE(pip_gfx_info_t, 16);
PIP_ABI_AT(pip_gfx_info_t, width, 0x00);
PIP_ABI_AT(pip_gfx_info_t, height, 0x02);
PIP_ABI_AT(pip_gfx_info_t, format, 0x04);
PIP_ABI_AT(pip_gfx_info_t, backend, 0x08);
PIP_ABI_AT(pip_gfx_info_t, fg_default, 0x0C);

/* arc. Degrees, clockwise on the screen, 0 along +x. */
typedef struct __attribute__((packed)) pip_arc {
    int16_t  cx, cy;        /* center */
    uint16_t radius;        /* 1 to 512 */
    uint16_t start_deg;     /* 0 to 359 */
    uint16_t end_deg;       /* 0 to 360 */
    uint16_t fill;          /* PIP_ARC_OUTLINE or PIP_ARC_FILLED */
} pip_arc_t;

PIP_ABI_SIZE(pip_arc_t, 12);
PIP_ABI_AT(pip_arc_t, cx, 0x00);
PIP_ABI_AT(pip_arc_t, cy, 0x02);
PIP_ABI_AT(pip_arc_t, radius, 0x04);
PIP_ABI_AT(pip_arc_t, start_deg, 0x06);
PIP_ABI_AT(pip_arc_t, end_deg, 0x08);
PIP_ABI_AT(pip_arc_t, fill, 0x0A);

/* blit_1bpp and blit_rgb */
typedef struct __attribute__((packed)) pip_blit {
    int16_t  x, y;          /* top left corner on the surface */
    uint16_t w, h;          /* 1 to 320, 1 to 240 */
    uint16_t stride;        /* bytes from one row to the next */
    uint16_t flags;         /* 0 */
    uint32_t data;          /* address of the first row */
    uint32_t color;         /* blit_1bpp only */
} pip_blit_t;

PIP_ABI_SIZE(pip_blit_t, 20);
PIP_ABI_AT(pip_blit_t, x, 0x00);
PIP_ABI_AT(pip_blit_t, y, 0x02);
PIP_ABI_AT(pip_blit_t, w, 0x04);
PIP_ABI_AT(pip_blit_t, h, 0x06);
PIP_ABI_AT(pip_blit_t, stride, 0x08);
PIP_ABI_AT(pip_blit_t, flags, 0x0A);
PIP_ABI_AT(pip_blit_t, data, 0x0C);
PIP_ABI_AT(pip_blit_t, color, 0x10);

/* poll_event and wait_event */
typedef struct __attribute__((packed)) pip_event {
    uint32_t type;          /* PIP_EV_BUTTON or PIP_EV_DIAL */
    uint32_t code;          /* button number, or 0 for the dial */
    int32_t  value;         /* PIP_EV_PRESS/RELEASE/REPEAT, or signed detents */
    uint32_t time_ms;       /* ticks_ms when the event happened */
} pip_event_t;

PIP_ABI_SIZE(pip_event_t, 16);
PIP_ABI_AT(pip_event_t, type, 0x00);
PIP_ABI_AT(pip_event_t, code, 0x04);
PIP_ABI_AT(pip_event_t, value, 0x08);
PIP_ABI_AT(pip_event_t, time_ms, 0x0C);

/* play_seq */
typedef struct __attribute__((packed)) pip_note {
    uint16_t freq_hz;       /* 20 to 20000, or PIP_NOTE_REST */
    uint16_t duration_ms;   /* 1 to 60000 */
} pip_note_t;

PIP_ABI_SIZE(pip_note_t, 4);
PIP_ABI_AT(pip_note_t, freq_hz, 0x00);
PIP_ABI_AT(pip_note_t, duration_ms, 0x02);

/* dir_list */
typedef struct __attribute__((packed)) pip_dirent {
    char     name[PIP_NAME83_SIZE]; /* 8.3 name, NUL terminated */
    uint8_t  flags;         /* PIP_DIRENT_DIR */
    uint16_t reserved;      /* 0 */
    uint32_t size;          /* bytes, 0 for a directory */
} pip_dirent_t;

PIP_ABI_SIZE(pip_dirent_t, 20);
PIP_ABI_AT(pip_dirent_t, name, 0x00);
PIP_ABI_AT(pip_dirent_t, flags, 0x0D);
PIP_ABI_AT(pip_dirent_t, reserved, 0x0E);
PIP_ABI_AT(pip_dirent_t, size, 0x10);

/* holo_enumerate */
typedef struct __attribute__((packed)) pip_holo_info {
    char     file[PIP_NAME83_SIZE]; /* 8.3 file name, for holo_launch */
    uint8_t  subtype;       /* PIP_HOLO_GAME, _UTILITY or _DATA */
    uint16_t reserved;      /* 0 */
    uint32_t size;          /* file size in bytes */
    uint32_t image_version;
    char     name[16];      /* from the image header */
    char     title[64];     /* from the META block, or empty */
    uint8_t  icon[128];     /* from the META block, or zero */
} pip_holo_info_t;

PIP_ABI_SIZE(pip_holo_info_t, 232);
PIP_ABI_AT(pip_holo_info_t, file, 0x00);
PIP_ABI_AT(pip_holo_info_t, subtype, 0x0D);
PIP_ABI_AT(pip_holo_info_t, reserved, 0x0E);
PIP_ABI_AT(pip_holo_info_t, size, 0x10);
PIP_ABI_AT(pip_holo_info_t, image_version, 0x14);
PIP_ABI_AT(pip_holo_info_t, name, 0x18);
PIP_ABI_AT(pip_holo_info_t, title, 0x28);
PIP_ABI_AT(pip_holo_info_t, icon, 0x68);

/* sys_info */
typedef struct __attribute__((packed)) pip_sys_info {
    uint32_t size;           /* 32 */
    uint32_t abi_version;    /* PIP_ABI_VERSION */
    uint32_t pipos_version;  /* (major << 16) | (minor << 8) | patch */
    uint32_t board_rev;      /* firmware board revision code */
    uint32_t soc;            /* 0x2835, 0x2836, 0x2837, or 0 if unknown */
    uint32_t mem_size;       /* RAM in bytes */
    /*
     * PIP_HW_* flags, since ABI 1.3. A Pip-OS before 1.3 writes 0 here, so
     * a ROM checks abi_version >= 0x00010003 before it reads this field.
     */
    uint32_t hw_flags;
    uint32_t reserved;       /* 0 */
} pip_sys_info_t;

/* sys_info hw_flags bits. Bit 1 and bits 3 to 31 are 0. */
#define PIP_HW_RTC          (1u << 0)  /* a clock chip answered at boot */
#define PIP_HW_AUDIO        (1u << 2)  /* the audio driver started at boot */

PIP_ABI_SIZE(pip_sys_info_t, 32);
PIP_ABI_AT(pip_sys_info_t, size, 0x00);
PIP_ABI_AT(pip_sys_info_t, abi_version, 0x04);
PIP_ABI_AT(pip_sys_info_t, pipos_version, 0x08);
PIP_ABI_AT(pip_sys_info_t, board_rev, 0x0C);
PIP_ABI_AT(pip_sys_info_t, soc, 0x10);
PIP_ABI_AT(pip_sys_info_t, mem_size, 0x14);
PIP_ABI_AT(pip_sys_info_t, hw_flags, 0x18);
PIP_ABI_AT(pip_sys_info_t, reserved, 0x1C);

/* mem_info */
typedef struct __attribute__((packed)) pip_mem_info {
    uint32_t heap_base;
    uint32_t heap_size;
    uint32_t stack_top;
    uint32_t stack_size;     /* PIP_STACK_SIZE */
} pip_mem_info_t;

PIP_ABI_SIZE(pip_mem_info_t, 16);
PIP_ABI_AT(pip_mem_info_t, heap_base, 0x00);
PIP_ABI_AT(pip_mem_info_t, heap_size, 0x04);
PIP_ABI_AT(pip_mem_info_t, stack_top, 0x08);
PIP_ABI_AT(pip_mem_info_t, stack_size, 0x0C);

#endif /* __ASSEMBLER__ */
#endif
