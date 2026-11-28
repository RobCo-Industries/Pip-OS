/*
 * The conformance ROM's companion holotape. Its argument string picks
 * what it does:
 *
 *   check X   checks its entry state, its memory limits by return values
 *             and the calls a holotape may not make. X is the hex address
 *             of a text in the ROM slot. Returns 42 when every check
 *             passes, else 100 plus the number of failures.
 *   exit N    exit(N), N decimal. The ROM sees the low 8 bits.
 *   fault     writes to the ROM slot, which a holotape may only read.
 *   rate      sets its sample rate to 37500 Hz and returns the rate it
 *             started with, divided by 100: 80 for 8000 Hz.
 *   kind      sets its kind to recordings and returns the kind it started
 *             with: 3 for games.
 *   trim      sets its trim to 40 and returns the trim it started with:
 *             100.
 *
 * Each check prints "@RC stage=conform ev=tape_check name=<name> ok=<0|1>".
 */
#include <pipos/pip.h>

#define TAPE_OK 42

static int failures;

static void tcheck(const char *name, int ok)
{
    failures += !ok;
    pip_printf("@RC stage=conform ev=tape_check name=%s ok=%d\n", name, ok ? 1 : 0);
}

static uint32_t hex(const char *s)
{
    uint32_t v = 0;

    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }
    for (; (*s >= '0' && *s <= '9') || (*s >= 'a' && *s <= 'f'); s++) {
        v = v << 4 | (uint32_t)(*s <= '9' ? *s - '0' : *s - 'a' + 10);
    }
    return v;
}

static int32_t dec(const char *s)
{
    int neg = *s == '-';
    int32_t v = 0;

    for (s += neg; *s >= '0' && *s <= '9'; s++) {
        v = v * 10 + (*s - '0');
    }
    return neg ? -v : v;
}

static const uint32_t level_which[5] = {
    PIP_SND_RADIO, PIP_SND_SFX, PIP_SND_RECORDINGS, PIP_SND_GAMES, PIP_LEVEL_MASTER,
};

static const char *const level_names[5] = {
    "level_radio_reads", "level_sfx_reads", "level_recordings_reads", "level_games_reads",
    "level_master_reads",
};

/* Kinds, levels and the trim of ABI 1.6, as a holotape sees them. */
static void check_sound(void)
{
    int32_t lv[5], master;
    int i;

    tcheck("kind_start_games", pip_sound_kind(PIP_SND_GAMES) == PIP_SND_GAMES);
    tcheck("kind_recordings", pip_sound_kind(PIP_SND_RECORDINGS) == PIP_SND_GAMES);
    tcheck("kind_games_back", pip_sound_kind(PIP_SND_GAMES) == PIP_SND_RECORDINGS);
    tcheck("kind_radio_eperm", pip_sound_kind(PIP_SND_RADIO) == -PIP_EPERM);
    tcheck("kind_sfx_eperm", pip_sound_kind(PIP_SND_SFX) == -PIP_EPERM);
    tcheck("kind_master_einval", pip_sound_kind(PIP_LEVEL_MASTER) == -PIP_EINVAL);
    tcheck("kind_refused_keeps", pip_sound_kind(PIP_SND_GAMES) == PIP_SND_GAMES);
    for (i = 0; i < 5; i++) {
        lv[i] = pip_sound_level(level_which[i], PIP_LEVEL_ASK);
        tcheck(level_names[i], lv[i] >= 0 && lv[i] <= PIP_VOLUME_MAX);
    }
    master = lv[4];
    tcheck("level_set_eperm", pip_sound_level(PIP_SND_GAMES, 50) == -PIP_EPERM);
    tcheck("level_master_eperm", pip_sound_level(PIP_LEVEL_MASTER, 50) == -PIP_EPERM);
    tcheck("level_eperm_keeps",
           pip_sound_level(PIP_SND_GAMES, PIP_LEVEL_ASK) == lv[3] &&
           pip_sound_level(PIP_LEVEL_MASTER, PIP_LEVEL_ASK) == master);
    /* A bad which is an argument error before it is a caller error. */
    tcheck("level_which5_einval", pip_sound_level(5, 50) == -PIP_EINVAL);
    tcheck("store_eperm", pip_sound_store() == -PIP_EPERM);
    tcheck("trim_starts_100", pip_volume(100) == 100);
    tcheck("trim_set", pip_volume(40) == 100);
    tcheck("trim_master_keeps", pip_sound_level(PIP_LEVEL_MASTER, PIP_LEVEL_ASK) == master);
    tcheck("trim_101", pip_volume(101) == -PIP_EINVAL);
    /* volume is the only way to read the trim: set 40 again, expect 40. */
    tcheck("trim_kept", pip_volume(40) == 40);
}

