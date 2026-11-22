/*
 * File and directory calls on the card test_storage.py builds: chunked
 * reads, the handle table, dir_list paging, hostile paths and bad
 * pointers. It exits with three files open.
 */
#include "rt.h"

#define SLOT_END    (PIP_ROM_BASE + PIP_ROM_SIZE)
#define KERNEL_ADDR 0x00008000u
#define PERIPH      0x20000000u
#define BIG         10000u
#define OK          1000        /* "succeeded" in the hostile table */

static uint8_t want[BIG];
static uint8_t back[BIG + 64];
static pip_dirent_t ents[PIP_DIR_LIST_MAX + 1];
static char path[160];

static int intact(volatile uint8_t *p, unsigned n)
{
    for (unsigned i = 0; i < n; i++) {
        if (p[i] != 0x5a) {
            return 0;
        }
    }
    return 1;
}

static void big_file(void)
{
    int32_t h;
    uint32_t at = 0;
    static const char *const keys[] = { "big_c1", "big_c2", "big_c3", "big_c4" };
    static const int32_t sizes[] = { 3000, 3000, 3000, 1000 };

    for (uint32_t i = 0; i < BIG; i++) {
        want[i] = (uint8_t)(i * 7u + (i >> 9) + 3u);
    }
    memset(back, 0xee, sizeof(back));
    h = pip_file_open("/DATA/BIG.BIN");
    rt_check_eq("big_open", h, 0);
    for (int i = 0; i < 4; i++) {
        int32_t n = pip_file_read(h, back + at, 3000);

        rt_check_eq(keys[i], n, sizes[i]);
        at += n > 0 ? (uint32_t)n : 0;
    }
    rt_check_eq("big_end", pip_file_read(h, back + at, 3000), 0);
    rt_check_eq("big_end_again", pip_file_read(h, back + at, 1), 0);
    rt_check("big_same", memcmp(back, want, BIG) == 0 && back[BIG] == 0xee);
    rt_check_eq("big_zero", pip_file_read(h, NULL, 0), 0);
    rt_check_eq("big_close", pip_file_close(h), 0);

    memset(back, 0xee, sizeof(back));
    h = pip_file_open("/data/big.bin");
    rt_check_eq("whole_open", h, 0);
    rt_check_eq("whole_read", pip_file_read(h, back, sizeof(back)), BIG);
    rt_check("whole_same", memcmp(back, want, BIG) == 0 && back[BIG] == 0xee);
    rt_check_eq("whole_close", pip_file_close(h), 0);

    h = pip_file_open("/Data/Small.Txt");
    rt_check_eq("small_open", h, 0);
    memset(back, 0, sizeof(back));
    rt_check_eq("small_read", pip_file_read(h, back, 100), 13);
    rt_check("small_same", memcmp(back, "hello pip-os\n", 14) == 0);
    rt_check_eq("small_close", pip_file_close(h), 0);

    rt_check_eq("missing", pip_file_open("/DATA/NOPE.TXT"), -PIP_ENOENT);
    rt_check_eq("missing_dir", pip_file_open("/NODIR/X.TXT"), -PIP_ENOENT);
    rt_check_eq("open_dir", pip_file_open("/DATA/SUB"), -PIP_EINVAL);
    rt_check_eq("open_root", pip_file_open("/"), -PIP_EINVAL);
}

static void pointers(void)
{
    volatile uint8_t *edge = (volatile uint8_t *)(uintptr_t)(SLOT_END - 8);
    char *sedge = (char *)(uintptr_t)(SLOT_END - 4);
    int32_t h = pip_file_open("/DATA/BIG.BIN");

    rt_check_eq("ptr_open", h, 0);
    rt_check_eq("rd_kernel", pip_file_read(h, (void *)KERNEL_ADDR, 16), -PIP_EFAULT);
    rt_check_eq("rd_periph", pip_file_read(h, (void *)PERIPH, 16), -PIP_EFAULT);
    memset((void *)edge, 0x5a, 8);
    rt_check_eq("rd_cross", pip_file_read(h, (void *)edge, 16), -PIP_EFAULT);
    rt_check("rd_intact", intact(edge, 8));
    /* The refused reads did not move the position. */
    rt_check("rd_pos", pip_file_read(h, back, 4) == 4 && memcmp(back, want, 4) == 0);
    rt_check_eq("ptr_close", pip_file_close(h), 0);

    rt_check_eq("path_kernel", pip_file_open((const char *)KERNEL_ADDR), -PIP_EFAULT);
    rt_check_eq("path_null", pip_file_open(NULL), -PIP_EFAULT);
    memcpy(sedge, "/ABC", 4);
    rt_check_eq("path_cross", pip_dir_list(sedge, ents, 1, 0), -PIP_EFAULT);
    memset(path, 'A', sizeof(path));
    path[0] = '/';
    rt_check_eq("path_nonul", pip_file_open(path), -PIP_EINVAL);
    /* 127 bytes: 14 components "/ABCDEFGH" (126) + "/" + ... too long; build 127 from parts. */
    path[0] = '\0';
    for (int i = 0; i < 12; i++) {
        strlcpy(path + strlen(path), "/ABCDEFGH", sizeof(path) - strlen(path));
    }
    strlcpy(path + strlen(path), "/ABCDEFGH.TXT/ABCDE", sizeof(path) - strlen(path));
    rt_check_eq("path_127", strlen(path) == 127 ? pip_file_open(path) : -1, -PIP_ENOENT);
    strlcpy(path + strlen(path), "F", sizeof(path) - strlen(path));
    rt_check_eq("path_128", pip_file_open(path), -PIP_EINVAL);
}

