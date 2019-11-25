ENTRY(_start)
 
SECTIONS
{
    /* Starts at LOADER_ADDR. */
    . = DEFINED(__aarch64__) ? 0x80000 : 0x8000;
    __start = .;

    .init : {
		*(.init)
	}

    __text_start = .;
    .text :
    {
        *(.text)
    }
    . = ALIGN(4096); /* align to page size */
    __text_end = .;
 
    __rodata_start = .;
    .rodata :
    {
        *(.rodata)
    }
    . = ALIGN(4096); /* align to page size */
    __rodata_end = .;

    __init_start = .;
    .init_array : 
    {
        *(.init_array*)
    }
    . = ALIGN(4096); /* align to page size */
    __init_end = .;

    __exidx_start = .;
    .ARM.exidx :
    {
        *(.ARM.exidx*)
    }
    . = ALIGN(4096); /* align to page size */
    __exidx_end = .;
 
    __data_start = .;
    .data :
    {
        *(.data)
    }
    . = ALIGN(4096); /* align to page size */
    __data_end = .;
 
    /* C Stack */

    __bss_start = .;
    .bss :
    {
        *(.bss*)
        *(COMMON)
    }
    . = ALIGN(4096); /* align to page size */
    __bss_end = .;
    __bss_size = __bss_end - __bss_start;
    
    
    __end = .;
}