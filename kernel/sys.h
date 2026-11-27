/*
 * The system-call groups: what each kernel/sys_<group>.c file exports,
 * the handler type, the table type and the helpers handlers share.
 *
 * syscall.c holds the dispatcher. It looks a number up in an index built
 * at boot from the seven group tables, and returns -PIP_ENOSYS for a
 * number no table lists or an entry with no handler. A number listed
 * twice, or by a group that does not own it, stops the boot.
 *
 * The pure parts (index, ownership, pointer helpers) are in sysdispatch.c
 * and build on the host too.
 */
#ifndef PIPOS_SYS_H
#define PIPOS_SYS_H

#include <stddef.h>
#include <stdint.h>

#include <pipos/abi.h>
#include <robco/exc.h>

/* Groups, in the order of every hook call. */
enum {
    PIPOS_GROUP_SYSTEM,     /* sys_system.c */
    PIPOS_GROUP_GFX,        /* sys_gfx.c */
    PIPOS_GROUP_INPUT,      /* sys_input.c */
    PIPOS_GROUP_AUDIO,      /* sys_audio.c */
    PIPOS_GROUP_TIME,       /* sys_time.c */
    PIPOS_GROUP_STORAGE,    /* sys_storage.c */
    PIPOS_GROUP_HOLO,       /* sys_holo.c */
    PIPOS_GROUP_COUNT
};

/*
 * The group that owns a number, or -1 for a number outside the ABI table.
 * A constant expression, so PIPOS_SYS_ENTRY can check it at build time.
 */
#define PIPOS_SYS_OWNER(nr)                                                        \
    ((nr) >= PIP_SYS_DRAW_LINE && (nr) <= PIP_SYS_SET_FONT ? PIPOS_GROUP_GFX :      \
     (nr) >= PIP_SYS_READ_BUTTONS && (nr) <= PIP_SYS_WAIT_EVENT ? PIPOS_GROUP_INPUT : \
     (nr) >= PIP_SYS_TONE && (nr) <= PIP_SYS_SOUND_STORE ? PIPOS_GROUP_AUDIO :     \
     (nr) == PIP_SYS_READ_SENSOR || (nr) == PIP_SYS_SENSOR_AVAILABLE ||           \
     (nr) == PIP_SYS_GET_TIME || (nr) == PIP_SYS_GET_BATTERY ||                    \
     (nr) == PIP_SYS_POWER || (nr) == PIP_SYS_SET_TIME ? PIPOS_GROUP_TIME :        \
     (nr) >= PIP_SYS_TICKS_MS && (nr) <= PIP_SYS_RAND ? PIPOS_GROUP_SYSTEM :       \
     (nr) >= PIP_SYS_READ_SAVE && (nr) <= PIP_SYS_DIR_LIST ? PIPOS_GROUP_STORAGE : \
     (nr) >= PIP_SYS_HOLO_ENUMERATE && (nr) <= PIP_SYS_HOLO_EJECT_EVENT ? PIPOS_GROUP_HOLO : \
     -1)

/* Reads and writes of the running image's memory: umode.h on the target. */
typedef struct pipos_user_mem {
    int (*range_ok)(uintptr_t ptr, size_t len, int write);
    long (*strnlen_ok)(uintptr_t ptr, size_t max);
} pipos_user_mem_t;

/* What a handler knows about its caller. */
typedef struct pipos_sys_ctx {
    int kind;                       /* PIP_KIND_ROM or PIP_KIND_HOLOTAPE */
    exc_frame_t *frame;             /* the caller's registers, for umode_exit() */
    const pipos_user_mem_t *mem;    /* used by the pipos_user_* helpers */
} pipos_sys_ctx_t;

/* A handler: r0 to r3 in a0 to a3, the result goes to r0. */
typedef int32_t (*pipos_sys_fn)(const pipos_sys_ctx_t *ctx, uint32_t a0, uint32_t a1,
                                uint32_t a2, uint32_t a3);

typedef struct pipos_sys_entry {
    uint32_t nr;                    /* PIP_SYS_* */
    pipos_sys_fn fn;                /* NULL: -PIP_ENOSYS */
} pipos_sys_entry_t;

typedef struct pipos_sys_table {
    int group;                      /* PIPOS_GROUP_* */
    const char *name;
    const pipos_sys_entry_t *entries;
    uint32_t count;
} pipos_sys_table_t;

/*
 * One table entry: PIPOS_SYS_ENTRY(GFX, DRAW_LINE, sys_draw_line).
 * A number the group does not own fails the build with a negative
 * array size.
 */
