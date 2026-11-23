/*
 * The conformance ROM: shared declarations.
 *
 * Every check records one result and prints one serial line:
 *   @RC stage=conform ev=check n=<n> call=<call> name=<name> result=<R>
 *       [got=<n> expected=<n>]
 * R is PASS, FAIL, SKIP or ABSENT.
 */
#ifndef CONFORM_H
#define CONFORM_H

#include <stdint.h>

#include <pipos/pip.h>

enum { R_PASS, R_FAIL, R_SKIP, R_ABSENT };

/* Addresses no image may pass to a call. */
#define BAD_KERNEL   0x00008000u                 /* kernel code */
#define BAD_STRADDLE (PIP_ROM_STACK_TOP - 4u)    /* 4 bytes in, the rest past the region */

/* The holotapes the card carries. */
#define TAPE_FILE    "CONFTAPE.HTP"
#define DATA_FILE    "CONFDATA.HTP"
#define TAPE_PATH    "/PIPBOY/HOLO/" TAPE_FILE
#define TAPE_OK      42      /* the tape's exit status when all its checks pass */
#define SAVE_NAME    "CONFTEST"

/* 1 when nobody answers prompts: interactive checks are SKIP. */
extern int auto_mode;
/* The call the current checks belong to, for the serial line. */
extern const char *cur_call;
/* holo_last_status() before the ROM's first holo_launch. */
extern int32_t first_last_status;

/* A check that passes when ok is non-zero. */
void ck(const char *name, int ok);
/* A check that got == expected. */
void ck_eq(const char *name, int32_t got, int32_t expected);
/* A check with an explicit result. has_values adds got and expected. */
void ck_res(const char *name, int res, int has_values, int32_t got, int32_t expected);

/* Elapsed milliseconds from a to b, across the ticks_ms wrap. */
static inline int32_t elapsed_ms(int32_t a, int32_t b)
{
    return (int32_t)((uint32_t)(b - a) & 0x7FFFFFFFu);
}

/* Interactive part, in interact.c. */
void run_interactive(void);
/* Empties the event queue. */
void drain_events(void);
/* Waits up to ms for a button press. Returns the button number, or -1. */
int wait_press(uint32_t ms);
/* Draws the scene of the "is the picture right" prompt. */
void draw_scene(void);

/*
 * One check function per system call, named check_<call>. The runner's
 * table is built from PIP_SYSCALLS in abi.h, so a call added there and
 * not here fails the build.
 */
#define CONFORM_DECLARE(NAME, name) void check_##name(void);
PIP_SYSCALLS(CONFORM_DECLARE)
#undef CONFORM_DECLARE

/* Checks that belong to no single call. */
void check_unknown(void);

#endif
