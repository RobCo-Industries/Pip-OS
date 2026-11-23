/*
 * The holotapes group: holo_enumerate, holo_launch, holo_last_status and
 * holo_eject_event (0x60-0x63). See docs/pending/holo.md.
 *
 * holo_launch runs the holotape as an inner user task (umode.h depth 2)
 * inside the ROM's system call, through the same pipos_image_run() as
 * the ROM. The pure rules are in holo_rules.c.
 */
#include <robco/fs.h>
#include <robco/klibc.h>
#include <robco/loader.h>
#include <robco/rimg.h>
#include <robco/umode.h>

#include "holo.h"
#include "pipos.h"
#include "rom.h"
#include "storage.h"
#include "sys.h"

#define VOL             PIPOS_VOLUME ":"
#define HOLO_DIR        VOL PIPOS_HOLO_DIR
#define PATH_SIZE       (sizeof(HOLO_DIR "/") + PIPOS_HOLO_NAME_SIZE)

/*
 * The Sandbox table of PIPOS_ABI.md: the holotape's own slot and heap
 * read-write, the ROM slot read-only. umode_run() leaves everything else,
 * the ROM heap included, privileged-only while the holotape runs.
 */
static const umode_region_t holo_regions[] = {
    { PIP_ROM_BASE, PIP_ROM_SIZE, 0 },
    { PIPOS_HOLO_BASE, PIPOS_HOLO_SIZE, UMODE_REGION_WRITE },
    { PIPOS_HOLO_HEAP_BASE, PIPOS_HOLO_HEAP_REGION, UMODE_REGION_WRITE },
};

static int32_t last_status = -PIP_ENOENT;
static int running;

/* holo_enumerate's entries and the header buffer, main context only. */
static pip_holo_info_t infos[PIP_HOLO_ENUM_MAX];
static uint8_t head[PIPOS_HOLO_HEAD];
static fs_file_t file;
static fs_dir_t dir;
static fs_stat_t st;

int pipos_holo_running(void)
{
    return running;
}

static void holo_path(char *path, const char *name)
{
    k_strncpy(path, HOLO_DIR "/", PATH_SIZE);
    k_strncpy(path + sizeof(HOLO_DIR "/") - 1, name, PIPOS_HOLO_NAME_SIZE);
    path[PATH_SIZE - 1] = '\0';
}

/* 1 when the entry in `st` is a file holo_enumerate lists by name, the name in listed. */
static int listed_entry(char *listed)
{
    const char *n = pipos_holo_listed_name(st.name, st.short_name);

    if (st.is_dir || st.leftover || pipos_holo_name(n, listed) != 0) {
        return 0;
    }
    return k_strcmp(listed, n) == 0;
}

/* Reads the first PIPOS_HOLO_HEAD bytes of `name`. 1 when all were read. */
static int read_head(const char *name)
{
    char path[PATH_SIZE];
    size_t got = 0;
    int n = 1;

    holo_path(path, name);
    if (fs_open(&file, path, FS_READ) != FS_OK) {
        return 0;
    }
    while (got < sizeof(head) && n > 0) {
        n = fs_read(&file, head + got, sizeof(head) - got);
        if (n > 0) {
            got += (size_t)n;
        }
    }
    fs_close(&file);
    return got == sizeof(head);
}

static int32_t sys_holo_enumerate(const pipos_sys_ctx_t *ctx, uint32_t out, uint32_t max,
                                  uint32_t a2, uint32_t a3)
{
    char listed[PIPOS_HOLO_NAME_SIZE];
    uint32_t count = 0;
    int32_t rc;
    int frc;

    (void)a2, (void)a3;
    if (max < 1 || max > PIP_HOLO_ENUM_MAX) {
        return -PIP_EINVAL;
    }
    rc = pipos_user_check_out(ctx, out, max * sizeof(pip_holo_info_t));
    if (rc != 0) {
        return rc;
    }
    frc = fs_opendir(&dir, HOLO_DIR);
    if (frc == FS_ERR_NOENT) {
        return 0;
    }
    if (frc != FS_OK) {
        return pipos_storage_errno(frc);
    }
    while (count < max && (frc = fs_readdir(&dir, &st)) == 1) {
        if (!listed_entry(listed) || st.size < PIPOS_HOLO_HEAD) {
            continue;
        }
        if (read_head(listed) && pipos_holo_listed(head, sizeof(head), st.size)) {
            pipos_holo_info_fill(&infos[count++], listed, st.size, head);
        }
        pipos_poll();
    }
    fs_closedir(&dir);
    if (frc < 0) {
        return pipos_storage_errno(frc);
    }
    pipos_user_copy_out(ctx, out, infos, count * sizeof(pip_holo_info_t));
    return (int32_t)count;
}

/*
 * Finds `want` in the holotape folder the way the file system would open
 * it: the first entry whose long name or 8.3 alias matches. That entry
 * must be a file holo_enumerate lists under exactly this name, so an
 * alias cannot reach a folder, a work file, or a file listed under
 * another name. 0, -PIP_ENOENT, or an FS error.
 */
