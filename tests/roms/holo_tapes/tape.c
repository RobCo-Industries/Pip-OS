/*
 * The test holotape. Its argument string picks what it does:
 *
 *   ""          returns 200
 *   exit N      exit(N), N decimal, may be negative
 *   ret N       returns N from pip_main
 *   args        returns strlen(args) when every byte after "args" is
 *               'a' + i % 26, else 254
 *   info X      checks the holotape's view of the ABI; X is the hex
 *               address of a string in the ROM slot. Returns the failures.
 *   state H     the state handover: H is the ROM's open handle. Draws,
 *               then sleeps until the kernel kills it.
 *   openfile    opens a file, leaves it open and returns 0
 *   rd X        returns the low byte of the word at hex address X
 *   wr X        writes the word at X, then returns 1
 *   jmp X       branches to X, then returns 1
 *   undef       runs an undefined instruction
 *   loop        logs ev=tloop, then calls sleep_ms(20) until the kernel ends it
 *   ticks       logs ev=tloop, then calls only ticks_ms until the kernel ends it
 *   eject       returns holo_eject_event()
 *   rate        sets the rate to 37500, plays 375 bytes at it, waits for
 *               the end and returns the rate it started with / 100: 80
 *   levels      the sound calls of ABI 1.6 from a holotape: the kind, the
 *               trim through volume, the refused calls and the five reads,
 *               on the card of tests/test_audio_levels.py. Returns the
 *               failures.
 *   trim        returns volume(100): the trim it started with
 *
 * Checks print "@RC stage=testrom ev=tcheck name=<name> ok=<0|1>", apart
 * from the ROM's ev=check lines. Faults print "ev=tfault pc=<hex>" first.
 */
#include "rt.h"

static const pipos_startup_t *su;
static int failures;

static int tcheck(const char *name, int ok)
{
    char line[96];

    failures += !ok;
    pip_snprintf(line, sizeof(line), "@RC stage=testrom ev=tcheck name=%s ok=%d\n", name,
                 ok ? 1 : 0);
    pip_log(line);
    return ok;
}

static int starts(const char *s, const char *word)
{
    size_t n = strlen(word);

    return strncmp(s, word, n) == 0 && (s[n] == '\0' || s[n] == ' ');
}

static const char *arg(const char *s)
{
    const char *sp = strchr(s, ' ');

    return sp != NULL ? sp + 1 : "";
}

