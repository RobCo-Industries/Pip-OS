/*
 * kernel/storage_rules.c: save names, save paths, file paths with hostile
 * spellings, the FS error mapping and the handle table.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include <pipos/abi.h>
#include <robco/fs.h>

#include "rc_test.h"
#include "storage.h"

TEST(save_names)
{
    static const struct {
        const char *in, *out;
    } good[] = {
        { "A", "A" },           { "SAVE1", "SAVE1" },     { "slot_9", "SLOT_9" },
        { "MiXeD", "MIXED" },   { "ABCDEFGH", "ABCDEFGH" }, { "_", "_" },
        { "0", "0" },           { "zzzzzzzz", "ZZZZZZZZ" },
    };
    static const char *const bad[] = {
        "", "ABCDEFGHI", "A.SAV", "A B", "A-B", "A/B", "..", ".", "~A", "#A", "A\\B",
        "\xc3\xa9", "A\x7f", "A\x01", "a:b", "ab*", " ", "A ", "abcdefghi",
    };
    char out[PIP_SAVE_NAME_MAX + 1];
    unsigned i;

    for (i = 0; i < sizeof(good) / sizeof(good[0]); i++) {
        CHECK_EQ(pipos_save_name(good[i].in, out), 0);
        CHECK_STR(out, good[i].out);
    }
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        CHECK_EQ(pipos_save_name(bad[i], out), -PIP_EINVAL);
    }
}

TEST(save_paths)
{
    char p[PIPOS_SAVE_PATH_MAX];

    pipos_save_path("sd:", "SAVE1", 0, p, sizeof(p));
    CHECK_STR(p, "sd:/PIPBOY/SAVE/SAVE1.SAV");
    pipos_save_path("sd:", "ABCDEFGH", '~', p, sizeof(p));
    CHECK_STR(p, "sd:/PIPBOY/SAVE/~ABCDEFGH.SAV");
    pipos_save_path("", "A", '#', p, sizeof(p));
    CHECK_STR(p, "/PIPBOY/SAVE/#A.SAV");
    pipos_save_path("sd:", "ABCDEFGH", '#', p, 12);
    CHECK_STR(p, "sd:/PIPBOY/");
}

static const char *repeat(char *buf, const char *unit, unsigned times, const char *tail)
{
    buf[0] = '\0';
    while (times-- > 0) {
        strcat(buf, unit);
    }
    strcat(buf, tail);
    return buf;
}

TEST(file_paths)
{
    static const struct {
        const char *in;
        int root_ok;
        int32_t rc;
        const char *out;
    } t[] = {
        { "/", 1, 0, "/" },
        { "/", 0, -PIP_EINVAL, NULL },
        { "/PIPBOY/ROM1.ROM", 0, 0, "/PIPBOY/ROM1.ROM" },
        { "/pipboy/rom1.rom", 0, 0, "/PIPBOY/ROM1.ROM" },
        { "/A", 0, 0, "/A" },
        { "/ABCDEFGH.ABC", 0, 0, "/ABCDEFGH.ABC" },
        { "/ABCDEFGH", 0, 0, "/ABCDEFGH" },
        { "/A.B", 0, 0, "/A.B" },
        { "/!#$%&'()/-@^_`{}~.X", 0, 0, "/!#$%&'()/-@^_`{}~.X" },
        { "/PIPBOY", 1, 0, "/PIPBOY" },
        { "/PIPBOY/HOLO", 1, 0, "/PIPBOY/HOLO" },
        { "/PIPBOY/SAVEX", 0, 0, "/PIPBOY/SAVEX" },
        { "/PIPBOY/SAVE.X", 0, 0, "/PIPBOY/SAVE.X" },
        { "/SAVE/PIPBOY", 0, 0, "/SAVE/PIPBOY" },
        { "/X/PIPBOY/SAVE", 0, 0, "/X/PIPBOY/SAVE" },
        /* 8.3 edges */
        { "/ABCDEFGHI", 0, -PIP_EINVAL, NULL },
        { "/ABCDEFGH.ABCD", 0, -PIP_EINVAL, NULL },
        { "/ABC.", 0, -PIP_EINVAL, NULL },
        { "/.ABC", 0, -PIP_EINVAL, NULL },
        { "/A.B.C", 0, -PIP_EINVAL, NULL },
        /* hostile spellings */
        { "", 1, -PIP_EINVAL, NULL },
        { "PIPBOY/ROM1.ROM", 0, -PIP_EINVAL, NULL },
        { "//", 1, -PIP_EINVAL, NULL },
        { "//PIPBOY", 1, -PIP_EINVAL, NULL },
        { "/PIPBOY//SAVE", 1, -PIP_EINVAL, NULL },
        { "/PIPBOY/", 1, -PIP_EINVAL, NULL },
        { "/./PIPBOY", 1, -PIP_EINVAL, NULL },
        { "/PIPBOY/.", 1, -PIP_EINVAL, NULL },
        { "/PIPBOY/..", 1, -PIP_EINVAL, NULL },
        { "/PIPBOY/../PIPBOY/SAVE", 1, -PIP_EINVAL, NULL },
        { "/X/../PIPBOY/SAVE/A.SAV", 0, -PIP_EINVAL, NULL },
        { "/PIPBOY./SAVE", 1, -PIP_EINVAL, NULL },
        { "/PIPBOY/SAVE.", 1, -PIP_EINVAL, NULL },
        { "/PIPBOY/SAVE ", 1, -PIP_EINVAL, NULL },
        { "/PIPBOY /SAVE", 1, -PIP_EINVAL, NULL },
        { "/PIP BOY", 1, -PIP_EINVAL, NULL },
        { "/PIPBOY\\SAVE", 1, -PIP_EINVAL, NULL },
        { "\\PIPBOY\\SAVE", 1, -PIP_EINVAL, NULL },
        { "sd:/PIPBOY/SAVE", 1, -PIP_EINVAL, NULL },
        { "/sd:/X", 1, -PIP_EINVAL, NULL },
        { "/PIPBOY/SAVE/*.SAV", 0, -PIP_EINVAL, NULL },
        { "/A?B", 0, -PIP_EINVAL, NULL },
        { "/A+B", 0, -PIP_EINVAL, NULL },
        { "/A,B", 0, -PIP_EINVAL, NULL },
        { "/A;B", 0, -PIP_EINVAL, NULL },
        { "/A=B", 0, -PIP_EINVAL, NULL },
        { "/A[B]", 0, -PIP_EINVAL, NULL },
        { "/A\"B", 0, -PIP_EINVAL, NULL },
        { "/A<B>", 0, -PIP_EINVAL, NULL },
        { "/A|B", 0, -PIP_EINVAL, NULL },
        { "/\xc3\xa9", 0, -PIP_EINVAL, NULL },
        { "/A\xe5", 0, -PIP_EINVAL, NULL },
        { "/A\x01", 0, -PIP_EINVAL, NULL },
        { "/A\x7f", 0, -PIP_EINVAL, NULL },
        { "/PIPBOY/SAVE/~A.SAV/", 0, -PIP_EINVAL, NULL },
        /* the save folder in every spelling that passes the syntax */
        { "/PIPBOY/SAVE", 1, -PIP_EPERM, NULL },
        { "/pipboy/save", 1, -PIP_EPERM, NULL },
        { "/PipBoy/Save", 1, -PIP_EPERM, NULL },
        { "/PIPBOY/SAVE/A.SAV", 0, -PIP_EPERM, NULL },
        { "/PIPBOY/SAVE/~A.SAV", 0, -PIP_EPERM, NULL },
        { "/PIPBOY/SAVE/#A.SAV", 0, -PIP_EPERM, NULL },
        { "/pipboy/save/slot1.sav", 0, -PIP_EPERM, NULL },
        { "/PIPBOY/SAVE/X/Y", 0, -PIP_EPERM, NULL },
        { "/PIPBOY/SAVE/~ABCDE~1.SAV", 0, -PIP_EPERM, NULL },
    };
    char out[PIP_PATH_MAX + 1];
    char in[512];
    unsigned i;

    for (i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
        int32_t rc = pipos_file_path(t[i].in, t[i].root_ok, out);

        if (rc != t[i].rc) {
            fprintf(stderr, "path \"%s\": got %d expected %d\n", t[i].in, rc, t[i].rc);
        }
        CHECK_EQ(rc, t[i].rc);
        if (t[i].out != NULL) {
            CHECK_STR(out, t[i].out);
        }
    }
    /* 127 bytes passes, 128 does not. */
    repeat(in, "/ABCDEFGH", 12, "/ABCDEFGH.TXT/ABCDE");
    CHECK_EQ(strlen(in), 127);
    CHECK_EQ(pipos_file_path(in, 0, out), 0);
    CHECK_STR(out, in);
    repeat(in, "/ABCDEFGH", 12, "/ABCDEFGH.TXT/ABCDEF");
    CHECK_EQ(strlen(in), 128);
    CHECK_EQ(pipos_file_path(in, 0, out), -PIP_EINVAL);
    repeat(in, "/A", 200, "");
    CHECK_EQ(pipos_file_path(in, 1, out), -PIP_EINVAL);
}

