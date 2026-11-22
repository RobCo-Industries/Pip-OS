/*
 * uint32_t regs_check(void): loads known values into r4-r11 and lr,
 * makes a system call (ticks_ms, r7 = 0x42) and returns a bit mask of
 * the registers that changed: bit n for rn, bit 13 for sp, bit 14 for lr.
 * 0 means every callee-saved register survived.
 */
    .text
    .arm
    .global regs_check
regs_check:
    push    {r4-r11, lr}
    ldr     r0, =saved_sp
    str     sp, [r0]
    ldr     r4, =0x44440004
    ldr     r5, =0x55550005
    ldr     r6, =0x66660006
    mov     r7, #0x42
    ldr     r8, =0x88880008
    ldr     r9, =0x99990009
    ldr     r10, =0xaaaa000a
    ldr     r11, =0xbbbb000b
    ldr     lr, =0xeeee000e
    svc     #0
    mov     r0, #0
    ldr     r12, =0x44440004
    cmp     r4, r12
    orrne   r0, r0, #(1 << 4)
    ldr     r12, =0x55550005
    cmp     r5, r12
    orrne   r0, r0, #(1 << 5)
    ldr     r12, =0x66660006
    cmp     r6, r12
    orrne   r0, r0, #(1 << 6)
    cmp     r7, #0x42
    orrne   r0, r0, #(1 << 7)
    ldr     r12, =0x88880008
    cmp     r8, r12
    orrne   r0, r0, #(1 << 8)
    ldr     r12, =0x99990009
    cmp     r9, r12
    orrne   r0, r0, #(1 << 9)
    ldr     r12, =0xaaaa000a
    cmp     r10, r12
    orrne   r0, r0, #(1 << 10)
    ldr     r12, =0xbbbb000b
    cmp     r11, r12
    orrne   r0, r0, #(1 << 11)
    ldr     r12, =saved_sp
    ldr     r12, [r12]
    cmp     sp, r12
    orrne   r0, r0, #(1 << 13)
    ldr     r12, =0xeeee000e
    cmp     lr, r12
    orrne   r0, r0, #(1 << 14)
    pop     {r4-r11, pc}
    .ltorg

    .bss
    .balign 4
saved_sp:
    .space  4
