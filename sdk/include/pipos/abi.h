/*
 * Pip-OS ABI 1.0: syscall numbers, error codes, the startup block and
 * the structs system calls fill. Shared by the kernel and by ROM and
 * holotape code. The contract is RobCo-Core's docs/spec/PIPOS_ABI.md.
 *
 * Builds freestanding for ARM and on the host. Assembly files may
 * include it: they see the numbers only.
 */
#ifndef PIPOS_ABI_H
#define PIPOS_ABI_H

#define PIP_ABI_VERSION     0x00010000  /* (major << 16) | minor */

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

/* Syscall numbers, in r7. The whole table of the spec. */
#define PIP_SYS_DRAW_LINE           0x01
#define PIP_SYS_DRAW_POINT          0x02
#define PIP_SYS_DRAW_TEXT           0x03
#define PIP_SYS_CLEAR               0x04
#define PIP_SYS_GFX_INFO            0x05
#define PIP_SYS_RECT                0x06
#define PIP_SYS_POLYLINE            0x07
#define PIP_SYS_ARC                 0x08
#define PIP_SYS_BLIT_1BPP           0x09
#define PIP_SYS_BLIT_RGB            0x0A
#define PIP_SYS_SET_CLIP            0x0B
#define PIP_SYS_SET_COLOR           0x0C
#define PIP_SYS_TEXT_METRICS        0x0D
#define PIP_SYS_PRESENT             0x0E
#define PIP_SYS_SET_FONT            0x0F
#define PIP_SYS_READ_BUTTONS        0x10
#define PIP_SYS_READ_DIAL           0x11
#define PIP_SYS_POLL_EVENT          0x12
#define PIP_SYS_WAIT_EVENT          0x13
#define PIP_SYS_TONE                0x20
#define PIP_SYS_SAMPLE              0x21
#define PIP_SYS_AUDIO_STOP          0x22
#define PIP_SYS_VOLUME              0x23
#define PIP_SYS_AUDIO_STATUS        0x24
#define PIP_SYS_PLAY_SEQ            0x25
#define PIP_SYS_READ_SENSOR         0x30
#define PIP_SYS_SENSOR_AVAILABLE    0x31
#define PIP_SYS_GET_TIME            0x40
#define PIP_SYS_GET_BATTERY         0x41
#define PIP_SYS_TICKS_MS            0x42
#define PIP_SYS_SLEEP_MS            0x43
#define PIP_SYS_EXIT                0x44
#define PIP_SYS_SYS_INFO            0x45
#define PIP_SYS_MEM_INFO            0x46
#define PIP_SYS_POWER               0x47
#define PIP_SYS_LOG                 0x48
#define PIP_SYS_RAND                0x49
#define PIP_SYS_SET_TIME            0x4A
#define PIP_SYS_READ_SAVE           0x50
#define PIP_SYS_WRITE_SAVE          0x51
#define PIP_SYS_SAVE_INFO           0x52
#define PIP_SYS_SAVE_DELETE         0x53
#define PIP_SYS_FILE_OPEN           0x54
#define PIP_SYS_FILE_READ           0x55
#define PIP_SYS_FILE_CLOSE          0x56
#define PIP_SYS_DIR_LIST            0x57
#define PIP_SYS_HOLO_ENUMERATE      0x60
#define PIP_SYS_HOLO_LAUNCH         0x61
#define PIP_SYS_HOLO_LAST_STATUS    0x62
#define PIP_SYS_HOLO_EJECT_EVENT    0x63

/* log: at most this many bytes, NUL included. */
#define PIP_LOG_MAX         256

#ifndef __ASSEMBLER__
#include <stdint.h>

/* Written by Pip-OS at stack_top - PIP_STACK_SIZE; r0 points at it on entry. */
typedef struct __attribute__((packed)) pipos_startup {
    uint32_t abi_version;
    uint32_t size;          /* 128 */
    uint32_t heap_base;
    uint32_t heap_size;
    uint32_t stack_top;
    uint32_t kind;          /* PIP_KIND_* */
    uint32_t flags;         /* 0 */
    char     args[100];     /* NUL-terminated */
} pipos_startup_t;

/* sys_info */
typedef struct __attribute__((packed)) pip_sys_info {
    uint32_t size;           /* 32 */
    uint32_t abi_version;    /* PIP_ABI_VERSION */
    uint32_t pipos_version;  /* (major << 16) | (minor << 8) | patch */
    uint32_t board_rev;      /* firmware board revision code */
    uint32_t soc;            /* 0x2835, 0x2836, 0x2837, or 0 if unknown */
    uint32_t mem_size;       /* RAM in bytes */
    uint32_t reserved[2];    /* 0 */
} pip_sys_info_t;

/* mem_info */
typedef struct __attribute__((packed)) pip_mem_info {
    uint32_t heap_base;
    uint32_t heap_size;
    uint32_t stack_top;
    uint32_t stack_size;
} pip_mem_info_t;

_Static_assert(sizeof(pipos_startup_t) == 128, "pipos_startup_t must be 128 bytes");
_Static_assert(sizeof(pip_sys_info_t) == 32, "pip_sys_info_t must be 32 bytes");
_Static_assert(sizeof(pip_mem_info_t) == 16, "pip_mem_info_t must be 16 bytes");

#endif /* __ASSEMBLER__ */
#endif
