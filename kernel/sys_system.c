/*
 * System group: ticks_ms, sleep_ms, exit, sys_info, mem_info, log and
 * rand. power (0x47) belongs to the time group. See docs/API.md.
 */
#include <robco/board.h>
#include <robco/rng.h>
#include <robco/rtc.h>
#include <robco/tick.h>
#include <robco/timer.h>
#include <robco/uart.h>
#include <robco/umode.h>

#include "holo.h"
#include "pipos.h"
#include "sys.h"
#include "sysabi.h"

static int32_t sys_ticks_ms(const pipos_sys_ctx_t *ctx, uint32_t a0, uint32_t a1, uint32_t a2,
                            uint32_t a3)
{
    (void)ctx, (void)a0, (void)a1, (void)a2, (void)a3;
    return (int32_t)(tick_ms() & 0x7fffffffu);
}

/*
 * Runs the kernel's main-loop work while it waits, so script commands still
 * work. ms above 0x7FFFFFFF returns -PIP_ERANGE at once. The deadline is on
 * the 1 MHz system timer, so the wait is never short; the tick interrupt
 * wakes the wfi, so it can run up to one tick long.
 */
static int32_t sys_sleep_ms(const pipos_sys_ctx_t *ctx, uint32_t ms, uint32_t a1, uint32_t a2,
                            uint32_t a3)
{
    uint64_t deadline = pipos_sleep_deadline_us(timer_now_us(), ms);
    int32_t rc = pipos_sleep_check(ms);

    (void)ctx, (void)a1, (void)a2, (void)a3;
    if (rc != 0) {
        return rc;
    }
    while (!timer_expired(deadline)) {
        pipos_poll();
        __asm__ volatile("wfi");
    }
    return 0;
}

static int32_t sys_exit(const pipos_sys_ctx_t *ctx, uint32_t status, uint32_t a1, uint32_t a2,
                        uint32_t a3)
{
    (void)a1, (void)a2, (void)a3;
    umode_exit(ctx->frame, (int)status);
    return (int32_t)status;
}

static int32_t sys_sys_info(const pipos_sys_ctx_t *ctx, uint32_t out, uint32_t a1, uint32_t a2,
                            uint32_t a3)
{
    const robco_board_t *b = board_get();
    pip_sys_info_t info;

    (void)a1, (void)a2, (void)a3;
    pipos_sys_info_fill(&info,
                        pipos_version_pack(PIPOS_VERSION_MAJOR, PIPOS_VERSION_MINOR,
                                           PIPOS_VERSION_PATCH),
                        b->board_rev, (uint32_t)b->soc, b->mem_size,
                        pipos_hw_flags(rtc_present(), pipos_audio_driver_ok(),
                                       pipos_slot_enabled()));
    return pipos_user_copy_out(ctx, out, &info, sizeof(info));
}

static int32_t sys_mem_info(const pipos_sys_ctx_t *ctx, uint32_t out, uint32_t a1, uint32_t a2,
                            uint32_t a3)
{
    pip_mem_info_t info;

    (void)a1, (void)a2, (void)a3;
    if (ctx->kind == PIP_KIND_HOLOTAPE) {
        pipos_mem_info_holo(&info);
    } else {
        pipos_mem_info_rom(&info);
    }
    return pipos_user_copy_out(ctx, out, &info, sizeof(info));
}

static int32_t sys_log(const pipos_sys_ctx_t *ctx, uint32_t text, uint32_t a1, uint32_t a2,
                       uint32_t a3)
{
    const char *s = (const char *)(uintptr_t)text;
    long len = pipos_user_strlen(ctx, text, PIP_LOG_MAX);
    long i;

    (void)a1, (void)a2, (void)a3;
    if (len < 0) {
        return (int32_t)len;
    }
    for (i = 0; i < len; i++) {
        uart_putc(pipos_log_char(s[i]));
    }
    return 0;
}

static int32_t sys_rand(const pipos_sys_ctx_t *ctx, uint32_t a0, uint32_t a1, uint32_t a2,
                        uint32_t a3)
{
    uint32_t v;

    (void)ctx, (void)a0, (void)a1, (void)a2, (void)a3;
    if (rng_u32(&v) != 0) {
        return -PIP_EIO;
    }
    return (int32_t)(v & 0x7fffffffu);
}

static const pipos_sys_entry_t system_calls[] = {
    PIPOS_SYS_ENTRY(SYSTEM, TICKS_MS, sys_ticks_ms),
    PIPOS_SYS_ENTRY(SYSTEM, SLEEP_MS, sys_sleep_ms),
    PIPOS_SYS_ENTRY(SYSTEM, EXIT, sys_exit),
    PIPOS_SYS_ENTRY(SYSTEM, SYS_INFO, sys_sys_info),
    PIPOS_SYS_ENTRY(SYSTEM, MEM_INFO, sys_mem_info),
    PIPOS_SYS_ENTRY(SYSTEM, LOG, sys_log),
    PIPOS_SYS_ENTRY(SYSTEM, RAND, sys_rand),
};

const pipos_sys_table_t pipos_system_table = PIPOS_SYS_TABLE(SYSTEM, "system", system_calls);

void pipos_system_init(void)
{
    rng_init(); /* on failure rand returns -PIP_EIO */
}

void pipos_system_image_start(int kind)
{
    (void)kind;
}

void pipos_system_image_end(int kind)
{
    (void)kind;
}

void pipos_system_poll(void)
{
}
