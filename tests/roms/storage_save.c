/*
 * Save calls on a card without /PIPBOY/SAVE: write, read back, info,
 * overwrite shorter and longer, delete, the 64 KiB limit, names, and bad
 * pointers that must write nothing. The test reads the card afterwards.
 */
#include "rt.h"

#define SLOT_END    (PIP_ROM_BASE + PIP_ROM_SIZE)
#define KERNEL_ADDR 0x00008000u
#define PERIPH      0x20000000u

static uint8_t data[PIP_SAVE_MAX + 1];
static uint8_t back[PIP_SAVE_MAX + 16];

/* The bytes the test expects on the card too: see storage_pattern() in the test. */
static void fill(uint32_t seed, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        data[i] = (uint8_t)(i * 31u + seed + (i >> 8));
    }
}

static uint32_t info(const char *name)
{
    uint32_t size = 0xdeadbeefu;
    int32_t rc = pip_save_info(name, &size);

    return rc == 0 ? size : (uint32_t)rc;
}

/* Writes, then reads back through a bigger buffer, and compares. */
static void round_trip(const char *tag, const char *name, uint32_t seed, uint32_t n)
{
    char key[48];
    int32_t rc;

    fill(seed, n);
    pip_snprintf(key, sizeof(key), "%s_write", tag);
    rt_check_eq(key, pip_write_save(name, data, n), (int32_t)n);
    pip_snprintf(key, sizeof(key), "%s_info", tag);
    rt_check_eq(key, (int32_t)info(name), (int32_t)n);
    memset(back, 0xee, sizeof(back));
    rc = pip_read_save(name, back, sizeof(back));
    pip_snprintf(key, sizeof(key), "%s_read", tag);
    rt_check_eq(key, rc, (int32_t)n);
    pip_snprintf(key, sizeof(key), "%s_same", tag);
    rt_check(key, memcmp(back, data, n) == 0 && back[n] == 0xee);
}

static void basics(void)
{
    rt_check_eq("missing_info", (int32_t)info("SLOT1"), -PIP_ENOENT);
    rt_check_eq("missing_read", pip_read_save("SLOT1", back, 16), -PIP_ENOENT);
    rt_check_eq("missing_delete", pip_save_delete("SLOT1"), -PIP_ENOENT);

    round_trip("first", "slot1", 1, 1000);
    memset(back, 0xee, sizeof(back));
    rt_check_eq("small_read", pip_read_save("SLOT1", back, 10), 10);
    rt_check("small_same", memcmp(back, data, 10) == 0 && back[10] == 0xee);
    rt_check_eq("zero_read", pip_read_save("Slot1", NULL, 0), 0);
    round_trip("shorter", "SLOT1", 2, 300);
    round_trip("longer", "SLOT1", 3, 5000);
    rt_check_eq("delete", pip_save_delete("slot1"), 0);
    rt_check_eq("gone_info", (int32_t)info("SLOT1"), -PIP_ENOENT);
    rt_check_eq("gone_read", pip_read_save("SLOT1", back, 16), -PIP_ENOENT);
    rt_check_eq("gone_delete", pip_save_delete("SLOT1"), -PIP_ENOENT);
    round_trip("empty", "EMPTY", 4, 0);
    round_trip("long_name", "abcdefgh", 5, 777);
    round_trip("edge_chars", "_09AZ", 6, 33);
}

static void limits(void)
{
    int32_t t0, t1;

    fill(9, PIP_SAVE_MAX);
    t0 = pip_ticks_ms();
    rt_check_eq("max_write", pip_write_save("MAX", data, PIP_SAVE_MAX), PIP_SAVE_MAX);
    t1 = pip_ticks_ms();
    rt_begin("timing");
    rt_dec("write_64k_ms", t1 - t0);
    rt_end();
    rt_check_eq("max_info", (int32_t)info("MAX"), PIP_SAVE_MAX);
    memset(back, 0xee, sizeof(back));
    rt_check_eq("max_read", pip_read_save("MAX", back, sizeof(back)), PIP_SAVE_MAX);
    rt_check("max_same", memcmp(back, data, PIP_SAVE_MAX) == 0);
    rt_check_eq("over_write", pip_write_save("MAX", data, PIP_SAVE_MAX + 1), -PIP_ERANGE);
    rt_check_eq("huge_write", pip_write_save("MAX", data, 0xffffffffu), -PIP_ERANGE);
    rt_check_eq("over_kept", (int32_t)info("MAX"), PIP_SAVE_MAX);
}