/* A fake card: entries are "dir|name|short_name|is_dir". */
typedef struct {
    const char *const *ents;
    char dir[16];
    unsigned at;
    int opens, open_now;
    int32_t fail;
} fake_t;

static int32_t fake_next(void *self, const char **name, const char **short_name, int *is_dir);

static int32_t fake_open(void *self, const char *path)
{
    fake_t *f = self;

    CHECK(!f->open_now);
    if (f->fail != 0) {
        return f->fail;
    }
    /* "/X" resolves like FatFs: the root entry whose long or short name is X. */
    snprintf(f->dir, sizeof(f->dir), "%s", path);
    if (strcmp(path, "/") != 0) {
        const char *n, *sn;
        int d;

        snprintf(f->dir, sizeof(f->dir), "/");
        f->at = 0;
        while (fake_next(f, &n, &sn, &d) == 1) {
            if (strcasecmp(n, path + 1) == 0 || strcasecmp(sn, path + 1) == 0) {
                snprintf(f->dir, sizeof(f->dir), "/%s", n);
                break;
            }
        }
    }
    f->at = 0;
    f->opens++;
    f->open_now = 1;
    return 0;
}

static int32_t fake_next(void *self, const char **name, const char **short_name, int *is_dir)
{
    static char n[64], sn[16];
    fake_t *f = self;

    for (; f->ents[f->at] != NULL; f->at++) {
        const char *e = f->ents[f->at];
        size_t dl = strlen(f->dir);
        int d;

        if (strncmp(e, f->dir, dl) != 0 || e[dl] != '|' ||
            sscanf(e + dl + 1, "%63[^|]|%15[^|]|%d", n, sn, &d) != 3) {
            continue;
        }
        if (strcmp(sn, "-") == 0) {
            sn[0] = '\0';
        }
        *name = n;
        *short_name = sn;
        *is_dir = d;
        f->at++;
        return 1;
    }
    return 0;
}