static void handles(void)
{
    int ok = 1;
    int32_t h;

    for (int32_t i = 0; i < 8; i++) {
        ok &= pip_file_open("/DATA/SMALL.TXT") == i;
    }
    rt_check("handles_8", ok);
    rt_check_eq("handle_9th", pip_file_open("/DATA/SMALL.TXT"), -PIP_ENOMEM);
    rt_check_eq("close_3", pip_file_close(3), 0);
    rt_check_eq("reuse_3", pip_file_open("/DATA/BIG.BIN"), 3);
    ok = 1;
    for (h = 0; h < 8; h++) {
        ok &= pip_file_close(h) == 0;
    }
    rt_check("close_all", ok);
    rt_check_eq("close_again", pip_file_close(3), -PIP_EBADF);
    rt_check_eq("read_closed", pip_file_read(0, back, 4), -PIP_EBADF);
    rt_check_eq("bad_m1", pip_file_read(-1, back, 4), -PIP_EBADF);
    rt_check_eq("bad_8", pip_file_close(8), -PIP_EBADF);
    rt_check_eq("bad_big", pip_file_read(0x7fffffff, back, 4), -PIP_EBADF);
}

static void report(const char *tag, int32_t n)
{
    for (int32_t i = 0; i < n; i++) {
        pip_snprintf(path, sizeof(path),
                     "@RC stage=testrom ev=dirent tag=%s name=%s flags=%u size=%u\n", tag,
                     ents[i].name, (unsigned)ents[i].flags, (unsigned)ents[i].size);
        pip_log(path);
    }
}

static void listing(void)
{
    volatile uint8_t *edge = (volatile uint8_t *)(uintptr_t)(SLOT_END - 8);
    int32_t n;

    rt_check_eq("ls_max0", pip_dir_list("/", ents, 0, 0), -PIP_EINVAL);
    rt_check_eq("ls_max65", pip_dir_list("/", ents, 65, 0), -PIP_EINVAL);
    rt_check_eq("ls_file", pip_dir_list("/DATA/BIG.BIN", ents, 4, 0), -PIP_EINVAL);
    rt_check_eq("ls_missing", pip_dir_list("/NODIR", ents, 4, 0), -PIP_ENOENT);
    rt_check_eq("ls_kernel", pip_dir_list("/", (pip_dirent_t *)KERNEL_ADDR, 1, 0), -PIP_EFAULT);
    memset((void *)edge, 0x5a, 8);
    /* One entry fits before the end, max 2 does not: nothing is written. */
    rt_check_eq("ls_cross", pip_dir_list("/", (pip_dirent_t *)(SLOT_END - 28), 2, 0),
                -PIP_EFAULT);
    rt_check("ls_intact", intact(edge, 8));

    n = pip_dir_list("/", ents, PIP_DIR_LIST_MAX, 0);
    rt_check_eq("ls_root", n, 4);
    report("root", n);
    n = pip_dir_list("/data", ents, PIP_DIR_LIST_MAX, 0);
    rt_check_eq("ls_data", n, 3);
    report("data", n);
    memset(ents, 0x5a, sizeof(ents));
    n = pip_dir_list("/MANY", ents, PIP_DIR_LIST_MAX, 0);
    rt_check("ls_many_p0", n == 64 && ((uint8_t *)&ents[64])[0] == 0x5a);
    report("many0", n);
    n = pip_dir_list("/MANY", ents, PIP_DIR_LIST_MAX, 64);
    rt_check_eq("ls_many_p1", n, 7);
    report("many1", n);
    rt_check_eq("ls_many_end", pip_dir_list("/MANY", ents, 5, 71), 0);
    rt_check_eq("ls_many_far", pip_dir_list("/MANY", ents, 5, 0xffffffffu), 0);
    n = pip_dir_list("/MANY", ents, 3, 10);
    rt_check_eq("ls_many_small", n, 3);
    report("small", n);
}