#define PIPOS_SYS_ENTRY(GROUP, NAME, fn)                                           \
    { PIP_SYS_##NAME + 0 * sizeof(char[PIPOS_SYS_OWNER(PIP_SYS_##NAME) ==          \
                                       PIPOS_GROUP_##GROUP ? 1 : -1]), (fn) }

/* const pipos_sys_table_t pipos_gfx_table = PIPOS_SYS_TABLE(GFX, "gfx", entries); */
#define PIPOS_SYS_TABLE(GROUP, name, entries)                                      \
    { PIPOS_GROUP_##GROUP, (name), (entries), sizeof(entries) / sizeof((entries)[0]) }

/*
 * What each group file exports. Hooks run in PIPOS_GROUP_* order, except
 * image_end, which runs in reverse order:
 *   init         once at boot, after the card and the settings, before the ROM
 *   image_start  before a ROM or holotape runs: reset per-image state
 *   image_end    after it ended (exit, return or fault): release its resources
 *   poll         from pipos_poll() and the main loop
 */
#define PIPOS_SYS_GROUP(g)                                                         \
    extern const pipos_sys_table_t pipos_##g##_table;                              \
    void pipos_##g##_init(void);                                                   \
    void pipos_##g##_image_start(int kind);                                        \
    void pipos_##g##_image_end(int kind);                                          \
    void pipos_##g##_poll(void)

PIPOS_SYS_GROUP(system);
PIPOS_SYS_GROUP(gfx);
PIPOS_SYS_GROUP(input);
PIPOS_SYS_GROUP(audio);
PIPOS_SYS_GROUP(time);
PIPOS_SYS_GROUP(storage);
PIPOS_SYS_GROUP(holo);

/* ---- syscall.c --------------------------------------------------------- */

/* Builds the index (a bad table stops the boot), runs every init, installs the handler. */
void pipos_sys_init(void);
/* Runs every group's image_start or image_end for an image of `kind`. */
void pipos_sys_image_start(int kind);
void pipos_sys_image_end(int kind);
/* Runs every group's poll. */
void pipos_sys_poll(void);

/* ---- sysdispatch.c: pure ----------------------------------------------- */

/* Number -> handler, built from the tables. */
typedef struct pipos_sys_index {
    pipos_sys_fn fn[PIP_SYS_NR_LIMIT];
    int8_t owner[PIP_SYS_NR_LIMIT];     /* table group, -1 when no table lists it */
} pipos_sys_index_t;

/* pipos_sys_index_build() results. */
#define PIPOS_SYS_OK        0
#define PIPOS_SYS_EDUP      1   /* two entries name the same number */
#define PIPOS_SYS_EABI      2   /* a number outside the ABI table */
#define PIPOS_SYS_EOWNER    3   /* a number in a table of another group */

/*
 * Fills *ix from `count` tables. On error returns PIPOS_SYS_E* and stores
 * the number in *bad_nr; the index is then unusable.
 */
int pipos_sys_index_build(pipos_sys_index_t *ix, const pipos_sys_table_t *const *tables,
                          unsigned count, uint32_t *bad_nr);

/* The handler of nr, or NULL. */
pipos_sys_fn pipos_sys_lookup(const pipos_sys_index_t *ix, uint32_t nr);

/* Calls nr's handler, or returns -PIP_ENOSYS. */
int32_t pipos_sys_dispatch(const pipos_sys_index_t *ix, const pipos_sys_ctx_t *ctx, uint32_t nr,
                           uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3);

/* Bit n of the 128-bit mask (word n / 32) is set when number n has a handler. */
void pipos_sys_served(const pipos_sys_index_t *ix, uint32_t mask[PIP_SYS_NR_LIMIT / 32]);

/* ---- sysdispatch.c: helpers for handlers -------------------------------- */

/* 0, or -PIP_EPERM for a holotape. */
int32_t pipos_sys_rom_only(const pipos_sys_ctx_t *ctx);

/* 0 when [ptr, ptr + n) is writable (out) or readable (in) by the caller, else -PIP_EFAULT. */
int32_t pipos_user_check_out(const pipos_sys_ctx_t *ctx, uintptr_t ptr, size_t n);
int32_t pipos_user_check_in(const pipos_sys_ctx_t *ctx, uintptr_t ptr, size_t n);

/* Checks, then copies n bytes to or from the caller. 0 or -PIP_EFAULT. */
int32_t pipos_user_copy_out(const pipos_sys_ctx_t *ctx, uintptr_t ptr, const void *src, size_t n);
int32_t pipos_user_copy_in(const pipos_sys_ctx_t *ctx, void *dst, uintptr_t ptr, size_t n);

/*
 * The length of the caller's string at ptr, which must end with a NUL in
 * its first `size` bytes (so at most size - 1 characters). -PIP_EFAULT
 * when a byte before the NUL is outside the caller's memory, -PIP_EINVAL
 * when all `size` bytes are readable and none is a NUL.
 */
long pipos_user_strlen(const pipos_sys_ctx_t *ctx, uintptr_t ptr, size_t size);

/* pipos_user_strlen(), then copies the string and its NUL into dst[size]. */
long pipos_user_str(const pipos_sys_ctx_t *ctx, char *dst, uintptr_t ptr, size_t size);

#endif
