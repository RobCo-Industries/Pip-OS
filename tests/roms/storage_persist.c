/*
 * Boots twice on one card. The first boot finds no PERSIST save and
 * writes it; the second reads it back.
 */
#include "rt.h"

#define SIZE 3000u

static uint8_t data[SIZE];
static uint8_t back[SIZE + 8];

int pip_main(const pipos_startup_t *startup)
{
    int32_t rc;

    (void)startup;
    for (uint32_t i = 0; i < SIZE; i++) {
        data[i] = (uint8_t)(i * 31u + 42u + (i >> 8));
    }
    rc = pip_read_save("PERSIST", back, sizeof(back));
    if (rc == -PIP_ENOENT) {
        rt_check_eq("first_missing", rc, -PIP_ENOENT);
        rt_check_eq("first_write", pip_write_save("PERSIST", data, SIZE), (int32_t)SIZE);
    } else {
        rt_check_eq("second_read", rc, (int32_t)SIZE);
        rt_check("second_same", memcmp(back, data, SIZE) == 0);
    }
    return rt_checks_done();
}
