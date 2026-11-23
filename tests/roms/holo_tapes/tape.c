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
    if (starts(a, "undef")) {
        say_fault((uint32_t)(uintptr_t)fault_insn);
        __asm__ volatile(".global fault_insn\nfault_insn: .word 0xe7f000f0");
        return 1;
    }
    return 253;
}