static void fake_close(void *self)
{
    ((fake_t *)self)->open_now = 0;
}

static int32_t save_dir(const char *const *ents, const char *path, int *opens)
{
    fake_t f = { ents, "", 0, 0, 0, 0 };
    pipos_dir_reader_t r = { fake_open, fake_next, fake_close, &f };
    char out[PIP_PATH_MAX + 1];
    int32_t rc = pipos_file_path(path, 1, out);

    if (rc == 0) {
        rc = pipos_save_dir_check(out, &r);
    }
    CHECK(!f.open_now);
    if (opens != NULL) {
        *opens = f.opens;
    }
    return rc;
}

TEST(save_dir_identity)
{
    /* Plain names: no long name, so the short name is empty, as FatFs gives it. */
    static const char *const plain[] = {
        "/|PIPBOY|-|1", "/|DATA|-|1", "/|SAVEGA~1|-|1", "/PIPBOY|SAVE|-|1",
        "/PIPBOY|HOLO|-|1", "/PIPBOY|ROM1.ROM|-|0", NULL,
    };
    /* PC tools: long names with other aliases, in any case. */
    static const char *const alias[] = {
        "/|SaveGames|SAVEGA~1|1", "/|Pipboy|VAULT111|1", "/|Data|DATA|1",
        "/Pipboy|Save|SAVE~1|1", "/Pipboy|Notes For Me|NOTESF~1|1",
        "/Pipboy|SAVEX|SAVE~2|1", NULL,
    };
    /* PIPBOY reached by its alias PIPBOY~1, SAVE by its long name. */
    static const char *const pipalias[] = {
        "/|pipboy|PIPBOY~1|1", "/pipboy|save|SAVE|1", NULL,
    };
    /* SAVE is a file: no save folder can exist beside it. */
    static const char *const file[] = {
        "/|PIPBOY|-|1", "/PIPBOY|Save|SAVE~1|0", NULL,
    };
    /* A long name "SAVE" on an unrelated entry of the root. */
    static const char *const unrelated[] = {
        "/|SAVE|-|1", "/|Save Stuff|SAVE~1|1", "/|OTHER|-|1", NULL,
    };
    static const struct {
        const char *const *ents;
        const char *path;
        int32_t rc;
        int opens;
    } t[] = {
        /* one component: never the save folder, no read */
        { plain, "/", 0, 0 },
        { plain, "/PIPBOY", 0, 0 },
        { alias, "/VAULT111", 0, 0 },
        { plain, "/PIPBOY/HOLO", 0, 2 },
        { plain, "/DATA/X", 0, 1 },
        { plain, "/SAVEGA~1/X", 0, 1 },
        { plain, "/NOPE/SAVE", 0, 1 },          /* missing first component */
        { plain, "/PIPBOY/NOPE/X", 0, 2 },
        { plain, "/PIPBOY/SAVE", -PIP_EPERM, 0 }, /* the lexical rule, no read */
        { alias, "/PIPBOY/SAVE~1", -PIP_EPERM, 2 },
        { alias, "/pipboy/save~1", -PIP_EPERM, 2 },
        { alias, "/PipBoy/Save~1/A.SAV", -PIP_EPERM, 2 },
        { alias, "/VAULT111/SAVE~1/A.SAV", -PIP_EPERM, 2 },
        { alias, "/vault111/SAVE~1/X/Y/Z", -PIP_EPERM, 2 },
        { alias, "/Vault111/save", -PIP_EPERM, 2 },
        { alias, "/VAULT111/SAVE/~A.SAV", -PIP_EPERM, 2 },
        { alias, "/PIPBOY/NOTESF~1", 0, 2 },
        { alias, "/VAULT111/NOTESF~1/X.TXT", 0, 2 },
        { alias, "/VAULT111/SAVEX", 0, 2 },
        { alias, "/VAULT111/SAVE~2/X", 0, 2 },
        { alias, "/SAVEGA~1/SAVE~1", 0, 1 },
        { alias, "/DATA/SAVE~1", 0, 1 },
        { pipalias, "/PIPBOY~1/SAVE", -PIP_EPERM, 2 },
        { pipalias, "/PIPBOY~1/save/A.SAV", -PIP_EPERM, 2 },
        { pipalias, "/pipboy~1/SAVE~1", 0, 2 },
        { file, "/PIPBOY/SAVE~1", 0, 2 },
        { unrelated, "/SAVE~1/X", 0, 1 },
        { unrelated, "/SAVE/PIPBOY", 0, 1 },
    };
    unsigned i;

    for (i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
        int opens = -1;
        int32_t rc = save_dir(t[i].ents, t[i].path, &opens);

        if (rc != t[i].rc || opens != t[i].opens) {
            fprintf(stderr, "save dir \"%s\": got %d/%d expected %d/%d\n", t[i].path, rc,
                    opens, t[i].rc, t[i].opens);
        }
        CHECK_EQ(rc, t[i].rc);
        CHECK_EQ(opens, t[i].opens);
    }
}

