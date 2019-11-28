ENTRY(_start)

__org = DEFINED(__aarch64__) ? 0x80000 : 0x8000;

MEMORY
{
    RAM (xrw) : ORIGIN = __org , LENGTH = 0x8000000 /* 128MB */
}

SECTIONS
{
    /* 
     * First section stores the assembler code
     * with the kernel entry point.
     */
    .text : 
    { 
        . = ALIGN(4);
        _start = .;
        KEEP(*(.text.boot)) 
        *(.text .text.* .gnu.linkonce.t*) 
        . = ALIGN(4);
    } > RAM

    /* Section for uninitialized static and global variables */
    .bss (NOLOAD) : 
    {
        . = ALIGN(4);
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > RAM

    /* Read-only data section for constants */
    .rodata : 
    {
        . = ALIGN(4);
        *(.rodata .rodata.* .gnu.linkonce.r*)
        PROVIDE(_data = .);
        . = ALIGN(4);
    } > RAM
    
    .data : 
    { 
        . = ALIGN(4);
        *(.data .data.* .gnu.linkonce.d*) 
        . = ALIGN(4);
    } > RAM

    /* Special section for Mailbox exchanges */
    .data1 : 
    { 
        . = ALIGN(16);
        *(.data1 .data1.* .gnu.linkonce.d*) 
        . = ALIGN(16);
    } > RAM

    /* Section for the first core's stack */
    .stack0 :
    {
        . = ALIGN(16);    /* 8 byte boundary alignment (AAPCS64 standard) */
        __stack0_start = .;
        . = . + 512;      /* EL0 stack size */
        __EL0_stack0 = .;
        . = . + 32768;    /* EL1 stack size */
        __EL1_stack0 = .;
        . = . + 16384;    /* EL2 stack size (start-up) */
        __EL2_stack0 = .;
        . = ALIGN(16);
        __stack0_end = .;  
    } > RAM

    /* Section for the second core's stack */
    .stack1 :
    {
        . = ALIGN(16);    /* 8 byte boundary alignment (AAPCS64 standard) */
        __stack1_start = .;
        . = . + 512;      /* EL0 stack size */
        __EL0_stack1 = .;
        . = . + 512;      /* EL1 stack size */
        __EL1_stack1 = .;
        . = . + 4096;     /* EL2 stack size (start-up) */
        __EL2_stack1 = .;
        . = ALIGN(16);
        __stack1_end = .;  
    } > RAM

    /* Section for the third core's stack */
    .stack2 :
    {
        . = ALIGN(16);    /* 8 byte boundary alignment (AAPCS64 standard) */
        __stack2_start = .;
        . = . + 512;      /* EL0 stack size */
        __EL0_stack2 = .;
        . = . + 512;      /* EL1 stack size */
        __EL1_stack2 = .;
        . = . + 4096;     /* EL2 stack size (start-up) */
        __EL2_stack2 = .;
        . = ALIGN(16);
        __stack2_end = .;  
    } > RAM

    /* Section for the fourth core's stack */
    .stack3 :
    {
        . = ALIGN(16);    /* 8 byte boundary alignment (AAPCS64 standard) */
        __stack3_start = .;
        . = . + 512;      /* EL0 stack size */
        __EL0_stack3 = .;
        . = . + 512;      /* EL1 stack size */
        __EL1_stack3 = .;
        . = . + 4096;     /* EL2 stack size (start-up) */
        __EL2_stack3 = .;
        . = ALIGN(16);
        __stack3_end = .;  
        
    } > RAM

    .heap : 
    {
        . = ALIGN(4);
        _end = .;
        PROVIDE(end = .);
    } > RAM

    /DISCARD/ : 
    { 
        *(.ARM.exidx*)
        *(.comment) 
        *(.gnu*) 
        *(.note*) 
        *(.eh_frame*)
        *(.debug_frame*)
    }
}

__bss_size = (__bss_end - __bss_start)>>3;