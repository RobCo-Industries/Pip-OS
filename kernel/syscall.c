/*
 * The system-call dispatcher: number in r7, arguments in r0 to r3, result
 * in r0. The calls live in the group files kernel/sys_<group>.c; this file
 * builds their index at boot and runs their hooks. See sys.h.
 */
#include <robco/klibc.h>
#include <robco/umode.h>

#include "pipos.h"
#include "sys.h"

typedef struct group {
    const pipos_sys_table_t *table;
    void (*init)(void);
    void (*image_start)(int kind);
    void (*image_end)(int kind);
    void (*poll)(void);
} group_t;

#define GROUP(g)                                                                   \
    { &pipos_##g##_table, pipos_##g##_init, pipos_##g##_image_start,               \
      pipos_##g##_image_end, pipos_##g##_poll }

/* In PIPOS_GROUP_* order. */
static const group_t groups[PIPOS_GROUP_COUNT] = {
    GROUP(system), GROUP(gfx), GROUP(input), GROUP(audio),
    GROUP(time), GROUP(storage), GROUP(holo),
};

static const pipos_user_mem_t umode_mem = { umode_range_ok, umode_strnlen_ok };

static pipos_sys_index_t sys_index;
/* The kind of the running image; a holotape ends back in the ROM. */
static int current_kind = PIP_KIND_ROM;

static void on_svc(exc_frame_t *f, void *ctx)
{
    pipos_sys_ctx_t c = { current_kind, f, &umode_mem };

    (void)ctx;
    /*
     * A slot tape whose card left ends here, at its next call of any kind,
     * before the call runs. umode_request_kill() ends it when this returns,
     * through the same path as a fault; sys_holo.c reports reason=eject.
     */
    if (current_kind == PIP_KIND_HOLOTAPE && pipos_slot_eject_due()) {
        umode_request_kill(PIPOS_KILL_EJECT);
        return;
    }
    f->r[0] = (uint32_t)pipos_sys_dispatch(&sys_index, &c, f->r[7], f->r[0], f->r[1], f->r[2],
                                           f->r[3]);
}

static const char *index_error(int rc)
{
    switch (rc) {
    case PIPOS_SYS_EDUP:   return "syscall listed twice";
    case PIPOS_SYS_EABI:   return "syscall outside the ABI";
    case PIPOS_SYS_EOWNER: return "syscall in the wrong group";
    default:               return "syscall table error";
    }
}

void pipos_sys_init(void)
{
    static char why[64];
    const pipos_sys_table_t *tables[PIPOS_GROUP_COUNT];
    uint32_t served[PIP_SYS_NR_LIMIT / 32];
    uint32_t bad;
    unsigned i;
    int rc;

    for (i = 0; i < PIPOS_GROUP_COUNT; i++) {
        tables[i] = groups[i].table;
    }
    rc = pipos_sys_index_build(&sys_index, tables, PIPOS_GROUP_COUNT, &bad);
    if (rc != PIPOS_SYS_OK) {
        k_snprintf(why, sizeof(why), "%s: 0x%02x", index_error(rc), (unsigned)bad);
        pipos_panic(why);
    }
    for (i = 0; i < PIPOS_GROUP_COUNT; i++) {
        groups[i].init();
    }
    umode_set_svc_handler(on_svc, NULL);
    /* Bit n of the 128-bit hex number: call n has a handler. */
    pipos_sys_served(&sys_index, served);
    k_printf("@RC stage=" PIPOS_STAGE " ev=syscalls served=%08x%08x%08x%08x\n",
             (unsigned)served[3], (unsigned)served[2], (unsigned)served[1],
             (unsigned)served[0]);
}

void pipos_sys_image_start(int kind)
{
    unsigned i;

    current_kind = kind;
    for (i = 0; i < PIPOS_GROUP_COUNT; i++) {
        groups[i].image_start(kind);
    }
}

void pipos_sys_image_end(int kind)
{
    unsigned i;

    for (i = PIPOS_GROUP_COUNT; i-- > 0;) {
        groups[i].image_end(kind);
    }
    current_kind = PIP_KIND_ROM;
}

void pipos_sys_poll(void)
{
    unsigned i;

    for (i = 0; i < PIPOS_GROUP_COUNT; i++) {
        groups[i].poll();
    }
}
