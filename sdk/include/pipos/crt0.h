/* What sdk/crt0.S provides to the image. */
#ifndef PIPOS_CRT0_H
#define PIPOS_CRT0_H

#include <stdint.h>

#include <pipos/abi.h>

/* The image's entry point. crt0 calls exit() with its return value. */
int pip_main(const pipos_startup_t *startup);

/* Registers at entry, saved by crt0 before it clears BSS: r0, r1, r2, r3, sp, lr. */
#define PIP_ENTRY_R0    0
#define PIP_ENTRY_SP    4
#define PIP_ENTRY_LR    5
extern uint32_t pip_entry_regs[6];

#endif
