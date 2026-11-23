/*
 * holo_launch and holo_last_status from the ROM, with the test holotape
 * tests/roms/holo_tapes/tape.c. test_holo.py puts these files in
 * /PIPBOY/HOLO: TAPE.HTP, BADCRC.HTP (a CRC error), TRUNC.HTP (cut short),
 * ROMFILE.HTP (a ROM1 image), DATA.HTP (a data holotape), DIR.HTP (a
 * folder), "Long tape.htp" (alias LONGTA~1.HTP) and "Game.htp" whose
 * 8.3 alias is OTHER.HTP.
 *
 * Then the sandbox: each fault case is one launch the kernel ends; the
 * ROM checks its own memory after each and goes on.
 */
#include "rt.h"

#define KERNEL_ADDR     0x00008000u
#define BACK_BUFFER     0x00C00000u

/* In the ROM slot: the holotape may read these, never write them. */
static const uint32_t rom_const = 0x1234565Au;
static const char rom_str[] = "rom slot string read by the holotape\n";
static volatile uint32_t rom_data = 0xC0FFEE11u;

static char args[128];
static char cmd[64];

static const char *with(const char *fmt, uint32_t v)
{
    pip_snprintf(cmd, sizeof(cmd), fmt, v);
    return cmd;
}

static int32_t launch(const char *name, const char *a)
{
    return pip_holo_launch(name, a);
}

static void args_of(size_t n)
{
    size_t i;

    memcpy(args, "args", 4);
    for (i = 4; i < n; i++) {
        args[i] = (char)('a' + (i - 4) % 26);
    }
    args[n] = '\0';
}

static uint32_t periph_reg(void)
{
    pip_sys_info_t si;

    pip_sys_info(&si);
    return (si.soc == 0x2835 ? 0x20000000u : 0x3F000000u) + 0x3004u;
}

static const char *const bad_names[] = {
    "", "/PIPBOY/HOLO/TAPE.HTP", "HOLO/TAPE.HTP", "/TAPE.HTP", "\\TAPE.HTP", "..", ".",
    "../ROM1.ROM", "..\\TAPE.HTP", "TAPE.HT", "TAPE", "TAPE.", ".HTP", "*.HTP", "TAP?.HTP",
    "sd:TAPE.HTP", "C:TAPE.HTP", "TAPE.HTP ", " TAPE.HTP", "ABCDEFGHI.HTP", "ROM1.ROM",
    "TAPE.HTP.HTP", "Long tape.htp", "TA PE.HTP",
};

