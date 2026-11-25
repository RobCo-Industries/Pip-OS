/*
 * The saves and files group: system calls 0x50-0x57. See
 * docs/API.md and storage.h for the rules.
 *
 * Saves go through RobCo-Core's atomic whole-file calls. The kernel copies
 * a save through one static 64 KiB buffer, which file_read also uses for
 * its chunks. Handlers never run at the same time, so one buffer is enough.
 * Any FS error, a missing card included, maps through pipos_storage_errno().
 */
#include <robco/fs.h>
#include <robco/klibc.h>

#include "pipos.h"
#include "storage.h"
#include "sys.h"

#define VOL             PIPOS_VOLUME ":"
#define CHUNK           4096u
#define PATH_SIZE       (sizeof(VOL) + PIP_PATH_MAX + 1u)

static uint8_t buf[PIP_SAVE_MAX];
/*
 * One handle table per image level: the ROM's files stay open, and out of
 * reach, while a holotape runs with its own 8 handles.
 */
#define LEVELS          2
static fs_file_t files[LEVELS][PIPOS_STORAGE_HANDLES];
static uint32_t used[LEVELS];
static pip_dirent_t ents[PIP_DIR_LIST_MAX];

unsigned pipos_storage_open_count(void)
{
    return pipos_handle_count(used[0]) + pipos_handle_count(used[1]);
}

/* The handle table of an image of `kind`. */
static unsigned level(int kind)
{
    return kind == PIP_KIND_HOLOTAPE ? 1u : 0u;
}

/* Copies and checks the caller's save name, then builds the save's path. */
static int32_t save_path(const pipos_sys_ctx_t *ctx, uint32_t uname, char *path)
{
    char raw[PIP_SAVE_NAME_MAX + 1];
    char name[PIP_SAVE_NAME_MAX + 1];
    long len = pipos_user_str(ctx, raw, uname, sizeof(raw));

    if (len < 0) {
        return (int32_t)len;
    }
    if (pipos_save_name(raw, name) != 0) {
        return -PIP_EINVAL;
    }
    pipos_save_path(VOL, name, 0, path, PIPOS_SAVE_PATH_MAX);
    return 0;
}

/* The work file path: the save's path with mark before the name. */
static void work_path(const char *path, char mark, char *out)
{
    const char *name = path + sizeof(VOL "/PIPBOY/SAVE/") - 1;
    unsigned i, at = 0;

    for (i = 0; &path[i] != name; i++) {
        out[at++] = path[i];
    }
    out[at++] = mark;
    for (; path[i] != '\0'; i++) {
        out[at++] = path[i];
    }
    out[at] = '\0';
}

/*
 * Reads a save into buf after recovery: its length, or a -PIP_E* code.
 * A file over 64 KiB is not a valid save: -PIP_ERANGE.
 */
static int32_t load_save(const char *path)
{
    size_t len = 0;
    int rc = fs_read_file_atomic(path, buf, sizeof(buf), &len);

    if (rc != FS_OK) {
        return pipos_storage_errno(rc);
    }
    return (int32_t)len;
}

static int32_t sys_read_save(const pipos_sys_ctx_t *ctx, uint32_t uname, uint32_t ubuf,
                             uint32_t size, uint32_t a3)
{
    char path[PIPOS_SAVE_PATH_MAX];
    int32_t rc = save_path(ctx, uname, path);

    (void)a3;
    if (rc == 0 && size > 0) {
        rc = pipos_user_check_out(ctx, ubuf, size);
    }
    if (rc == 0) {
        rc = load_save(path);
    }
    if (rc >= 0) {
        if ((uint32_t)rc > size) {
            rc = (int32_t)size;
        }
        pipos_user_copy_out(ctx, ubuf, buf, (size_t)rc);
    }
    return rc;
}

static int make_dir(const char *path)
{
    int rc = fs_mkdir(path);

    return rc == FS_ERR_EXIST ? FS_OK : rc;
}

static int32_t sys_write_save(const pipos_sys_ctx_t *ctx, uint32_t uname, uint32_t ubuf,
                              uint32_t size, uint32_t a3)
{
    char path[PIPOS_SAVE_PATH_MAX];
    int32_t rc = save_path(ctx, uname, path);
    int frc;

    (void)a3;
    if (rc != 0) {
        return rc;
    }
    if (size > PIP_SAVE_MAX) {
        return -PIP_ERANGE;
    }
    rc = size > 0 ? pipos_user_copy_in(ctx, buf, ubuf, size) : 0;
    if (rc != 0) {
        return rc;
    }
    frc = make_dir(VOL "/PIPBOY");
    if (frc == FS_OK) {
        frc = make_dir(VOL PIPOS_SAVE_DIR);
    }
    if (frc == FS_OK) {
        frc = fs_write_file_atomic(path, buf, size);
    }
    return frc == FS_OK ? (int32_t)size : pipos_storage_errno(frc);
}

