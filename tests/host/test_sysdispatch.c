/*
 * kernel/sysdispatch.c: the index built from group tables, lookup and
 * dispatch, the checks against duplicate, foreign and unknown numbers,
 * the served mask, and the pointer helpers over a fake memory map.
 */
#include <stdint.h>
#include <string.h>

#include <pipos/abi.h>

#include "rc_test.h"
#include "sys.h"

static int32_t ret1(const pipos_sys_ctx_t *c, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    (void)c;
    return (int32_t)(1000 + a0 + a1 + a2 + a3);
}

static int32_t ret2(const pipos_sys_ctx_t *c, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    (void)a0, (void)a1, (void)a2, (void)a3;
    return c->kind == PIP_KIND_HOLOTAPE ? 2 : -2;
}

static const pipos_sys_entry_t sys_e[] = {
    PIPOS_SYS_ENTRY(SYSTEM, TICKS_MS, ret1),
    PIPOS_SYS_ENTRY(SYSTEM, LOG, NULL),
};
static const pipos_sys_entry_t gfx_e[] = {
    PIPOS_SYS_ENTRY(GFX, DRAW_LINE, ret2),
    PIPOS_SYS_ENTRY(GFX, SET_FONT, ret1),
};
static const pipos_sys_entry_t none_e[] = {};
static const pipos_sys_table_t sys_t = PIPOS_SYS_TABLE(SYSTEM, "system", sys_e);
static const pipos_sys_table_t gfx_t = PIPOS_SYS_TABLE(GFX, "gfx", gfx_e);
static const pipos_sys_table_t none_t = PIPOS_SYS_TABLE(HOLO, "holo", none_e);

static pipos_sys_index_t ix;

TEST(lookup_across_tables)
{
    const pipos_sys_table_t *t[] = { &sys_t, &none_t, &gfx_t };
    pipos_sys_ctx_t ctx = { PIP_KIND_HOLOTAPE, NULL, NULL };
    uint32_t bad = 99;

    CHECK_EQ(none_t.count, 0);
    CHECK_EQ(pipos_sys_index_build(&ix, t, 3, &bad), PIPOS_SYS_OK);
    CHECK_EQ(bad, 0);
    CHECK(pipos_sys_lookup(&ix, PIP_SYS_TICKS_MS) == ret1);
    CHECK(pipos_sys_lookup(&ix, PIP_SYS_DRAW_LINE) == ret2);
    CHECK(pipos_sys_lookup(&ix, PIP_SYS_SET_FONT) == ret1);
    CHECK(pipos_sys_lookup(&ix, PIP_SYS_LOG) == NULL);
    CHECK(pipos_sys_lookup(&ix, PIP_SYS_RAND) == NULL);
    CHECK(pipos_sys_lookup(&ix, 0x80) == NULL);
    CHECK(pipos_sys_lookup(&ix, 0xffffffffu) == NULL);
    CHECK_EQ(ix.owner[PIP_SYS_DRAW_LINE], PIPOS_GROUP_GFX);
    CHECK_EQ(ix.owner[PIP_SYS_LOG], PIPOS_GROUP_SYSTEM);
    CHECK_EQ(ix.owner[PIP_SYS_RAND], -1);

    CHECK_EQ(pipos_sys_dispatch(&ix, &ctx, PIP_SYS_TICKS_MS, 1, 2, 3, 4), 1010);
    CHECK_EQ(pipos_sys_dispatch(&ix, &ctx, PIP_SYS_DRAW_LINE, 0, 0, 0, 0), 2);
    ctx.kind = PIP_KIND_ROM;
    CHECK_EQ(pipos_sys_dispatch(&ix, &ctx, PIP_SYS_DRAW_LINE, 0, 0, 0, 0), -2);
    /* Listed with no handler, in the ABI but unlisted, outside the ABI. */
    CHECK_EQ(pipos_sys_dispatch(&ix, &ctx, PIP_SYS_LOG, 0, 0, 0, 0), -PIP_ENOSYS);
    CHECK_EQ(pipos_sys_dispatch(&ix, &ctx, PIP_SYS_RAND, 0, 0, 0, 0), -PIP_ENOSYS);
    CHECK_EQ(pipos_sys_dispatch(&ix, &ctx, 0x00, 0, 0, 0, 0), -PIP_ENOSYS);
    CHECK_EQ(pipos_sys_dispatch(&ix, &ctx, 0x7f, 0, 0, 0, 0), -PIP_ENOSYS);
    CHECK_EQ(pipos_sys_dispatch(&ix, &ctx, 0x80000042u, 0, 0, 0, 0), -PIP_ENOSYS);
}