int pip_main(const pipos_startup_t *startup)
{
    volatile uint32_t local = 0xFEEDF00Du;
    uint32_t i;
    int bad = 0;

    (void)startup;
    rt_check_eq("last_none", pip_holo_last_status(), -PIP_ENOENT);
    rt_check_eq("eject", pip_holo_eject_event(), 0);

    rt_check_eq("exit0", launch("TAPE.HTP", "exit 0"), 0);
    rt_check_eq("last_exit0", pip_holo_last_status(), 0);
    rt_check_eq("exit1", launch("TAPE.HTP", "exit 1"), 1);
    rt_check_eq("last_exit1", pip_holo_last_status(), 1);
    rt_check_eq("exit255", launch("TAPE.HTP", "exit 255"), 255);
    rt_check_eq("exit256", launch("TAPE.HTP", "exit 256"), 0);
    rt_check_eq("exit_neg", launch("TAPE.HTP", "exit -1"), 255);
    rt_check_eq("ret7", launch("TAPE.HTP", "ret 7"), 7);
    rt_check_eq("ret_lower_name", launch("tape.htp", "ret 9"), 9);
    rt_check_eq("args_empty", launch("TAPE.HTP", ""), 200);
    args_of(99);
    rt_check_eq("args_99", launch("TAPE.HTP", args), 99);
    args_of(100);
    rt_check_eq("args_100", launch("TAPE.HTP", args), -PIP_EINVAL);
    rt_check_eq("last_args_100", pip_holo_last_status(), -PIP_EINVAL);
    rt_check_eq("args_kernel", launch("TAPE.HTP", (const char *)KERNEL_ADDR), -PIP_EFAULT);
    rt_check_eq("args_null", launch("TAPE.HTP", NULL), -PIP_EFAULT);
    rt_check_eq("name_kernel", launch((const char *)KERNEL_ADDR, ""), -PIP_EFAULT);
    rt_check_eq("name_null", launch(NULL, ""), -PIP_EFAULT);
    /* Both strings come in before the name check. */
    rt_check_eq("args_before_check", launch("BAD/NAME", (const char *)KERNEL_ADDR), -PIP_EFAULT);

    rt_check_eq("missing", launch("NOPE.HTP", ""), -PIP_ENOENT);
    rt_check_eq("last_missing", pip_holo_last_status(), -PIP_ENOENT);
    rt_check_eq("bad_crc", launch("BADCRC.HTP", "ret 1"), -PIP_ENOEXEC);
    rt_check_eq("last_bad_crc", pip_holo_last_status(), -PIP_ENOEXEC);
    rt_check_eq("truncated", launch("TRUNC.HTP", "ret 1"), -PIP_ENOEXEC);
    rt_check_eq("rom_magic", launch("ROMFILE.HTP", "ret 1"), -PIP_ENOEXEC);
    rt_check_eq("data_tape", launch("DATA.HTP", "ret 1"), -PIP_ENOEXEC);
    rt_check_eq("folder", launch("DIR.HTP", "ret 1"), -PIP_ENOENT);
    rt_check_eq("long_alias", launch("LONGTA~1.HTP", "ret 6"), 6);
    rt_check_eq("alias_name", launch("OTHER.HTP", "ret 5"), 5);
    rt_check_eq("alias_long_name", launch("GAME.HTP", "ret 5"), -PIP_ENOENT);
    for (i = 0; i < sizeof(bad_names) / sizeof(bad_names[0]); i++) {
        if (launch(bad_names[i], "ret 1") != -PIP_EINVAL) {
            rt_begin("bad_name");
            rt_dec("i", (int32_t)i);
            rt_end();
            bad++;
        }
    }
    rt_check_eq("bad_names", bad, 0);

    rt_check_eq("exit0_again", launch("TAPE.HTP", "exit 0"), 0);
    rt_check_eq("info", launch("TAPE.HTP", with("info %x", (uint32_t)(uintptr_t)rom_str)), 0);
    rt_check_eq("open_file", launch("TAPE.HTP", "openfile"), 0);

    /* The sandbox. */
    rt_check_eq("rd_rom", launch("TAPE.HTP", with("rd %x", (uint32_t)(uintptr_t)&rom_const)),
                0x5A);
    rt_check_eq("wr_rom", launch("TAPE.HTP", with("wr %x", (uint32_t)(uintptr_t)&rom_data)),
                -PIP_EFAULT);
    rt_check_eq("last_kill", pip_holo_last_status(), -PIP_EFAULT);
    rt_check_eq("wr_rom_intact", (int32_t)rom_data, (int32_t)0xC0FFEE11u);
    rt_check_eq("rd_heap", launch("TAPE.HTP", with("rd %x", (uint32_t)(uintptr_t)&local)),
                -PIP_EFAULT);
    rt_check_eq("wr_heap", launch("TAPE.HTP", with("wr %x", (uint32_t)(uintptr_t)&local)),
                -PIP_EFAULT);
    rt_check_eq("wr_heap_intact", (int32_t)local, (int32_t)0xFEEDF00Du);
    rt_check_eq("rd_kernel", launch("TAPE.HTP", with("rd %x", KERNEL_ADDR)), -PIP_EFAULT);
    rt_check_eq("wr_kernel", launch("TAPE.HTP", with("wr %x", KERNEL_ADDR)), -PIP_EFAULT);
    rt_check_eq("rd_backbuf", launch("TAPE.HTP", with("rd %x", BACK_BUFFER)), -PIP_EFAULT);
    rt_check_eq("rd_mmio", launch("TAPE.HTP", with("rd %x", periph_reg())), -PIP_EFAULT);
    rt_check_eq("undef", launch("TAPE.HTP", "undef"), -PIP_EFAULT);
    rt_check_eq("jmp_kernel", launch("TAPE.HTP", with("jmp %x", KERNEL_ADDR)), -PIP_EFAULT);
    rt_check_eq("after_kill", launch("TAPE.HTP", "ret 3"), 3);
    rt_check_eq("rom_intact", (int32_t)(rom_const + rom_data + local),
                (int32_t)(0x1234565Au + 0xC0FFEE11u + 0xFEEDF00Du));
    rt_check_eq("last_after", pip_holo_last_status(), 3);
    rt_checks_done();
    return 0;
}