TEST(save_dir_errors)
{
    static const char *const none[] = { NULL };
    fake_t f = { none, "", 0, 0, 0, -PIP_EIO };
    pipos_dir_reader_t r = { fake_open, fake_next, fake_close, &f };

    CHECK_EQ(pipos_save_dir_check("/A/B", &r), -PIP_EIO);
    CHECK_EQ(pipos_save_dir_check("/A", &r), 0);
    CHECK_EQ(pipos_save_dir_check("/", &r), 0);
    CHECK_EQ(pipos_save_dir_check("/ABCDEFGHIJKLM/B", &r), -PIP_EINVAL);
}

TEST(error_mapping)
{
    static const struct {
        int fs;
        int32_t pip;
    } t[] = {
        { FS_ERR_NOENT, -PIP_ENOENT },     { FS_ERR_NOSPC, -PIP_ENOSPC },
        { FS_ERR_IO, -PIP_EIO },           { FS_ERR_TIMEOUT, -PIP_EIO },
        { FS_ERR_NOTREADY, -PIP_EIO },     { FS_ERR_RO, -PIP_EIO },
        { FS_ERR_NOVOL, -PIP_EIO },        { FS_ERR_NOFS, -PIP_EIO },
        { FS_ERR_CORRUPT, -PIP_EIO },      { FS_ERR_DENIED, -PIP_EIO },
        { FS_ERR_BADF, -PIP_EIO },         { FS_ERR_INVAL, -PIP_EIO },
        { FS_ERR_NAME, -PIP_EIO },         { FS_ERR_EXIST, -PIP_EIO },
        { FS_ERR_MOUNTED, -PIP_EIO },      { FS_ERR_TOOBIG, -PIP_ERANGE },
        { -99, -PIP_EIO },
    };
    unsigned i;

    for (i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
        CHECK_EQ(pipos_storage_errno(t[i].fs), t[i].pip);
    }
}