TEST(served_mask)
{
    const pipos_sys_table_t *t[] = { &sys_t, &gfx_t };
    uint32_t mask[4], bad;

    CHECK_EQ(pipos_sys_index_build(&ix, t, 2, &bad), PIPOS_SYS_OK);
    pipos_sys_served(&ix, mask);
    CHECK_EQ(mask[0], (1u << PIP_SYS_DRAW_LINE) | (1u << PIP_SYS_SET_FONT));
    CHECK_EQ(mask[1], 0u);
    CHECK_EQ(mask[2], 1u << (PIP_SYS_TICKS_MS - 64));   /* log has no handler */
    CHECK_EQ(mask[3], 0u);
}

TEST(duplicate_numbers)
{
    static const pipos_sys_entry_t again[] = { PIPOS_SYS_ENTRY(GFX, SET_FONT, ret2) };
    static const pipos_sys_table_t again_t = PIPOS_SYS_TABLE(GFX, "gfx2", again);
    static const pipos_sys_entry_t twice[] = {
        PIPOS_SYS_ENTRY(SYSTEM, RAND, ret1),
        PIPOS_SYS_ENTRY(SYSTEM, RAND, NULL),
    };
    static const pipos_sys_table_t twice_t = PIPOS_SYS_TABLE(SYSTEM, "system", twice);
    const pipos_sys_table_t *across[] = { &sys_t, &gfx_t, &again_t };
    const pipos_sys_table_t *within[] = { &twice_t };
    uint32_t bad = 0;

    CHECK_EQ(pipos_sys_index_build(&ix, across, 3, &bad), PIPOS_SYS_EDUP);
    CHECK_EQ(bad, PIP_SYS_SET_FONT);
    CHECK_EQ(pipos_sys_index_build(&ix, within, 1, &bad), PIPOS_SYS_EDUP);
    CHECK_EQ(bad, PIP_SYS_RAND);
}

TEST(numbers_outside_the_abi_or_group)
{
    /* Raw entries: PIPOS_SYS_ENTRY would refuse these at build time. */
    static const pipos_sys_entry_t gap[] = { { 0x14, ret1 } };
    static const pipos_sys_entry_t high[] = { { 0x80, ret1 } };
    static const pipos_sys_entry_t huge[] = { { 0xffffffffu, ret1 } };
    static const pipos_sys_entry_t power[] = { { PIP_SYS_POWER, ret1 } };
    static const pipos_sys_table_t gap_t = PIPOS_SYS_TABLE(INPUT, "input", gap);
    static const pipos_sys_table_t high_t = PIPOS_SYS_TABLE(HOLO, "holo", high);
    static const pipos_sys_table_t huge_t = PIPOS_SYS_TABLE(HOLO, "holo", huge);
    static const pipos_sys_table_t power_t = PIPOS_SYS_TABLE(SYSTEM, "system", power);
    const pipos_sys_table_t *t[1];
    uint32_t bad = 0;

    t[0] = &gap_t;
    CHECK_EQ(pipos_sys_index_build(&ix, t, 1, &bad), PIPOS_SYS_EABI);
    CHECK_EQ(bad, 0x14);
    t[0] = &high_t;
    CHECK_EQ(pipos_sys_index_build(&ix, t, 1, &bad), PIPOS_SYS_EABI);
    t[0] = &huge_t;
    CHECK_EQ(pipos_sys_index_build(&ix, t, 1, &bad), PIPOS_SYS_EABI);
    CHECK_EQ(bad, 0xffffffffu);
    t[0] = &power_t;    /* 0x47 sits among the system numbers but is the time group's */
    CHECK_EQ(pipos_sys_index_build(&ix, t, 1, &bad), PIPOS_SYS_EOWNER);
    CHECK_EQ(bad, PIP_SYS_POWER);
}