static int32_t sys_save_info(const pipos_sys_ctx_t *ctx, uint32_t uname, uint32_t usize,
                             uint32_t a2, uint32_t a3)
{
    char path[PIPOS_SAVE_PATH_MAX];
    int32_t rc = save_path(ctx, uname, path);
    uint32_t size;

    (void)a2, (void)a3;
    if (rc == 0) {
        rc = pipos_user_check_out(ctx, usize, sizeof(size));
    }
    if (rc != 0) {
        return rc;
    }
    /* A read recovers the work files, also on a read-only card where only a read can tell. */
    rc = load_save(path);
    if (rc < 0) {
        return rc;
    }
    size = (uint32_t)rc;
    return pipos_user_copy_out(ctx, usize, &size, sizeof(size));
}

static int unlink_quiet(const char *path)
{
    int rc = fs_unlink(path);

    return rc == FS_ERR_NOENT ? FS_OK : rc;
}

static int32_t sys_save_delete(const pipos_sys_ctx_t *ctx, uint32_t uname, uint32_t a1,
                               uint32_t a2, uint32_t a3)
{
    char path[PIPOS_SAVE_PATH_MAX];
    char work[PIPOS_SAVE_PATH_MAX];
    int32_t rc = save_path(ctx, uname, path);
    int frc;

    (void)a1, (void)a2, (void)a3;
    if (rc != 0) {
        return rc;
    }
    /* Recovery first, so a save that sits only in a work file counts as present. */
    frc = fs_atomic_recover(path);
    if (frc == FS_OK) {
        frc = fs_unlink(path);
    }
    if (frc != FS_OK) {
        return pipos_storage_errno(frc);
    }
    work_path(path, '~', work);
    frc = unlink_quiet(work);
    if (frc == FS_OK) {
        work_path(path, '#', work);
        frc = unlink_quiet(work);
    }
    return frc == FS_OK ? 0 : pipos_storage_errno(frc);
}

/* The directory reader of pipos_save_dir_check(), on fs_readdir(). */
static fs_dir_t rd_dir;
static fs_stat_t rd_st;

static int32_t rd_open(void *self, const char *path)
{
    char full[sizeof(VOL) + 14];
    int frc;

    (void)self;
    k_strncpy(full, VOL, sizeof(full));
    k_strncpy(full + sizeof(VOL) - 1, path, sizeof(full) - sizeof(VOL));
    full[sizeof(full) - 1] = '\0';
    frc = fs_opendir(&rd_dir, full);
    return frc == FS_OK ? 0 : pipos_storage_errno(frc);
}

static int32_t rd_next(void *self, const char **name, const char **short_name, int *is_dir)
{
    int frc = fs_readdir(&rd_dir, &rd_st);

    (void)self;
    if (frc < 0) {
        return pipos_storage_errno(frc);
    }
    *name = rd_st.name;
    *short_name = rd_st.short_name;
    *is_dir = rd_st.is_dir;
    return frc;
}

static void rd_close(void *self)
{
    (void)self;
    fs_closedir(&rd_dir);
}

static const pipos_dir_reader_t reader = { rd_open, rd_next, rd_close, NULL };

/* Copies and checks the caller's path into "sd:<PATH>". */
static int32_t file_path(const pipos_sys_ctx_t *ctx, uint32_t upath, int root_ok, char *path)
{
    char raw[PIP_PATH_MAX + 1];
    long len = pipos_user_str(ctx, raw, upath, sizeof(raw));
    int32_t rc;

    if (len < 0) {
        return (int32_t)len;
    }
    k_strncpy(path, VOL, PATH_SIZE);
    rc = pipos_file_path(raw, root_ok, path + sizeof(VOL) - 1);
    return rc == 0 ? pipos_save_dir_check(path + sizeof(VOL) - 1, &reader) : rc;
}

static int32_t sys_file_open(const pipos_sys_ctx_t *ctx, uint32_t upath, uint32_t a1,
                             uint32_t a2, uint32_t a3)
{
    char path[PATH_SIZE];
    int32_t rc = file_path(ctx, upath, 0, path);
    fs_stat_t st;
    int32_t h;
    int frc;

    (void)a1, (void)a2, (void)a3;
    if (rc != 0) {
        return rc;
    }
    frc = fs_stat(path, &st);
    if (frc != FS_OK) {
        return pipos_storage_errno(frc);
    }
    if (st.is_dir) {
        return -PIP_EINVAL;
    }
    h = pipos_handle_alloc(&used[level(ctx->kind)]);
    if (h < 0) {
        return h;
    }
    frc = fs_open(&files[level(ctx->kind)][h], path, FS_READ);
    if (frc != FS_OK) {
        pipos_handle_free(&used[level(ctx->kind)], h);
        return pipos_storage_errno(frc);
    }
    return h;
}

