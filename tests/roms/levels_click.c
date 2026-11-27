/*
 * A kernel click under a ROM whose kind is radio. The ROM sets radio,
 * reads its own file and the config, then stays silent for 4 s, so the
 * kernel pays the clicks those reads owe. A click is sfx whatever the
 * ROM's kind: tests/test_audio_levels.py reads its start line.
 */
#include "rt.h"

static uint8_t buf[4096];

static void read_all(const char *path)
{
    int32_t h = pip_file_open(path);

    if (h < 0) {
        return;
    }
    while (pip_file_read(h, buf, sizeof(buf)) > 0) {
    }
    pip_file_close(h);
}

int pip_main(const pipos_startup_t *startup)
{
    int i;

    (void)startup;
    rt_check_eq("click_radio", pip_sound_kind(PIP_SND_RADIO), PIP_SND_SFX);
    for (i = 0; i < 3; i++) {
        read_all("/PIPBOY/ROM1.ROM");
        read_all("/PIPBOY/PIPOS.CFG");
    }
    for (i = 0; i < 40; i++) {
        pip_sleep_ms(100);
    }
    return rt_checks_done();
}