static int check(const pipos_startup_t *su, uint32_t rom_text)
{
    const uint32_t heap = 0x01800000u, top = 0x02000000u;
    pip_mem_info_t mi;
    pip_sys_info_t si;

    tcheck("entry_kind", su->kind == PIP_KIND_HOLOTAPE);
    tcheck("entry_size", su->size == sizeof(pipos_startup_t));
    tcheck("entry_abi", su->abi_version == PIP_ABI_VERSION);
    tcheck("entry_heap", su->heap_base == heap && su->heap_size == 0x007F0000u);
    tcheck("entry_stack", su->stack_top == top);
    tcheck("entry_flags", su->flags == 0);
    tcheck("entry_args", strncmp(su->args, "check ", 6) == 0);
    tcheck("mem_info", pip_mem_info(&mi) == 0 && mi.heap_base == heap &&
           mi.heap_size == 0x007F0000u && mi.stack_top == top &&
           mi.stack_size == PIP_STACK_SIZE);
    /* Memory limits, by return values only. */
    tcheck("own_heap_ok", pip_sys_info((pip_sys_info_t *)(uintptr_t)(heap + 0x400000u)) == 0);
    tcheck("own_end_straddle", pip_sys_info((pip_sys_info_t *)(uintptr_t)(top - 4)) ==
           -PIP_EFAULT);
    tcheck("rom_heap_refused", pip_sys_info((pip_sys_info_t *)(uintptr_t)PIP_ROM_HEAP_BASE) ==
           -PIP_EFAULT);
    tcheck("rom_slot_readable", pip_log((const char *)(uintptr_t)rom_text) == 0);
    tcheck("rom_slot_not_out", pip_sys_info((pip_sys_info_t *)(uintptr_t)rom_text) ==
           -PIP_EFAULT);
    tcheck("heap_alloc", pip_malloc(4096) != NULL);
    /* Calls only the ROM may make. */
    tcheck("launch_eperm", pip_holo_launch("CONFTAPE.HTP", "exit 1") == -PIP_EPERM);
    tcheck("power_eperm", pip_power(PIP_POWER_SCREEN_ON) == -PIP_EPERM);
    tcheck("set_time_eperm", pip_set_time(1700000000u) == -PIP_EPERM);
    tcheck("sys_info", pip_sys_info(&si) == 0 && si.abi_version == PIP_ABI_VERSION);
    check_sound();
    return failures ? 100 + failures : TAPE_OK;
}

int pip_main(const pipos_startup_t *su)
{
    const char *a = su->args;

    if (strncmp(a, "check ", 6) == 0) {
        return check(su, hex(a + 6));
    }
    if (strncmp(a, "exit ", 5) == 0) {
        pip_exit(dec(a + 5));
    }
    if (strcmp(a, "rate") == 0) {
        int32_t prev = pip_sample_rate(PIP_SAMPLE_RATE_HI);

        return prev < 0 ? 1 : prev / 100;
    }
    if (strcmp(a, "kind") == 0) {
        int32_t prev = pip_sound_kind(PIP_SND_RECORDINGS);

        return prev < 0 ? 99 : prev;
    }
    if (strcmp(a, "trim") == 0) {
        int32_t prev = pip_volume(40);

        return prev < 0 ? 99 : prev;
    }
    if (strcmp(a, "fault") == 0) {
        *(volatile uint32_t *)(uintptr_t)PIP_ROM_BASE = 0;
        return 1;
    }
    return 99;
}
