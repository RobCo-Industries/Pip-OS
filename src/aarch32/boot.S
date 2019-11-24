.section ".text.boot"

// _start is the entrypoint used by the linker script
.globl _start

	// On 32bits architecture, the C stack should begin at 0x8000
	.org 0x8000

_start:

#ifndef BCM2835 // BCM2835 has a mono-core CPU
	// send 3 out of 4 cores to halt (mono-core)
	// Read Proc feature Register 0
	mrc p15, #0, r1, c0, c0, #5
	and r1, r1, #3
	cmp r1, #0
	bne halt
#endif

	// set the C stack starting at address .org and downwards
	// the other side is used by the kernel itself
	ldr r5, =_start
    mov sp, r5

	// put start and end of C BSS memory section into registers
	// __bss_start & __bss_end are symbols defined by the linker
	ldr r4, =__bss_start
	ldr r9, =__bss_end

	// begin to zero out the BSS section
	mov r5, #0
	mov r6, #0
	mov r7, #0
	mov r8, #0
	b       2f

1:
    // store multiple at r4 and compare with end of BSS 
    // if current address still below, continue looping
	stmia r4!, {r5-r8}

2:
	cmp r4, r9
	blo 1b
	// BSS is zeroed out

	// load the C function kernel_main() into register
	ldr r3, =kernel_main
	// jump to the location of the function (call)
	blx r3

// halt is an infinite loop used by others cores
halt:
	wfe
	b halt
