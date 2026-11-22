/*
 * Exercises every system call of this step and reports the results as
 * testrom markers: ticks and sleep, sys_info, mem_info, rand, log errors,
 * numbers outside the table, bad out pointers and the callee-saved registers.
 */
#include "rt.h"

#define ROM_SLOT_END    (PIP_ROM_BASE + PIP_ROM_SIZE)
#define KERNEL_ADDR     0x00008000u
#define PERIPH_BCM2835  0x20000000u
#define PERIPH_BCM2836  0x3F000000u

uint32_t regs_check(void);

static char nonul[PIP_LOG_MAX];

static void ticks(void)
{
    int32_t a = pip_ticks_ms();
    int32_t rc = pip_sleep_ms(50);
    int32_t b = pip_ticks_ms();
    int32_t c = pip_ticks_ms();

    rt_begin("ticks");
    rt_dec("a", a);
    rt_dec("b", b);
    rt_dec("c", c);
    rt_dec("sleep_rc", rc);
    rt_end();
}

/* Out-of-range sleeps return at once; a normal sleep still waits. */
static void sleep_range(void)
{
    int32_t a = pip_ticks_ms();
    int32_t big = pip_sleep_ms(0x80000000u);
    int32_t max = pip_sleep_ms(0xffffffffu);
    int32_t b = pip_ticks_ms();
    int32_t ok = pip_sleep_ms(20);
    int32_t c = pip_ticks_ms();

    rt_begin("sleep_range");
    rt_dec("big", big);
    rt_dec("max", max);
    rt_dec("ok", ok);
    rt_dec("fast_ms", b - a);
    rt_dec("slept_ms", c - b);
    rt_end();
}

/*
 * Sleeps of several lengths, each timed with ticks_ms. The first starts
 * right after a tick edge, the others wherever the last one ended.
 */
static void sleep_lengths(void)
{
    static const uint32_t lens[] = {1, 7, 10, 13, 25, 50};
    int32_t t = pip_ticks_ms();

    while (pip_ticks_ms() == t) {
    }
    for (unsigned i = 0; i < sizeof(lens) / sizeof(lens[0]); i++) {
        int32_t a = pip_ticks_ms();
        int32_t rc = pip_sleep_ms(lens[i]);
        int32_t b = pip_ticks_ms();

        rt_begin("sleep_len");
        rt_dec("ms", (int32_t)lens[i]);
        rt_dec("rc", rc);
        rt_dec("elapsed", b - a);
        rt_end();
    }
}

static void sys_info(void)
{
    pip_sys_info_t si;
    int32_t rc;

    memset(&si, 0xaa, sizeof(si));
    rc = pip_sys_info(&si);
    rt_begin("sys_info");
    rt_dec("rc", rc);
    rt_hex("size", si.size);
    rt_hex("abi_version", si.abi_version);
    rt_hex("pipos_version", si.pipos_version);
    rt_hex("board_rev", si.board_rev);
    rt_hex("soc", si.soc);
    rt_hex("mem_size", si.mem_size);
    rt_hex("reserved0", si.reserved[0]);
    rt_hex("reserved1", si.reserved[1]);
    rt_end();
}

static void mem_info(void)
{
    pip_mem_info_t mi;
    int32_t rc;

    memset(&mi, 0xaa, sizeof(mi));
    rc = pip_mem_info(&mi);
    rt_begin("mem_info");
    rt_dec("rc", rc);
    rt_hex("heap_base", mi.heap_base);
    rt_hex("heap_size", mi.heap_size);
    rt_hex("stack_top", mi.stack_top);
    rt_hex("stack_size", mi.stack_size);
    rt_end();
}

static void rand8(void)
{
    static const char *const keys[8] = { "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7" };
    int i;

    rt_begin("rand");
    for (i = 0; i < 8; i++) {
        rt_dec(keys[i], pip_rand());
    }
    rt_end();
}

static void log_errors(void)
{
    char *edge = (char *)(uintptr_t)(ROM_SLOT_END - 4);
    int32_t bad, cross, none, max, ctrl;

    bad = pip_log((const char *)(uintptr_t)KERNEL_ADDR);
    memcpy(edge, "abcd", 4);                  /* no NUL before the slot ends */
    cross = pip_log(edge);
    memset(nonul, 'A', sizeof(nonul));
    none = pip_log(nonul);
    nonul[PIP_LOG_MAX - 1] = '\0';            /* 255 bytes and the NUL: allowed */
    max = pip_log(nonul);
    pip_log("\n");
    ctrl = pip_log("ctl[a\001b\177c\200d\te\033f]\n");

    rt_begin("log");
    rt_dec("bad", bad);
    rt_dec("cross", cross);
    rt_dec("nonul", none);
    rt_dec("max", max);
    rt_dec("ctrl", ctrl);
    rt_end();
}

static void unknown(void)
{
    rt_begin("enosys");
    rt_dec("n00", pip_syscall(0x00, 0, 0, 0, 0));
    rt_dec("n7f", pip_syscall(0x7f, 0, 0, 0, 0));
    rt_dec("nffffffff", pip_syscall(0xffffffffu, 0, 0, 0, 0));
    rt_dec("n80", pip_syscall(0x80, 0, 0, 0, 0));
    rt_dec("n14", pip_syscall(0x14, 0, 0, 0, 0));     /* a gap in the table */
    rt_end();
}

static void out_pointers(void)
{
    rt_begin("outptr");
    rt_dec("sys_kernel", pip_sys_info((pip_sys_info_t *)(uintptr_t)KERNEL_ADDR));
    rt_dec("sys_periph1", pip_sys_info((pip_sys_info_t *)(uintptr_t)PERIPH_BCM2835));
    rt_dec("sys_periph2", pip_sys_info((pip_sys_info_t *)(uintptr_t)PERIPH_BCM2836));
    rt_dec("mem_kernel", pip_mem_info((pip_mem_info_t *)(uintptr_t)KERNEL_ADDR));
    rt_dec("mem_null", pip_mem_info((pip_mem_info_t *)0));
    rt_dec("sys_slot_end", pip_sys_info((pip_sys_info_t *)(uintptr_t)(ROM_SLOT_END - 16)));
    rt_end();
}

/*
 * mem_info into the last 8 bytes of the heap region: 16 bytes would
 * cross its end. Those bytes hold pip_main's saved registers, so they
 * are put back before anything returns.
 */
static void straddle(void)
{
    volatile uint8_t *top = (volatile uint8_t *)(uintptr_t)(PIP_ROM_STACK_TOP - 8);
    uint8_t keep[8];
    int32_t rc;
    int i, intact = 1;

    for (i = 0; i < 8; i++) {
        keep[i] = top[i];
        top[i] = 0x5a;
    }
    rc = pip_mem_info((pip_mem_info_t *)(uintptr_t)top);
    for (i = 0; i < 8; i++) {
        if (top[i] != 0x5a) {
            intact = 0;
        }
        top[i] = keep[i];
    }
    rt_begin("straddle");
    rt_dec("rc", rc);
    rt_dec("intact", intact);
    rt_end();
}

int pip_main(const pipos_startup_t *startup)
{
    (void)startup;
    ticks();
    sleep_range();
    sleep_lengths();
    sys_info();
    mem_info();
    rand8();
    log_errors();
    unknown();
    out_pointers();
    straddle();
    rt_begin("regs");
    rt_hex("changed", regs_check());
    rt_end();
    rt_begin("done");
    rt_end();
    return 0;
}