static uint32_t hex(const char *s)
{
    uint32_t v = 0;

    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }
    for (; *s != '\0'; s++) {
        char c = *s;
        uint32_t d = c >= '0' && c <= '9' ? (uint32_t)(c - '0') :
                     c >= 'a' && c <= 'f' ? (uint32_t)(c - 'a' + 10) :
                     c >= 'A' && c <= 'F' ? (uint32_t)(c - 'A' + 10) : 16u;

        if (d > 15) {
            break;
        }
        v = v << 4 | d;
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

static void say_fault(uint32_t pc)
{
    rt_begin("tfault");
    rt_hex("pc", pc);
    rt_end();
}

static int check_args(const char *a)
{
    size_t n = strlen(a), i;

    for (i = 4; i < n; i++) {
        if (a[i] != (char)('a' + (i - 4) % 26)) {
            return 254;
        }
    }
    return (int)n;
}

static int info(uint32_t rom_str)
{
    pip_sys_info_t si;
    pip_mem_info_t mi;
    pip_holo_info_t hi[2];

    tcheck("su_kind", su->kind == PIP_KIND_HOLOTAPE);
    tcheck("su_size", su->size == 128);
    tcheck("su_abi", su->abi_version == PIP_ABI_VERSION);
    tcheck("su_heap", su->heap_base == 0x01800000u && su->heap_size == 0x007F0000u);
    tcheck("su_stack", su->stack_top == 0x02000000u);
    tcheck("su_flags", su->flags == 0);
    tcheck("su_at", (uint32_t)(uintptr_t)su == 0x01FF0000u);
    tcheck("sys_info", pip_sys_info(&si) == 0 && si.size == 32 &&
           si.abi_version == PIP_ABI_VERSION);
    tcheck("mem_info", pip_mem_info(&mi) == 0 && mi.heap_base == 0x01800000u &&
           mi.heap_size == 0x007F0000u && mi.stack_top == 0x02000000u &&
           mi.stack_size == 0x00010000u);
    tcheck("heap_works", pip_malloc(1000) != NULL);
    tcheck("launch_eperm", pip_holo_launch("TAPE.HTP", "") == -PIP_EPERM);
    tcheck("launch_bad_eperm", pip_holo_launch(NULL, NULL) == -PIP_EPERM);
    tcheck("power_eperm", pip_power(3) == -PIP_EPERM);
    tcheck("power_bad_eperm", pip_power(9) == -PIP_EPERM);
    tcheck("set_time_eperm", pip_set_time(1000000000u) == -PIP_EPERM);
    tcheck("last_status", pip_holo_last_status() == 0);
    tcheck("eject", pip_holo_eject_event() == 0);
    tcheck("enumerate", pip_holo_enumerate(hi, 2) >= 1 && hi[0].file[0] != '\0');
    /* Input pointers may point into the read-only ROM slot, output pointers may not. */
    tcheck("in_rom_slot", pip_log((const char *)(uintptr_t)rom_str) == 0);
    tcheck("out_rom_slot", pip_sys_info((pip_sys_info_t *)(uintptr_t)rom_str) == -PIP_EFAULT);
    tcheck("enum_rom_slot",
           pip_holo_enumerate((pip_holo_info_t *)(uintptr_t)rom_str, 1) == -PIP_EFAULT);
    tcheck("in_rom_heap", pip_log((const char *)(uintptr_t)0x017F0000u) == -PIP_EFAULT);
    tcheck("out_rom_heap", pip_sys_info((pip_sys_info_t *)(uintptr_t)0x017F0000u) == -PIP_EFAULT);
    tcheck("out_holo_heap", pip_sys_info((pip_sys_info_t *)(uintptr_t)0x01800000u) == 0);
    return failures;
}

static void state(int32_t h)
{
    pip_event_t ev;
    char buf[8] = { 0 };
    int32_t mine;

    tcheck("st_kind", su->kind == PIP_KIND_HOLOTAPE);
    tcheck("st_no_events", pip_poll_event(&ev) == 0);
    tcheck("st_no_sound", pip_audio_status() == 0);
    tcheck("st_rom_read", pip_file_read(h, buf, 4) == -PIP_EBADF);
    tcheck("st_rom_close", pip_file_close(h) == -PIP_EBADF);
    mine = pip_file_open("/CARD.TXT");
    tcheck("st_open", mine == 0);
    tcheck("st_read", pip_file_read(mine, buf, 4) == 4 && memcmp(buf, "ABCD", 4) == 0);
    tcheck("st_tone", pip_tone(880, 10000) == 0 && pip_audio_status() == 1);
    /* The default colour and no clip: this fills the whole screen. */
    pip_rect_xywh(0, 0, PIP_SCREEN_W, PIP_SCREEN_H, PIP_RECT_FILLED);
    pip_present();
    rt_begin("tape_drawn");
    rt_end();
    for (;;) {
        pip_sleep_ms(20);
    }
}

/* Waits until the tape's sound ends, at most 3 s. */
static void quiet(void)
{
    uint32_t t0 = pip_ticks_ms();

    while (pip_audio_status() == 1 && pip_ticks_ms() - t0 < 3000u) {
        pip_sleep_ms(5);
    }
}

/*
 * The card sets volume 80, radio 40, sfx 100, recordings 50 and games 30.
 * The trace shows each start's gain: tests/test_audio_levels.py.
 */
static int levels(void)
{
    static uint8_t pcm[8000];
    uint32_t i;

    for (i = 0; i < sizeof(pcm); i++) {
        pcm[i] = (uint8_t)(128u + (i & 15u));
    }
    tcheck("lv_kind_games", pip_sound_kind(PIP_SND_GAMES) == PIP_SND_GAMES);
    /* One second of games at trim 100, then the trim halves it while it plays. */
    tcheck("lv_sample", pip_sample(pcm, sizeof(pcm)) == 0);
    pip_sleep_ms(100); /* the driver starts a request at its next tick */
    tcheck("lv_trim_100", pip_volume(100) == 100);
    tcheck("lv_trim_50", pip_volume(50) == 100);
    tcheck("lv_trim_bad", pip_volume(101) == -PIP_EINVAL);
    tcheck("lv_stop", pip_audio_stop() == 0);
    tcheck("lv_tone_trim", pip_tone(700, 40) == 0);
    quiet();
    tcheck("lv_recordings", pip_sound_kind(PIP_SND_RECORDINGS) == PIP_SND_GAMES);
    tcheck("lv_tone_rec", pip_tone(701, 40) == 0);
    quiet();
    tcheck("lv_radio_perm", pip_sound_kind(PIP_SND_RADIO) == -PIP_EPERM);
    tcheck("lv_sfx_perm", pip_sound_kind(PIP_SND_SFX) == -PIP_EPERM);
    tcheck("lv_kind_bad", pip_sound_kind(4) == -PIP_EINVAL);
    tcheck("lv_kind_kept", pip_sound_kind(PIP_SND_GAMES) == PIP_SND_RECORDINGS);
    tcheck("lv_set_perm", pip_sound_level(PIP_SND_GAMES, 100) == -PIP_EPERM);
    tcheck("lv_master_perm", pip_sound_level(PIP_LEVEL_MASTER, 100) == -PIP_EPERM);
    tcheck("lv_which_bad", pip_sound_level(5, 50) == -PIP_EINVAL);
    tcheck("lv_store_perm", pip_sound_store() == -PIP_EPERM);
    tcheck("lv_read_master", pip_sound_level(PIP_LEVEL_MASTER, PIP_LEVEL_ASK) == 80);
    tcheck("lv_read_radio", pip_sound_level(PIP_SND_RADIO, PIP_LEVEL_ASK) == 40);
    tcheck("lv_read_sfx", pip_sound_level(PIP_SND_SFX, PIP_LEVEL_ASK) == 100);
    tcheck("lv_read_recordings", pip_sound_level(PIP_SND_RECORDINGS, PIP_LEVEL_ASK) == 50);
    tcheck("lv_read_games", pip_sound_level(PIP_SND_GAMES, PIP_LEVEL_ASK) == 30);
    return failures;
}

extern const char fault_insn[];

int pip_main(const pipos_startup_t *startup)
{
    const char *a = startup->args;

    su = startup;
    if (a[0] == '\0') {
        return 200;
    }
    if (starts(a, "exit")) {
        pip_exit(dec(arg(a)));
    }
    if (starts(a, "ret")) {
        return dec(arg(a));
    }
    if (strncmp(a, "args", 4) == 0) {
        return check_args(a);
    }
    if (starts(a, "info")) {
        return info(hex(arg(a)));
    }
    if (starts(a, "state")) {
        state(dec(arg(a)));
    }
    if (starts(a, "openfile")) {
        return pip_file_open("/CARD.TXT");
    }
    if (starts(a, "rd")) {
        return (int)(*(volatile uint32_t *)(uintptr_t)hex(arg(a)) & 0xffu);
    }
    if (starts(a, "wr")) {
        *(volatile uint32_t *)(uintptr_t)hex(arg(a)) = 0x0BADF00Du;
        return 1;
    }
    if (starts(a, "jmp")) {
        uint32_t to = hex(arg(a));

        say_fault(to);
        ((void (*)(void))(uintptr_t)to)();
        return 1;
    }
    if (starts(a, "loop") || starts(a, "ticks")) {
        int sleep = starts(a, "loop");

        pip_log("@RC stage=testrom ev=tloop\n");
        for (;;) {
            if (sleep) {
                pip_sleep_ms(20);
            } else {
                (void)pip_ticks_ms();
            }
        }
    }
    if (starts(a, "rate")) {
        static uint8_t pcm[375];
        int32_t prev = pip_sample_rate(37500);
        uint32_t i;

        for (i = 0; i < sizeof(pcm); i++) {
            pcm[i] = (uint8_t)(i * 7u + 6u * 31u);
        }
        if (prev < 0 || pip_sample(pcm, sizeof(pcm)) != 0) {
            return 1;
        }
        while (pip_audio_status() == 1) {
            pip_sleep_ms(5);
        }
        return prev / 100;
    }
    if (starts(a, "levels")) {
        return levels();
    }
    if (starts(a, "trim")) {
        return pip_volume(100);
    }
    if (starts(a, "eject")) {
        return pip_holo_eject_event();
    }
    if (starts(a, "undef")) {
        say_fault((uint32_t)(uintptr_t)fault_insn);
        __asm__ volatile(".global fault_insn\nfault_insn: .word 0xe7f000f0");
        return 1;
    }
    return 253;
}