#define ROW(NAME, name) PIP_SYS_##NAME,
static const unsigned abi_numbers[] = { PIP_SYSCALLS(ROW) };

TEST(owner_map_covers_the_abi_exactly)
{
    unsigned nr, i;

    for (nr = 0; nr < PIP_SYS_NR_LIMIT; nr++) {
        int in_abi = 0;

        for (i = 0; i < sizeof(abi_numbers) / sizeof(abi_numbers[0]); i++) {
            in_abi |= abi_numbers[i] == nr;
        }
        CHECK_EQ(PIPOS_SYS_OWNER(nr) >= 0, in_abi);
    }
}

TEST(owner_map_follows_the_layout)
{
    CHECK_EQ(PIPOS_SYS_OWNER(0x01), PIPOS_GROUP_GFX);
    CHECK_EQ(PIPOS_SYS_OWNER(0x0F), PIPOS_GROUP_GFX);
    CHECK_EQ(PIPOS_SYS_OWNER(0x10), PIPOS_GROUP_INPUT);
    CHECK_EQ(PIPOS_SYS_OWNER(0x13), PIPOS_GROUP_INPUT);
    CHECK_EQ(PIPOS_SYS_OWNER(0x20), PIPOS_GROUP_AUDIO);
    CHECK_EQ(PIPOS_SYS_OWNER(0x25), PIPOS_GROUP_AUDIO);
    CHECK_EQ(PIPOS_SYS_OWNER(0x26), PIPOS_GROUP_AUDIO);
    CHECK_EQ(PIPOS_SYS_OWNER(0x27), PIPOS_GROUP_AUDIO);
    CHECK_EQ(PIPOS_SYS_OWNER(0x28), -1);
    CHECK_EQ(PIPOS_SYS_OWNER(0x30), PIPOS_GROUP_TIME);
    CHECK_EQ(PIPOS_SYS_OWNER(0x31), PIPOS_GROUP_TIME);
    CHECK_EQ(PIPOS_SYS_OWNER(0x40), PIPOS_GROUP_TIME);
    CHECK_EQ(PIPOS_SYS_OWNER(0x41), PIPOS_GROUP_TIME);
    CHECK_EQ(PIPOS_SYS_OWNER(0x47), PIPOS_GROUP_TIME);
    CHECK_EQ(PIPOS_SYS_OWNER(0x4A), PIPOS_GROUP_TIME);
    CHECK_EQ(PIPOS_SYS_OWNER(0x42), PIPOS_GROUP_SYSTEM);
    CHECK_EQ(PIPOS_SYS_OWNER(0x46), PIPOS_GROUP_SYSTEM);
    CHECK_EQ(PIPOS_SYS_OWNER(0x48), PIPOS_GROUP_SYSTEM);
    CHECK_EQ(PIPOS_SYS_OWNER(0x49), PIPOS_GROUP_SYSTEM);
    CHECK_EQ(PIPOS_SYS_OWNER(0x50), PIPOS_GROUP_STORAGE);
    CHECK_EQ(PIPOS_SYS_OWNER(0x57), PIPOS_GROUP_STORAGE);
    CHECK_EQ(PIPOS_SYS_OWNER(0x60), PIPOS_GROUP_HOLO);
    CHECK_EQ(PIPOS_SYS_OWNER(0x63), PIPOS_GROUP_HOLO);
}

/* ---- pointer helpers over a fake memory map ---------------------------- */

/* The caller owns mem[0..32) read-only and mem[32..64) read-write. */
static char mem[64];
#define RO ((uintptr_t)mem)
#define RW ((uintptr_t)mem + 32)
#define END ((uintptr_t)mem + 64)

