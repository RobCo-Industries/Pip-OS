/*
 * holo_enumerate: the argument checks, then the whole listing as one
 * "ev=tape" line per entry and "ev=count n=<n>". test_holo.py builds the
 * card and compares the listing.
 */
#include "rt.h"

#define KERNEL_ADDR     0x00008000u
#define ROM_SLOT_END    0x00800000u

static pip_holo_info_t all[PIP_HOLO_ENUM_MAX];
static pip_holo_info_t one[2];

static void show(int32_t i, const pip_holo_info_t *t)
{
    char name[17], title[65], line[200];
    uint32_t sum = 0;
    unsigned k;

    memcpy(name, t->name, 16);
    name[16] = '\0';
    memcpy(title, t->title, 64);
    title[64] = '\0';
    for (k = 0; k < sizeof(t->icon); k++) {
        sum += t->icon[k];
    }
    pip_snprintf(line, sizeof(line),
                 "@RC stage=testrom ev=tape i=%d file=%s sub=%u size=%u ver=0x%08x name=%s "
                 "title=%s icon=0x%08x res=%u\n",
                 (int)i, t->file, (unsigned)t->subtype, (unsigned)t->size,
                 (unsigned)t->image_version, name, title, (unsigned)sum,
                 (unsigned)t->reserved);
    pip_log(line);
}

int pip_main(const pipos_startup_t *startup)
{
    uint8_t *edge = (uint8_t *)(uintptr_t)(ROM_SLOT_END - 100u);
    int32_t n, i, rc;
    int intact = 1;

    (void)startup;
    rt_check_eq("max0", pip_holo_enumerate(all, 0), -PIP_EINVAL);
    rt_check_eq("max33", pip_holo_enumerate(all, 33), -PIP_EINVAL);
    rt_check_eq("out_kernel", pip_holo_enumerate((pip_holo_info_t *)KERNEL_ADDR, 1),
                -PIP_EFAULT);
    rt_check_eq("out_null", pip_holo_enumerate(NULL, 1), -PIP_EFAULT);
    memset(edge, 0x5A, 100);
    rt_check_eq("out_straddle", pip_holo_enumerate((pip_holo_info_t *)edge, 1), -PIP_EFAULT);
    for (i = 0; i < 100; i++) {
        intact &= edge[i] == 0x5A;
    }
    rt_check("straddle_intact", intact);

    n = pip_holo_enumerate(all, PIP_HOLO_ENUM_MAX);
    for (i = 0; i < n; i++) {
        show(i, &all[i]);
    }
    rt_begin("count");
    rt_dec("n", n);
    rt_end();

    memset(one, 0xA5, sizeof(one));
    rc = pip_holo_enumerate(one, 1);
    rt_check_eq("max1", rc, n > 0 ? 1 : 0);
    rt_check("max1_first", n <= 0 || memcmp(&one[0], &all[0], sizeof(one[0])) == 0);
    intact = 1;
    for (i = 0; i < (int32_t)sizeof(one[1]); i++) {
        intact &= ((uint8_t *)&one[1])[i] == 0xA5;
    }
    rt_check("max1_untouched", intact);
    rt_check("max32", n >= 0 && n <= PIP_HOLO_ENUM_MAX);
    rt_checks_done();
    return 0;
}
