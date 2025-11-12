.section ".text.boot"

// _start is the entrypoint used by the linker script
.globl _start

_start:
#ifndef BCM2835 // BCM2835 has a mono-core CPU
	// send 3 out of 4 cores to halt
	// Read Multiprocessor Affinity Register
	mrc p15, #0, r1, c0, c0, #5
	and r1, r1, #3
	cmp r1, #0
	bne halt
#endif

	// Set stack pointer to beginning of code (grows downward)
	// This is the standard approach - sp points to 0x8000, grows down
	ldr sp, =_start

	// Zero out BSS section
	ldr r4, =__bss_start
	ldr r9, =__bss_end
	mov r5, #0
	mov r6, #0
	mov r7, #0
	mov r8, #0
	b 2f

1:
	// Store multiple zeros at r4 and increment
	stmia r4!, {r5-r8}

2:
	cmp r4, r9
	blo 1b

	// Set up parameters for kernel_main
	// r0, r1, r2 contain boot parameters (ATAGS)
	mov r0, #0
	mov r1, #0
	mov r2, #0x100

	// Call kernel_main
	bl kernel_main

// Infinite loop for halt
halt:
#ifndef BCM2835
	wfe
#else
	nop
#endif
	b halt