static const struct {
    const char *path;
    int list;           /* 1: dir_list, 0: file_open */
    int32_t rc;
} hostile[] = {
    { "/", 1, OK },
    { "/PIPBOY/ROM1.ROM", 0, OK },
    { "/pipboy/rom1.rom", 0, OK },
    { "/Mixed/x.txt", 0, OK },
    { "/PIPBOY", 1, OK },
    { "/pipboy", 1, OK },
    { "/PIPBOY/SAVEX", 0, -PIP_ENOENT },
    { "/PIPBOY/SAVE.X", 0, -PIP_ENOENT },
    { "/ABCDEFGH.ABC", 0, -PIP_ENOENT },
    { "/!#$%&'()/-@^_`{}~.X", 0, -PIP_ENOENT },
    { "/ABCDEFGHI", 0, -PIP_EINVAL },
    { "/ABCDEFGH.ABCD", 0, -PIP_EINVAL },
    { "/ABC.", 0, -PIP_EINVAL },
    { "/.ABC", 0, -PIP_EINVAL },
    { "/A.B.C", 0, -PIP_EINVAL },
    { "", 1, -PIP_EINVAL },
    { "PIPBOY/ROM1.ROM", 0, -PIP_EINVAL },
    { "//", 1, -PIP_EINVAL },
    { "//PIPBOY", 1, -PIP_EINVAL },
    { "/PIPBOY//SAVE", 1, -PIP_EINVAL },
    { "/PIPBOY/", 1, -PIP_EINVAL },
    { "/DATA/", 1, -PIP_EINVAL },
    { "/./PIPBOY", 1, -PIP_EINVAL },
    { "/PIPBOY/.", 1, -PIP_EINVAL },
    { "/PIPBOY/..", 1, -PIP_EINVAL },
    { "/PIPBOY/../PIPBOY/SAVE", 1, -PIP_EINVAL },
    { "/DATA/../PIPBOY/SAVE/A.SAV", 0, -PIP_EINVAL },
    { "/PIPBOY./SAVE", 1, -PIP_EINVAL },
    { "/PIPBOY/SAVE.", 1, -PIP_EINVAL },
    { "/PIPBOY/SAVE ", 1, -PIP_EINVAL },
    { "/PIPBOY /SAVE", 1, -PIP_EINVAL },
    { "/PIPBOY/SAVE/A.SAV.", 0, -PIP_EINVAL },
    { "/PIPBOY/SAVE/A.SAV ", 0, -PIP_EINVAL },
    { "/PIP BOY", 1, -PIP_EINVAL },
    { "/PIPBOY\\SAVE", 1, -PIP_EINVAL },
    { "\\PIPBOY\\SAVE", 1, -PIP_EINVAL },
    { "sd:/PIPBOY/SAVE", 1, -PIP_EINVAL },
    { "/sd:/X", 1, -PIP_EINVAL },
    { "/PIPBOY/SAVE/*.SAV", 0, -PIP_EINVAL },
    { "/PIPBOY/SAVE/?.SAV", 0, -PIP_EINVAL },
    { "/A+B", 0, -PIP_EINVAL },
    { "/A,B", 0, -PIP_EINVAL },
    { "/A;B", 0, -PIP_EINVAL },
    { "/A=B", 0, -PIP_EINVAL },
    { "/A[B]", 0, -PIP_EINVAL },
    { "/A\"B", 0, -PIP_EINVAL },
    { "/A<B>", 0, -PIP_EINVAL },
    { "/A|B", 0, -PIP_EINVAL },
    { "/\xc3\xa9", 0, -PIP_EINVAL },
    { "/A\xe5", 0, -PIP_EINVAL },
    { "/A\x01", 0, -PIP_EINVAL },
    { "/A\x7f", 0, -PIP_EINVAL },
    { "/PIPBOY/SAVE/~A.SAV/", 0, -PIP_EINVAL },
    { "/PIPBOY/SAVE", 1, -PIP_EPERM },
    { "/pipboy/save", 1, -PIP_EPERM },
    { "/PipBoy/Save", 1, -PIP_EPERM },
    { "/PIPBOY/SAVE", 0, -PIP_EPERM },
    { "/PIPBOY/SAVE/A.SAV", 0, -PIP_EPERM },
    { "/pipboy/save/a.sav", 0, -PIP_EPERM },
    { "/PIPBOY/SAVE/~A.SAV", 0, -PIP_EPERM },
    { "/PIPBOY/SAVE/#A.SAV", 0, -PIP_EPERM },
    { "/PIPBOY/SAVE/X/Y", 1, -PIP_EPERM },
    { "/PIPBOY/SAVE/~ABCDE~1.SAV", 0, -PIP_EPERM },
};

static void hostile_paths(void)
{
    int all = 1;

    for (unsigned i = 0; i < sizeof(hostile) / sizeof(hostile[0]); i++) {
        int32_t rc;

        if (hostile[i].list) {
            rc = pip_dir_list(hostile[i].path, ents, 4, 0);
        } else {
            rc = pip_file_open(hostile[i].path);
            if (rc >= 0) {
                pip_file_close(rc);
            }
        }
        if (rc >= 0) {
            rc = OK;
        }
        all &= rc == hostile[i].rc;
        rt_begin("hostile");
        rt_dec("i", (int32_t)i);
        rt_dec("got", rc);
        rt_dec("expected", hostile[i].rc);
        rt_end();
    }
    rt_check("hostile", all);
}

int pip_main(const pipos_startup_t *startup)
{
    (void)startup;
    big_file();
    pointers();
    handles();
    listing();
    hostile_paths();
    rt_check("left_open", pip_file_open("/DATA/BIG.BIN") == 0 &&
                          pip_file_open("/DATA/SMALL.TXT") == 1 &&
                          pip_file_open("/PIPBOY/ROM1.ROM") == 2);
    return rt_checks_done();
}
