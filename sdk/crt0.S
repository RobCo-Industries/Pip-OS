/*
 * Start-up code for a Pip-OS ROM or holotape, linked first by rom.ld.
 * Emits the rimg layout of IMAGE_FORMAT.md: a branch at 0x00, the header
 * at 0x20 with the META flag, a zeroed META block at 0x60, then code.
 * `rimg.py seal` writes the name, version, title, icon and CRC.
 *
 * Override with -D: PIP_IMG_MAGIC ("ROM1" or "ROBCO78"), PIP_IMG_SUBTYPE,
 * PIPOS_ABI_TARGET (the abi_version, see pipos/abi.h).
 */
#include <pipos/abi.h>

#ifndef PIP_IMG_MAGIC
#define PIP_IMG_MAGIC   "ROM1"
#endif
#ifndef PIP_IMG_SUBTYPE
#define PIP_IMG_SUBTYPE 0
#endif

#define RIMG_HDR_VERSION    1
#define RIMG_HDR_SIZE       64
#define RIMG_META_SIZE      192
#define RIMG_FLAG_META      1

/* 0x00: branch to the entry, padded to 0x20. */
    .section .pip.head, "ax"
    .arm
    .global _start
_start:
    b       pip_crt0
    .balign 32, 0

/* 0x20: header. */
    .section .pip.rimg, "a"
pip_rimg:
    .ascii  PIP_IMG_MAGIC
    .org    pip_rimg + 8, 0
    .hword  RIMG_HDR_VERSION
    .hword  RIMG_HDR_SIZE
    .word   RIMG_FLAG_META
    .word   __image_start                   @ load_addr
    .word   __image_file_size
    .word   __image_mem_size
    .word   0                               @ crc32, written by rimg.py seal
    .word   PIPOS_ABI_TARGET                @ abi_version, PIP_ABI_VERSION when not set
    .word   0                               @ image_version, rimg.py seal --image-version
    .word   PIP_IMG_SUBTYPE
    .space  16, 0                           @ name, rimg.py seal --name
    .word   0                               @ reserved
    .org    pip_rimg + RIMG_HDR_SIZE

/* 0x60: META block, title and icon, written by rimg.py seal. */
    .section .pip.meta, "a"
    .space  RIMG_META_SIZE, 0

    .section .pip.crt0, "ax"
    .arm
pip_crt0:
    /* Keep the entry registers for the image: r0-r3, sp, lr. */
    ldr     r12, =pip_entry_regs
    stmia   r12, {r0-r3, sp, lr}
    /* Clear BSS. rom.ld aligns both ends to 4 bytes. */
    ldr     r4, =__bss_start
    ldr     r5, =__bss_end
    mov     r6, #0
1:  cmp     r4, r5
    strlo   r6, [r4], #4
    blo     1b
    /* r0 still holds the startup block. */
    bl      pip_main
    mov     r7, #PIP_SYS_EXIT
    svc     #0
2:  b       2b

    .section .data.pip_entry_regs, "aw"
    .balign 4
    .global pip_entry_regs
pip_entry_regs:
    .space  24, 0
