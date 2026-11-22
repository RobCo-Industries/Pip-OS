/*
 * One fault per image, picked with -DFAULT_<name> at build time. The ROM
 * logs the address of the instruction that must fault, then runs it.
 *
 *   wrkern    store to kernel memory        data abort
 *   rdperiph  load from a peripheral        data abort
 *   jmpkern   branch into the kernel        prefetch abort at the target
 *   undef     undefined instruction         undef
 *   priv      read SCTLR from user mode     undef
 *   null      load from address 0           data abort
 */
#include "rt.h"

#define KERNEL_ADDR     0x00008000u

extern const char fault_insn[];

static __attribute__((unused)) uint32_t periph_reg(void)
{
    pip_sys_info_t si;

    if (pip_sys_info(&si) != 0) {
        pip_exit(100);
    }
    /* The system timer's counter, CLO. */
    return (si.soc == 0x2835 ? 0x20000000u : 0x3F000000u) + 0x3004u;
}

static void say(uint32_t pc, uint32_t addr)
{
    rt_begin("fault");
    rt_hex("pc", pc);
    rt_hex("addr", addr);
    rt_end();
}

int pip_main(const pipos_startup_t *startup)
{
    (void)startup;
#if defined(FAULT_wrkern) || defined(FAULT_rdperiph) || defined(FAULT_null)
    {
#if defined(FAULT_wrkern)
        uint32_t addr = KERNEL_ADDR;
#elif defined(FAULT_rdperiph)
        uint32_t addr = periph_reg();
#else
        uint32_t addr = 0;
#endif
        say((uint32_t)(uintptr_t)fault_insn, addr);
        {
            /* Bound to r0 here, after the calls, so the value is really in r0. */
            register uint32_t a __asm__("r0") = addr;
#if defined(FAULT_wrkern)
            __asm__ volatile(".global fault_insn\nfault_insn: str r0, [r0]" : : "r"(a) : "memory");
#else
            __asm__ volatile(".global fault_insn\nfault_insn: ldr r1, [r0]" : : "r"(a) : "r1", "memory");
#endif
        }
    }
#elif defined(FAULT_jmpkern)
    say(KERNEL_ADDR, KERNEL_ADDR);
    ((void (*)(void))(uintptr_t)KERNEL_ADDR)();
    __asm__ volatile(".global fault_insn\nfault_insn: nop");
#elif defined(FAULT_undef)
    say((uint32_t)(uintptr_t)fault_insn, 0);
    __asm__ volatile(".global fault_insn\nfault_insn: .word 0xe7f000f0");
#elif defined(FAULT_priv)
    say((uint32_t)(uintptr_t)fault_insn, 0);
    __asm__ volatile(".global fault_insn\nfault_insn: mrc p15, 0, r1, c1, c0, 0" : : : "r1");
#else
#error "build with -DFAULT_<name>"
#endif
    pip_log("the fault did not happen\n");
    return 1;
}