static int fake_range_ok(uintptr_t p, size_t n, int write)
{
    uintptr_t lo = write ? RW : RO;

    return p >= lo && p <= END && n <= END - p;
}

static long fake_strnlen_ok(uintptr_t p, size_t max)
{
    size_t i;

    for (i = 0; i < max; i++) {
        if (!fake_range_ok(p + i, 1, 0)) {
            return -1;
        }
        if (((const char *)p)[i] == '\0') {
            return (long)i;
        }
    }
    return -1;
}

static const pipos_user_mem_t fake = { fake_range_ok, fake_strnlen_ok };
static const pipos_sys_ctx_t rom = { PIP_KIND_ROM, NULL, &fake };
static const pipos_sys_ctx_t holo = { PIP_KIND_HOLOTAPE, NULL, &fake };

TEST(rom_only)
{
    CHECK_EQ(pipos_sys_rom_only(&rom), 0);
    CHECK_EQ(pipos_sys_rom_only(&holo), -PIP_EPERM);
}

TEST(check_out_and_in)
{
    CHECK_EQ(pipos_user_check_out(&rom, RW, 32), 0);
    CHECK_EQ(pipos_user_check_out(&rom, RW + 16, 17), -PIP_EFAULT);   /* crosses the end */
    CHECK_EQ(pipos_user_check_out(&rom, RO, 4), -PIP_EFAULT);         /* read only */
    CHECK_EQ(pipos_user_check_in(&rom, RO, 64), 0);
    CHECK_EQ(pipos_user_check_in(&rom, RO - 1, 2), -PIP_EFAULT);
    CHECK_EQ(pipos_user_check_in(&rom, UINTPTR_MAX - 3, 8), -PIP_EFAULT); /* wraps */
}

TEST(copy_out_and_in)
{
    pip_mem_info_t m = { 1, 2, 3, 4 }, back;

    memset(mem, 0, sizeof(mem));
    CHECK_EQ(pipos_user_copy_out(&rom, RW + 8, &m, sizeof(m)), 0);
    CHECK_MEM(mem + 40, &m, sizeof(m));
    CHECK_EQ(pipos_user_copy_out(&rom, RW + 24, &m, sizeof(m)), -PIP_EFAULT);
    CHECK_EQ(mem[56], 0);                       /* nothing written on failure */
    CHECK_EQ(pipos_user_copy_in(&rom, &back, RW + 8, sizeof(back)), 0);
    CHECK_MEM(&back, &m, sizeof(m));
    CHECK_EQ(pipos_user_copy_in(&rom, &back, END - 8, sizeof(back)), -PIP_EFAULT);
}

TEST(strings)
{
    char buf[8];

    memset(mem, 'x', sizeof(mem));
    memcpy(mem, "save", 5);
    CHECK_EQ(pipos_user_strlen(&rom, RO, 8), 4);
    CHECK_EQ(pipos_user_strlen(&rom, RO, 5), 4);
    CHECK_EQ(pipos_user_strlen(&rom, RO, 4), -PIP_EINVAL);    /* no NUL in 4 bytes */
    CHECK_EQ(pipos_user_strlen(&rom, RO, 0), -PIP_EINVAL);
    CHECK_EQ(pipos_user_strlen(&rom, RO + 10, 16), -PIP_EINVAL);
    CHECK_EQ(pipos_user_strlen(&rom, END - 4, 16), -PIP_EFAULT); /* runs out of memory */
    CHECK_EQ(pipos_user_strlen(&rom, RO - 1, 16), -PIP_EFAULT);
    memset(buf, '#', sizeof(buf));
    CHECK_EQ(pipos_user_str(&rom, buf, RO, sizeof(buf)), 4);
    CHECK_STR(buf, "save");
    CHECK_EQ(buf[5], '#');
    memset(buf, '#', sizeof(buf));
    CHECK_EQ(pipos_user_str(&rom, buf, RO + 10, sizeof(buf)), -PIP_EINVAL);
    CHECK_EQ(buf[0], '#');
}