static int32_t sys_file_read(const pipos_sys_ctx_t *ctx, uint32_t handle, uint32_t ubuf,
                             uint32_t size, uint32_t a3)
{
    int32_t h = (int32_t)handle;
    int32_t rc = pipos_handle_check(used[level(ctx->kind)], h);
    uint32_t done = 0;

    (void)a3;
    if (rc == 0 && size > 0) {
        rc = pipos_user_check_out(ctx, ubuf, size);
    }
    if (rc != 0) {
        return rc;
    }
    if (size > 0x7fffffffu) {
        size = 0x7fffffffu; /* the result must stay positive */
    }
    /* Main-loop work, the holotape slot's included, runs in every file_read. */
    pipos_poll();
    while (done < size) {
        uint32_t want = size - done < CHUNK ? size - done : CHUNK;
        int n = fs_read(&files[level(ctx->kind)][h], buf, want);

        if (n < 0) {
            return done > 0 ? (int32_t)done : pipos_storage_errno(n);
        }
        if (n == 0) {
            break;
        }
        pipos_user_copy_out(ctx, ubuf + done, buf, (size_t)n);
        done += (uint32_t)n;
        if ((uint32_t)n < want) {
            break;
        }
        pipos_poll();
    }
    return (int32_t)done;
}

static int32_t sys_file_close(const pipos_sys_ctx_t *ctx, uint32_t handle, uint32_t a1,
                              uint32_t a2, uint32_t a3)
{
    int32_t h = (int32_t)handle;
    int32_t rc = pipos_handle_check(used[level(ctx->kind)], h);

    (void)a1, (void)a2, (void)a3;
    if (rc != 0) {
        return rc;
    }
    fs_close(&files[level(ctx->kind)][h]); /* a read handle has nothing to write back */
    pipos_handle_free(&used[level(ctx->kind)], h);
    return 0;
}

/* The 8.3 name of an entry: the short name, or the name when it has none. */
static void dirent_fill(pip_dirent_t *e, const fs_stat_t *st)
{
    const char *n = st->short_name[0] != '\0' ? st->short_name : st->name;

    k_memset(e, 0, sizeof(*e));
    k_strncpy(e->name, n, sizeof(e->name) - 1);
    e->flags = st->is_dir ? PIP_DIRENT_DIR : 0;
    e->size = st->is_dir ? 0 : st->size;
}

static int32_t sys_dir_list(const pipos_sys_ctx_t *ctx, uint32_t upath, uint32_t uout,
                            uint32_t max, uint32_t start)
{
    char path[PATH_SIZE];
    int32_t rc;
    fs_stat_t st;
    fs_dir_t d;
    uint32_t index = 0, count = 0;
    int frc;

    if (max < 1 || max > PIP_DIR_LIST_MAX) {
        return -PIP_EINVAL;
    }
    rc = file_path(ctx, upath, 1, path);
    if (rc == 0) {
        rc = pipos_user_check_out(ctx, uout, max * sizeof(pip_dirent_t));
    }
    if (rc != 0) {
        return rc;
    }
    frc = fs_stat(path, &st);
    if (frc != FS_OK) {
        return pipos_storage_errno(frc);
    }
    if (!st.is_dir) {
        return -PIP_EINVAL;
    }
    frc = fs_opendir(&d, path);
    if (frc != FS_OK) {
        return pipos_storage_errno(frc);
    }
    while (count < max && (frc = fs_readdir(&d, &st)) == 1) {
        if (pipos_dir_entry_skipped(st.name, st.attr)) {
            continue;
        }
        if (index++ >= start) {
            dirent_fill(&ents[count++], &st);
        }
    }
    fs_closedir(&d);
    if (frc < 0) {
        return pipos_storage_errno(frc);
    }
    pipos_user_copy_out(ctx, uout, ents, count * sizeof(pip_dirent_t));
    return (int32_t)count;
}

static const pipos_sys_entry_t storage_calls[] = {
    PIPOS_SYS_ENTRY(STORAGE, READ_SAVE, sys_read_save),
    PIPOS_SYS_ENTRY(STORAGE, WRITE_SAVE, sys_write_save),
    PIPOS_SYS_ENTRY(STORAGE, SAVE_INFO, sys_save_info),
    PIPOS_SYS_ENTRY(STORAGE, SAVE_DELETE, sys_save_delete),
    PIPOS_SYS_ENTRY(STORAGE, FILE_OPEN, sys_file_open),
    PIPOS_SYS_ENTRY(STORAGE, FILE_READ, sys_file_read),
    PIPOS_SYS_ENTRY(STORAGE, FILE_CLOSE, sys_file_close),
    PIPOS_SYS_ENTRY(STORAGE, DIR_LIST, sys_dir_list),
};

const pipos_sys_table_t pipos_storage_table = PIPOS_SYS_TABLE(STORAGE, "storage", storage_calls);

static void close_all(unsigned lv)
{
    int32_t h;

    for (h = 0; h < (int32_t)PIPOS_STORAGE_HANDLES; h++) {
        if (pipos_handle_check(used[lv], h) == 0) {
            fs_close(&files[lv][h]);
        }
    }
    used[lv] = 0;
}

void pipos_storage_init(void)
{
    used[0] = used[1] = 0;
}

/* A holotape starts with no files and leaves the ROM's open. */
void pipos_storage_image_start(int kind)
{
    close_all(level(kind));
}

void pipos_storage_image_end(int kind)
{
    close_all(level(kind));
}

void pipos_storage_poll(void)
{
}
