.section ".text.boot"

// _start is the entrypoint used by the linker script
.global _start

_start:

	// send 3 out of 4 cores to halt (monothread)
	// Read Proc feature Register 0
	mrc p15, #0, r1, c0, c0, #5
	and r1, r1, #3
	cmp r1, #0
	bne halt

	// set the C stack starting at address 0x8000 and downwards
	// the other side is used by the kernel itself
	mov sp, #0x8000

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
	stmia r4!, {r5-r8}

2:
	cmp r4, r9
	blo 1b
	// BSS is zeroed out

	// load the C function kernel_main() into register
	ldr r3, =kernel_main
	// jump to the location of the function (call)
	blx r3

halt:
	wfe
	b halt
