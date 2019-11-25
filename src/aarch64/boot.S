    .sect _init

    // _start is the entrypoint used by the linker script
    .globl _start

        // On 64bits architecture, the C stack should begin at 0x80000
	    .org 0x80000

_start:

    // stop slave cores
    mrs     x1, mpidr_el1
    and     x1, x1, #3
    cbz     x1, 2f

1:  // CPU ID > 0
    wfe 
    b 1b

2:  // CPU ID == 0
    // set the C stack starting at address .org and downwards
	// the other side is used by the kernel itself
	ldr     x1, =_start
    mov     sp, x1
    
    // begin to zero out the BSS section
    ldr     x1, =__bss_start
    ldr     w2, =__bss_size

3:  // loop through all sector to zero out
    cbz     w2, 4f
    str     xzr, [x1], #8
    sub     w2, w2, #1
    cbnz    w2, 3b

4:  // jump to C code, should not return
    bl      kernel_main
    b 1b
