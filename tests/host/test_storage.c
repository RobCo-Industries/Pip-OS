/*
 * kernel/storage_rules.c: save names, save paths, file paths with hostile
 * spellings, the FS error mapping and the handle table.
 */
#include <stdint.h>
#include <string.h>

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
        { FS_ERR_MOUNTED, -PIP_EIO },      { -99, -PIP_EIO },
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
