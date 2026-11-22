/*
 * Every storage call after the card goes away. The test ejects the card
 * from QEMU once the ROM prints ev=waiting; the ROM probes with a write
 * until the card fails, then expects -PIP_EIO from every call.
 */
#include "rt.h"

static uint8_t buf[64];
static pip_dirent_t ents[4];

int pip_main(const pipos_startup_t *startup)
{
    uint32_t size;
    int32_t rc = 0;
    int i;

    (void)startup;
    rt_check_eq("before", pip_write_save("PROBE", "x", 1), 1);
    rt_check_eq("open_before", pip_file_open("/PIPBOY/ROM1.ROM"), 0);
    rt_begin("waiting");
    rt_end();
    for (i = 0; i < 400; i++) {
        rc = pip_write_save("PROBE", "y", 1);
        if (rc != 1) {
            break;
        }
        pip_sleep_ms(50);
    }
    rt_check_eq("probe", rc, -PIP_EIO);
    rt_begin("probed");
    rt_dec("tries", i);
    rt_dec("ms", pip_ticks_ms());
    rt_end();
    rt_check_eq("write", pip_write_save("OTHER", "z", 1), -PIP_EIO);
    rt_check_eq("read", pip_read_save("PROBE", buf, sizeof(buf)), -PIP_EIO);
    rt_check_eq("info", pip_save_info("PROBE", &size), -PIP_EIO);
    rt_check_eq("delete", pip_save_delete("PROBE"), -PIP_EIO);
    rt_check_eq("open", pip_file_open("/PIPBOY/PIPOS.CFG"), -PIP_EIO);
    rt_check_eq("read_open", pip_file_read(0, buf, sizeof(buf)), -PIP_EIO);
    rt_check_eq("close", pip_file_close(0), 0);
    rt_check_eq("list", pip_dir_list("/PIPBOY", ents, 4, 0), -PIP_EIO);
    rt_begin("end");
    rt_dec("ms", pip_ticks_ms());
    rt_end();
    return rt_checks_done();
}