TEST(handles)
{
    uint32_t used = 0;
    int32_t h;

    CHECK_EQ(pipos_handle_count(used), 0);
    for (h = 0; h < 8; h++) {
        CHECK_EQ(pipos_handle_check(used, h), -PIP_EBADF);
        CHECK_EQ(pipos_handle_alloc(&used), h);
        CHECK_EQ(pipos_handle_check(used, h), 0);
    }
    CHECK_EQ(pipos_handle_count(used), 8);
    CHECK_EQ(pipos_handle_alloc(&used), -PIP_ENOMEM);
    CHECK_EQ(pipos_handle_check(used, -1), -PIP_EBADF);
    CHECK_EQ(pipos_handle_check(used, 8), -PIP_EBADF);
    CHECK_EQ(pipos_handle_check(used, 32), -PIP_EBADF);
    CHECK_EQ(pipos_handle_check(used, (int32_t)0x80000000u), -PIP_EBADF);
    pipos_handle_free(&used, 5);
    pipos_handle_free(&used, 2);
    CHECK_EQ(pipos_handle_count(used), 6);
    CHECK_EQ(pipos_handle_check(used, 5), -PIP_EBADF);
    CHECK_EQ(pipos_handle_alloc(&used), 2);           /* lowest free first */
    CHECK_EQ(pipos_handle_alloc(&used), 5);
    CHECK_EQ(pipos_handle_alloc(&used), -PIP_ENOMEM);
    pipos_handle_free(&used, 9);                      /* out of range: no change */
    pipos_handle_free(&used, -3);
    CHECK_EQ(used, 0xffu);
    used = 0;                                         /* close-all */
    CHECK_EQ(pipos_handle_count(used), 0);
    CHECK_EQ(pipos_handle_alloc(&used), 0);
}

TEST(dir_entry_skipped)
{
    /* macOS side files and folders, by their long names. */
    CHECK_EQ(pipos_dir_entry_skipped("._CULVERT.HTP", FS_ATTR_ARCHIVE), 1);
    CHECK_EQ(pipos_dir_entry_skipped(".DS_Store", 0), 1);
    CHECK_EQ(pipos_dir_entry_skipped(".Spotlight-V100", FS_ATTR_DIR), 1);
    CHECK_EQ(pipos_dir_entry_skipped(".fseventsd", FS_ATTR_DIR | FS_ATTR_HIDDEN), 1);
    CHECK_EQ(pipos_dir_entry_skipped(".Trashes", FS_ATTR_DIR | FS_ATTR_HIDDEN), 1);
    /* Hidden, system and volume label bits, whatever the name. */
    CHECK_EQ(pipos_dir_entry_skipped("TAPE.HTP", FS_ATTR_HIDDEN), 1);
    CHECK_EQ(pipos_dir_entry_skipped("TAPE.HTP", FS_ATTR_SYSTEM | FS_ATTR_ARCHIVE), 1);
    CHECK_EQ(pipos_dir_entry_skipped("PIPOS", PIPOS_FAT_ATTR_VOLUME), 1);
    /* Shown: plain files and folders, read-only, and names that only hold a dot. */
    CHECK_EQ(pipos_dir_entry_skipped("TAPE.HTP", 0), 0);
    CHECK_EQ(pipos_dir_entry_skipped("TAPE.HTP", FS_ATTR_ARCHIVE | FS_ATTR_RDONLY), 0);
    CHECK_EQ(pipos_dir_entry_skipped("HOLO", FS_ATTR_DIR), 0);
    CHECK_EQ(pipos_dir_entry_skipped("_CULVE~1.HTP", FS_ATTR_ARCHIVE), 0);
    CHECK_EQ(pipos_dir_entry_skipped("#GAME.HTP", 0), 0);
    CHECK_EQ(pipos_dir_entry_skipped("~GAME.HTP", 0), 0);
    CHECK_EQ(pipos_dir_entry_skipped("A.B.HTP", 0), 0);
}
