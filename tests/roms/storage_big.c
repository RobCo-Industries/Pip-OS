/*
 * Saves over 64 KiB that a PC put on the card. test_storage.py writes
 * them as the target, as #NAME.SAV only, and as both work files. None is
 * a valid save: save_info and read_save refuse it and write nothing.
 */
#include "rt.h"

#define SENTINEL 0xA5A5A5A5u

static uint8_t back[64];

static int intact(void)
{
    unsigned i;

    for (i = 0; i < sizeof(back); i++) {
        if (back[i] != 0x5A) {
            return 0;
        }
    }
    return 1;
}

static void key(char *out, const char *tag, const char *what)
{
    pip_snprintf(out, 40, "%s_%s", tag, what);
}

/* save_info and read_save return -PIP_ERANGE and leave the buffers alone. */
static void refused(const char *tag, const char *name)
{
    char k[40];
    uint32_t size = SENTINEL;

    key(k, tag, "info");
    rt_check_eq(k, pip_save_info(name, &size), -PIP_ERANGE);
    key(k, tag, "size_intact");
    rt_check(k, size == SENTINEL);
    memset(back, 0x5A, sizeof(back));
    key(k, tag, "read");
    rt_check_eq(k, pip_read_save(name, back, sizeof(back)), -PIP_ERANGE);
    key(k, tag, "buf_intact");
    rt_check(k, intact());
}

static void deleted(const char *tag, const char *name)
{
    char k[40];
    uint32_t size = SENTINEL;

    refused(tag, name);
    key(k, tag, "delete");
    rt_check_eq(k, pip_save_delete(name), 0);
    key(k, tag, "gone");
    rt_check_eq(k, pip_save_info(name, &size), -PIP_ENOENT);
}

int pip_main(const pipos_startup_t *startup)
{
    uint32_t size = 0;

    (void)startup;
    deleted("target", "BIGT");
    deleted("hash", "BIGH");
    deleted("both", "BIGB");
    refused("over", "BIGW");
    rt_check_eq("over_write", pip_write_save("BIGW", "fresh", 5), 5);
    memset(back, 0, sizeof(back));
    rt_check_eq("over_read", pip_read_save("BIGW", back, sizeof(back)), 5);
    rt_check("over_same", memcmp(back, "fresh", 5) == 0);
    rt_check_eq("over_info", pip_save_info("BIGW", &size), 0);
    rt_check_eq("over_size", (int32_t)size, 5);
    return rt_checks_done();
}
