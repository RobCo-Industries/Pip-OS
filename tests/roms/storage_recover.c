/*
 * Saves left by a power cut in each state of the storage guide's recovery
 * table. test_storage.py puts the work files on the card; OLD and NEW
 * match its contents.
 */
#include "rt.h"

static const char OLD[] = "OLD content of the save\n";
static const char NEW[] = "NEW content, longer than the old one\n";

static char back[128];

static void expect(const char *tag, const char *name, const char *want)
{
    char key[40];
    int32_t n = (int32_t)strlen(want);
    int32_t rc;

    memset(back, 0, sizeof(back));
    rc = pip_read_save(name, back, sizeof(back));
    pip_snprintf(key, sizeof(key), "%s_read", tag);
    rt_check_eq(key, rc, n);
    pip_snprintf(key, sizeof(key), "%s_same", tag);
    rt_check(key, memcmp(back, want, (size_t)n) == 0);
}

int pip_main(const pipos_startup_t *startup)
{
    uint32_t size = 0;

    (void)startup;
    /* A first write cut before its rename: the save stays absent. */
    rt_check_eq("tilde_read", pip_read_save("TILDE", back, sizeof(back)), -PIP_ENOENT);
    rt_check_eq("tilde_info", pip_save_info("TILDE", &size), -PIP_ENOENT);
    expect("hash", "HASH", OLD);
    expect("tgttilde", "TGTTILDE", OLD);
    expect("tgthash", "TGTHASH", NEW);
    expect("both", "BOTH", NEW);
    expect("all", "ALL", NEW);
    pip_save_info("INFOHASH", &size);
    rt_check_eq("infohash_info", (int32_t)size, (int32_t)strlen(OLD));
    rt_check_eq("delboth_delete", pip_save_delete("DELBOTH"), 0);
    rt_check_eq("delboth_gone", pip_read_save("DELBOTH", back, sizeof(back)), -PIP_ENOENT);
    return rt_checks_done();
}