static void names(void)
{
    char *edge = (char *)(uintptr_t)(SLOT_END - 3);
    static char nine[12];

    rt_check_eq("name_empty", pip_write_save("", data, 1), -PIP_EINVAL);
    rt_check_eq("name_nine", pip_write_save("ABCDEFGHI", data, 1), -PIP_EINVAL);
    rt_check_eq("name_dot", pip_write_save("A.SAV", data, 1), -PIP_EINVAL);
    rt_check_eq("name_dash", pip_save_info("A-B", (uint32_t *)back), -PIP_EINVAL);
    rt_check_eq("name_tilde", pip_read_save("~A", back, 1), -PIP_EINVAL);
    rt_check_eq("name_hash", pip_save_delete("#A"), -PIP_EINVAL);
    rt_check_eq("name_slash", pip_read_save("../X", back, 1), -PIP_EINVAL);
    rt_check_eq("name_space", pip_read_save("A B", back, 1), -PIP_EINVAL);
    rt_check_eq("name_high", pip_read_save("\xc3\xa9", back, 1), -PIP_EINVAL);
    memset(nine, 'A', sizeof(nine));       /* no NUL in the first 9 bytes */
    rt_check_eq("name_nonul", pip_write_save(nine, data, 1), -PIP_EINVAL);
    rt_check_eq("name_kernel", pip_write_save((const char *)KERNEL_ADDR, data, 1), -PIP_EFAULT);
    rt_check_eq("name_null", pip_read_save(NULL, back, 1), -PIP_EFAULT);
    memcpy(edge, "ABC", 3);                /* runs off the end of the slot */
    rt_check_eq("name_cross", pip_save_info(edge, (uint32_t *)back), -PIP_EFAULT);
}

static int intact(volatile uint8_t *p, unsigned n)
{
    for (unsigned i = 0; i < n; i++) {
        if (p[i] != 0x5a) {
            return 0;
        }
    }
    return 1;
}

static void pointers(void)
{
    volatile uint8_t *edge = (volatile uint8_t *)(uintptr_t)(SLOT_END - 8);

    fill(10, 64);
    rt_check_eq("ptr_write", pip_write_save("PTR", data, 64), 64);
    rt_check_eq("wbuf_kernel", pip_write_save("NEWPTR", (void *)KERNEL_ADDR, 16), -PIP_EFAULT);
    rt_check_eq("wbuf_periph", pip_write_save("NEWPTR", (void *)PERIPH, 16), -PIP_EFAULT);
    rt_check_eq("wbuf_cross", pip_write_save("NEWPTR", (void *)edge, 16), -PIP_EFAULT);
    rt_check_eq("wbuf_none", (int32_t)info("NEWPTR"), -PIP_ENOENT);

    rt_check_eq("rbuf_kernel", pip_read_save("PTR", (void *)KERNEL_ADDR, 16), -PIP_EFAULT);
    rt_check_eq("rbuf_periph", pip_read_save("PTR", (void *)PERIPH, 16), -PIP_EFAULT);
    memset((void *)edge, 0x5a, 8);
    rt_check_eq("rbuf_cross", pip_read_save("PTR", (void *)edge, 16), -PIP_EFAULT);
    rt_check("rbuf_intact", intact(edge, 8));
    /* A buffer that fits the save but not size: still refused, nothing written. */
    rt_check_eq("rbuf_size", pip_read_save("PTR", (void *)edge, 64), -PIP_EFAULT);
    rt_check("rbuf_size_intact", intact(edge, 8));

    rt_check_eq("info_kernel", pip_save_info("PTR", (uint32_t *)KERNEL_ADDR), -PIP_EFAULT);
    memset((void *)edge, 0x5a, 8);
    rt_check_eq("info_cross", pip_save_info("PTR", (uint32_t *)(SLOT_END - 2)), -PIP_EFAULT);
    rt_check("info_intact", intact(edge, 8));
}

int pip_main(const pipos_startup_t *startup)
{
    (void)startup;
    basics();
    limits();
    names();
    pointers();
    return rt_checks_done();
}
