/*
 * Pip-OS holotape link script: the holotape slot, 0x00800000 to 0x00BFFFFF.
 * crt0.S provides the head, the rimg header and the META block. The
 * stack and heap live in the holotape heap region, not here, so mem_size
 * covers the file and BSS.
 */
ENTRY(_start)

SECTIONS
{
    . = 0x00800000;
    __image_start = .;

    .head : { KEEP(*(.pip.head)) }
    .rimg 0x00800020 : { KEEP(*(.pip.rimg)) }
    .meta 0x00800060 : { KEEP(*(.pip.meta)) }

    .text 0x00800120 : {
        KEEP(*(.pip.crt0))
        *(.text .text.*)
    }

    .rodata : ALIGN(4) {
        *(.rodata .rodata.*)
    }

    .ARM.exidx : ALIGN(4) {
        *(.ARM.exidx*)
    }

    .data : ALIGN(4) {
        *(.data .data.*)
        . = ALIGN(4);
    }
    __image_end = .;

    .bss (NOLOAD) : ALIGN(4) {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    }
    __image_mem_end = .;

    __image_file_size = __image_end - __image_start;
    __image_mem_size = __image_mem_end - __image_start;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.ARM.attributes)
    }
}

ASSERT(SIZEOF(.head) == 0x20, "branch area must be 0x20 bytes")
ASSERT(SIZEOF(.rimg) == 64, "rimg header must be 64 bytes")
ASSERT(SIZEOF(.meta) == 192, "META block must be 192 bytes")
ASSERT(__image_mem_end <= 0x00C00000, "the holotape does not fit the 4 MiB holotape slot")
