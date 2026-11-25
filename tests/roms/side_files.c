/*
 * What a ROM sees of a card, for tests/test_side_files.py: the holotape
 * listing as one "ev=tape" line per entry, then one "ev=ent" line per
 * dir_list entry of /, /PIPBOY, /PIPBOY/HOLO and /PIPBOY/DATA, then the
 * size of the save GAME. The test boots a clean card and the same card
 * with macOS side files, and compares the lines.
 */
#include "rt.h"

static pip_holo_info_t tapes[PIP_HOLO_ENUM_MAX];
static pip_dirent_t ents[PIP_DIR_LIST_MAX];

static void list(const char *dir)
{
    char line[120];
    int32_t n = pip_dir_list(dir, ents, PIP_DIR_LIST_MAX, 0);
    int32_t i;

    for (i = 0; i < n; i++) {
        pip_snprintf(line, sizeof(line),
                     "@RC stage=testrom ev=ent dir=%s name=%s flags=%u size=%u\n", dir,
                     ents[i].name, (unsigned)ents[i].flags, (unsigned)ents[i].size);
        pip_log(line);
    }
    pip_snprintf(line, sizeof(line), "@RC stage=testrom ev=dir dir=%s n=%d\n", dir, (int)n);
    pip_log(line);
}

int pip_main(const pipos_startup_t *startup)
{
    char line[80];
    uint32_t size = 0;
    int32_t n, i;

    (void)startup;
    n = pip_holo_enumerate(tapes, PIP_HOLO_ENUM_MAX);
    for (i = 0; i < n; i++) {
        pip_snprintf(line, sizeof(line), "@RC stage=testrom ev=tape file=%s size=%u\n",
                     tapes[i].file, (unsigned)tapes[i].size);
        pip_log(line);
    }
    rt_begin("tapes");
    rt_dec("n", n);
    rt_end();
    list("/");
    list("/PIPBOY");
    list("/PIPBOY/HOLO");
    list("/PIPBOY/DATA");
    rt_begin("save");
    rt_dec("rc", pip_save_info("GAME", &size));
    rt_dec("size", (int32_t)size);
    rt_end();
    return 0;
}