static int32_t find_tape(const char *want)
{
    char listed[PIPOS_HOLO_NAME_SIZE];
    int32_t rc = -PIP_ENOENT;
    int frc = fs_opendir(&dir, HOLO_DIR);

    if (frc == FS_ERR_NOENT) {
        return -PIP_ENOENT;
    }
    if (frc != FS_OK) {
        return pipos_storage_errno(frc);
    }
    while ((frc = fs_readdir(&dir, &st)) == 1) {
        if (pipos_holo_entry_match(want, st.name, st.short_name)) {
            rc = listed_entry(listed) && k_strcmp(listed, want) == 0 ? 0 : -PIP_ENOENT;
            break;
        }
    }
    fs_closedir(&dir);
    return frc < 0 ? pipos_storage_errno(frc) : rc;
}

/* Loads, runs and reports one holotape. Returns holo_launch's result. */
static int32_t run_tape(const char *name, const char *args)
{
    static const loader_slot_t slot = { PIPOS_HOLO_BASE, PIPOS_HOLO_SIZE, NULL };
    static const rimg_expect_t expect = { .magic = RIMG_MAGIC_ROBCO78,
                                          .abi_major = PIP_ABI_VERSION >> 16 };
    static umode_exit_t ex;
    static loader_result_t res;
    static char start[48];
    char path[PATH_SIZE];
    pipos_image_t im;
    int32_t result;
    int rc;

    holo_path(path, name);
    rc = loader_load(path, &slot, &expect, &res);
    if (rc != RIMG_OK) {
        k_printf("@RC stage=" PIPOS_STAGE " ev=holo_error code=%s name=%s\n",
                 rimg_strerror(rc), name);
        return pipos_holo_load_errno(rc);
    }
    pipos_startup_holo((pipos_startup_t *)(uintptr_t)PIPOS_HOLO_STARTUP, args);
    k_snprintf(start, sizeof(start), "ev=holo_start name=%s", name);
    im.kind = PIP_KIND_HOLOTAPE;
    im.entry = PIPOS_HOLO_BASE;
    im.sp = PIPOS_HOLO_STACK_TOP;
    im.startup = PIPOS_HOLO_STARTUP;
    im.stub = PIPOS_HOLO_STUB;
    im.load_size = res.file_size;
    im.regions = holo_regions;
    im.region_count = sizeof(holo_regions) / sizeof(holo_regions[0]);
    im.start_line = start;

    running = 1;
    rc = pipos_image_run(&im, &ex);
    running = 0;
    if (rc != 0) {
        k_printf("@RC stage=" PIPOS_STAGE " ev=error code=umode rc=%d\n", rc);
        return -PIP_ENOEXEC;
    }
    result = pipos_holo_result(ex.reason, ex.code);
    if (result >= 0) {
        k_printf("@RC stage=" PIPOS_STAGE " ev=holo_exit status=%d\n", (int)result);
    } else {
        pipos_image_report_kill("holo", &ex, 0);
    }
    return result;
}

static int32_t launch(const pipos_sys_ctx_t *ctx, uint32_t uname, uint32_t uargs)
{
    char raw[PIPOS_HOLO_NAME_SIZE];
    char name[PIPOS_HOLO_NAME_SIZE];
    char args[PIP_ARGS_SIZE];
    long len;
    int32_t rc;

    /* Both strings come in before anything else happens. */
    len = pipos_user_str(ctx, raw, uname, sizeof(raw));
    if (len < 0) {
        return (int32_t)len;
    }
    len = pipos_user_str(ctx, args, uargs, sizeof(args));
    if (len < 0) {
        return (int32_t)len;
    }
    rc = pipos_holo_name(raw, name);
    if (rc == 0) {
        rc = find_tape(name);
    }
    return rc != 0 ? rc : run_tape(name, args);
}

static int32_t sys_holo_launch(const pipos_sys_ctx_t *ctx, uint32_t uname, uint32_t uargs,
                               uint32_t a2, uint32_t a3)
{
    int32_t rc = pipos_sys_rom_only(ctx);

    (void)a2, (void)a3;
    if (rc != 0) {
        return rc; /* a holotape's refused call launched nothing: last_status stays */
    }
    last_status = launch(ctx, uname, uargs);
    return last_status;
}

static int32_t sys_holo_last_status(const pipos_sys_ctx_t *ctx, uint32_t a0, uint32_t a1,
                                    uint32_t a2, uint32_t a3)
{
    (void)ctx, (void)a0, (void)a1, (void)a2, (void)a3;
    return last_status;
}

static int32_t sys_holo_eject_event(const pipos_sys_ctx_t *ctx, uint32_t a0, uint32_t a1,
                                    uint32_t a2, uint32_t a3)
{
    (void)ctx, (void)a0, (void)a1, (void)a2, (void)a3;
    return 0;
}

static const pipos_sys_entry_t holo_calls[] = {
    PIPOS_SYS_ENTRY(HOLO, HOLO_ENUMERATE, sys_holo_enumerate),
    PIPOS_SYS_ENTRY(HOLO, HOLO_LAUNCH, sys_holo_launch),
    PIPOS_SYS_ENTRY(HOLO, HOLO_LAST_STATUS, sys_holo_last_status),
    PIPOS_SYS_ENTRY(HOLO, HOLO_EJECT_EVENT, sys_holo_eject_event),
};

const pipos_sys_table_t pipos_holo_table = PIPOS_SYS_TABLE(HOLO, "holo", holo_calls);

void pipos_holo_init(void)
{
    last_status = -PIP_ENOENT;
    running = 0;
}

void pipos_holo_image_start(int kind)
{
    (void)kind;
}

void pipos_holo_image_end(int kind)
{
    (void)kind;
}

void pipos_holo_poll(void)
{
}
