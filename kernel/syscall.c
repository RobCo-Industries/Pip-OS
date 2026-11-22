/*
 * System calls of a running ROM: number in r7, arguments in r0 to r3,
 * result in r0. Every pointer is checked against the task's regions
 * before use. See docs/API.md.
 */
#include <robco/board.h>
#include <robco/klibc.h>
#include <robco/rng.h>
#include <robco/tick.h>
#include <robco/timer.h>
#include <robco/uart.h>
#include <robco/umode.h>

#include "pipos.h"
#include "sysabi.h"

typedef int32_t (*sys_fn)(exc_frame_t *f);

static int32_t sys_ticks_ms(exc_frame_t *f)
{
    (void)f;
    return (int32_t)(tick_ms() & 0x7fffffffu);
}

/*
 * Runs the kernel's main-loop work while it waits, so script commands still
 * work. ms above 0x7FFFFFFF returns -PIP_ERANGE at once. The deadline is on
 * the 1 MHz system timer, so the wait is never short; the tick interrupt
 * wakes the wfi, so it can run up to one tick long.
 */
static int32_t sys_sleep_ms(exc_frame_t *f)
{
    uint32_t ms = f->r[0];
    uint64_t deadline = pipos_sleep_deadline_us(timer_now_us(), ms);
    int32_t rc = pipos_sleep_check(ms);

    if (rc != 0) {
        return rc;
    }
    while (!timer_expired(deadline)) {
        pipos_poll();
        __asm__ volatile("wfi");
    }
    return 0;
}

static int32_t sys_exit(exc_frame_t *f)
{
    umode_exit(f, (int)f->r[0]);
    return (int32_t)f->r[0];
}

static int32_t sys_sys_info(exc_frame_t *f)
{
    const robco_board_t *b = board_get();
    pip_sys_info_t info;

    if (!umode_range_ok(f->r[0], sizeof(info), 1)) {
        return -PIP_EFAULT;
    }
    pipos_sys_info_fill(&info,
                        pipos_version_pack(PIPOS_VERSION_MAJOR, PIPOS_VERSION_MINOR,
                                           PIPOS_VERSION_PATCH),
                        b->board_rev, (uint32_t)b->soc, b->mem_size);
    k_memcpy((void *)(uintptr_t)f->r[0], &info, sizeof(info));
    return 0;
}

static int32_t sys_mem_info(exc_frame_t *f)
{
    pip_mem_info_t info;

    if (!umode_range_ok(f->r[0], sizeof(info), 1)) {
        return -PIP_EFAULT;
    }
    pipos_mem_info_rom(&info);
    k_memcpy((void *)(uintptr_t)f->r[0], &info, sizeof(info));
    return 0;
}

static int32_t sys_log(exc_frame_t *f)
{
    const char *text = (const char *)(uintptr_t)f->r[0];
    long len = pipos_log_check(umode_strnlen_ok(f->r[0], PIP_LOG_MAX),
                               umode_range_ok(f->r[0], PIP_LOG_MAX, 0));
    long i;

    if (len < 0) {
        return (int32_t)len;
    }
    for (i = 0; i < len; i++) {
        uart_putc(pipos_log_char(text[i]));
    }
    return 0;
}

static int32_t sys_rand(exc_frame_t *f)
{
    uint32_t v;

    (void)f;
    if (rng_u32(&v) != 0) {
        return -PIP_EIO;
    }
    return (int32_t)(v & 0x7fffffffu);
}

/* Indexed by pipos_sys_slot(). */
static const sys_fn handlers[PIPOS_SYS_SLOTS] = {
    [PIP_SYS_TICKS_MS - PIPOS_SYS_FIRST] = sys_ticks_ms,
    [PIP_SYS_SLEEP_MS - PIPOS_SYS_FIRST] = sys_sleep_ms,
    [PIP_SYS_EXIT - PIPOS_SYS_FIRST] = sys_exit,
    [PIP_SYS_SYS_INFO - PIPOS_SYS_FIRST] = sys_sys_info,
    [PIP_SYS_MEM_INFO - PIPOS_SYS_FIRST] = sys_mem_info,
    [PIP_SYS_LOG - PIPOS_SYS_FIRST] = sys_log,
    [PIP_SYS_RAND - PIPOS_SYS_FIRST] = sys_rand,
};

static void on_svc(exc_frame_t *f, void *ctx)
{
    int slot = pipos_sys_slot(f->r[7]);
    int32_t rc = -PIP_ENOSYS;

    (void)ctx;
    if (slot >= 0 && handlers[slot] != NULL) {
        rc = handlers[slot](f);
    }
    f->r[0] = (uint32_t)rc;
}

void pipos_syscall_install(void)
{
    rng_init(); /* on failure rand returns -PIP_EIO */
    umode_set_svc_handler(on_svc, NULL);
}
