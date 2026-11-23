/*
 * kernel/holo_rules.c: holotape names, the alias match, the listing
 * check, the holo_enumerate entry, the startup block and the results.
 */
#include <stdint.h>
#include <string.h>

#include <pipos/abi.h>
#include <robco/rimg.h>
#include <robco/umode.h>

#include "holo.h"
#include "rc_test.h"

TEST(names)
{
    static const struct {
        const char *in, *out;
    } good[] = {
        { "A.HTP", "A.HTP" },           { "game.htp", "GAME.HTP" },
        { "ABCDEFGH.HTP", "ABCDEFGH.HTP" }, { "VERYLO~1.HTP", "VERYLO~1.HTP" },
        { "a_b-c!.Htp", "A_B-C!.HTP" }, { "{X}@^`.HTP", "{X}@^`.HTP" },
        { "0.HTP", "0.HTP" },           { "#$%&'().HTP", "#$%&'().HTP" },
    };
    static const char *const bad[] = {
        "", ".HTP", "HTP", "A.HT", "A.HTPX", "A.TXT", "A", "ABCDEFGHI.HTP", "A.B.HTP",
        "/A.HTP", "HOLO/A.HTP", "A\\B.HTP", "C:A.HTP", "sd:A.HTP", ".", "..", "../A.HTP",
        "*.HTP", "A?.HTP", "A .HTP", " A.HTP", "A+B.HTP", "A,B.HTP", "A;B.HTP", "A=B.HTP",
        "A[B.HTP", "A]B.HTP", "A\"B.HTP", "A<B.HTP", "A>B.HTP", "A|B.HTP", "\xe9.HTP",
        "A\x7f.HTP", "A\x01.HTP", "ABCDEFGHIJKLM", "A.HTP ", "A.HTP.",
    };
    char out[PIPOS_HOLO_NAME_SIZE];
    unsigned i;

    for (i = 0; i < sizeof(good) / sizeof(good[0]); i++) {
        CHECK_EQ(pipos_holo_name(good[i].in, out), 0);
        CHECK_STR(out, good[i].out);
    }
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        CHECK_EQ(pipos_holo_name(bad[i], out), -PIP_EINVAL);
    }
}

TEST(listed_name_and_match)
{
    CHECK_STR(pipos_holo_listed_name("Long name.htp", "LONGNA~1.HTP"), "LONGNA~1.HTP");
    CHECK_STR(pipos_holo_listed_name("GAME.HTP", ""), "GAME.HTP");
    /* A leading '#' or '~' is an ordinary holotape name, not a work file. */
    CHECK_STR(pipos_holo_listed_name("#GAME.HTP", ""), "#GAME.HTP");
    CHECK_STR(pipos_holo_listed_name("~GAME.HTP", ""), "~GAME.HTP");
    CHECK(pipos_holo_entry_match("#GAME.HTP", "#GAME.HTP", ""));
    CHECK(pipos_holo_entry_match("~GAME.HTP", "~GAME.HTP", ""));
    /* FatFs finds an entry by its long name or its alias, ignoring case. */
    CHECK(pipos_holo_entry_match("GAME.HTP", "game.htp", "GAME.HTP"));
    CHECK(pipos_holo_entry_match("GAME.HTP", "Game.Htp", ""));
    CHECK(pipos_holo_entry_match("OTHER~1.HTP", "other thing.htp", "OTHER~1.HTP"));
    CHECK(pipos_holo_entry_match("GAME.HTP", "GAME.HTP", "OTHER.HTP"));
    CHECK(!pipos_holo_entry_match("GAME.HTP", "GAMES.HTP", "GAMES.HTP"));
    CHECK(!pipos_holo_entry_match("GAME.HTP", "GAME.HT", ""));
    CHECK(!pipos_holo_entry_match("GAME.HTP", "GAME.HTPX", "GAME~1.HTP"));
}

/* A sealed holotape header: stub, rimg header, META block. */
static void make_head(uint8_t *b, uint32_t file_size)
{
    rimg_header_t h;

    memset(b, 0, PIPOS_HOLO_HEAD);
    memset(&h, 0, sizeof(h));
    memcpy(h.magic, RIMG_MAGIC_ROBCO78, 8);
    h.hdr_version = RIMG_HDR_VERSION;
    h.hdr_size = RIMG_HDR_SIZE;
    h.flags = RIMG_FLAG_META;
    h.load_addr = PIPOS_HOLO_BASE;
    h.file_size = file_size;
    h.mem_size = file_size;
    h.abi_version = PIP_ABI_VERSION;
    h.image_version = 0x00010203;
    h.subtype = RIMG_HOLO_UTILITY;
    memcpy(h.name, "SIXTEEN-BYTES-NM", 16);
    memcpy(b + RIMG_HDR_OFFSET, &h, sizeof(h));
    memcpy(b + RIMG_META_OFFSET, "Title", 5);
    b[RIMG_META_OFFSET + 64] = 0x80;
    b[RIMG_META_OFFSET + 191] = 0x01;
}

static void set32(uint8_t *b, unsigned off, uint32_t v)
{
    memcpy(b + off, &v, 4);
}

TEST(listing_checks)
{
    uint8_t b[PIPOS_HOLO_HEAD];

    make_head(b, 0x400);
    CHECK(pipos_holo_listed(b, sizeof(b), 0x400));
    /* No CRC check: the CRC field is 0. Steps 4 and 5 do not matter either. */
    set32(b, 0x48, RIMG_HOLO_DATA);
    CHECK(pipos_holo_listed(b, sizeof(b), 0x400));
    set32(b, 0x40, 0x00020000);
    CHECK(pipos_holo_listed(b, sizeof(b), 0x400));
    set32(b, 0x30, 0x00400000);
    CHECK(pipos_holo_listed(b, sizeof(b), 0x400));
    /* Size: the FAT size must equal file_size. */
    make_head(b, 0x400);
    CHECK(!pipos_holo_listed(b, sizeof(b), 0x401));
    CHECK(!pipos_holo_listed(b, sizeof(b), 0x3ff));
    CHECK(!pipos_holo_listed(b, sizeof(b) - 1, 0x400));
    make_head(b, 0x100);
    CHECK(!pipos_holo_listed(b, sizeof(b), 0x100));
    /* Magic. */
    make_head(b, 0x400);
    memcpy(b + 0x20, RIMG_MAGIC_ROM1, 8);
    CHECK(!pipos_holo_listed(b, sizeof(b), 0x400));
    memcpy(b + 0x20, RIMG_MAGIC_BOOT, 8);
    CHECK(!pipos_holo_listed(b, sizeof(b), 0x400));
    /* Version: hdr_version, hdr_size, flags, reserved. */
    make_head(b, 0x400);
    b[0x28] = 2;
    CHECK(!pipos_holo_listed(b, sizeof(b), 0x400));
    make_head(b, 0x400);
    b[0x2A] = 63;
    CHECK(!pipos_holo_listed(b, sizeof(b), 0x400));
    make_head(b, 0x400);
    set32(b, 0x2C, 0);
    CHECK(!pipos_holo_listed(b, sizeof(b), 0x400));
    make_head(b, 0x400);
    set32(b, 0x2C, 3);
    CHECK(!pipos_holo_listed(b, sizeof(b), 0x400));
    make_head(b, 0x400);
    set32(b, 0x5C, 1);
    CHECK(!pipos_holo_listed(b, sizeof(b), 0x400));
}

TEST(info_fill)
{
    uint8_t b[PIPOS_HOLO_HEAD];
    pip_holo_info_t info;
    uint8_t icon[128] = { 0 };

    make_head(b, 0x400);
    memset(&info, 0xAA, sizeof(info));
    pipos_holo_info_fill(&info, "GAME.HTP", 0x400, b);
    CHECK_STR(info.file, "GAME.HTP");
    CHECK_EQ(info.file[12], 0);
    CHECK_EQ(info.subtype, RIMG_HOLO_UTILITY);
    CHECK_EQ(info.reserved, 0);
    CHECK_EQ(info.size, 0x400);
    CHECK_EQ(info.image_version, 0x00010203);
    CHECK(memcmp(info.name, "SIXTEEN-BYTES-NM", 16) == 0);
    CHECK_STR(info.title, "Title");
    CHECK_EQ(info.title[63], 0);
    icon[0] = 0x80;
    icon[127] = 0x01;
    CHECK(memcmp(info.icon, icon, sizeof(icon)) == 0);
    /* An empty META block gives an empty title and a zero icon. */
    memset(b + RIMG_META_OFFSET, 0, RIMG_META_SIZE);
    pipos_holo_info_fill(&info, "ABCDEFGH.HTP", 0x400, b);
    CHECK_STR(info.file, "ABCDEFGH.HTP");
    CHECK_STR(info.title, "");
    memset(icon, 0, sizeof(icon));
    CHECK(memcmp(info.icon, icon, sizeof(icon)) == 0);
}

TEST(startup_block)
{
    pipos_startup_t s;
    pip_mem_info_t m;
    char args[150];

    memset(&s, 0xAA, sizeof(s));
    pipos_startup_holo(&s, "");
    CHECK_EQ(s.abi_version, PIP_ABI_VERSION);
    CHECK_EQ(s.size, 128);
    CHECK_EQ(s.heap_base, 0x01800000);
    CHECK_EQ(s.heap_size, 0x007F0000);
    CHECK_EQ(s.stack_top, 0x02000000);
    CHECK_EQ(s.kind, PIP_KIND_HOLOTAPE);
    CHECK_EQ(s.flags, 0);
    CHECK_STR(s.args, "");
    CHECK_EQ(s.args[99], 0);
    memset(args, 'x', 99);
    args[99] = '\0';
    pipos_startup_holo(&s, args);
    CHECK_STR(s.args, args);
    memset(args, 'y', sizeof(args) - 1);
    args[sizeof(args) - 1] = '\0';
    pipos_startup_holo(&s, args);
    CHECK_EQ(strlen(s.args), 99);
    CHECK_EQ(PIPOS_HOLO_STARTUP, 0x01FF0000);
    CHECK_EQ(PIPOS_HOLO_STUB, 0x01FF0080);
    pipos_mem_info_holo(&m);
    CHECK_EQ(m.heap_base, 0x01800000);
    CHECK_EQ(m.heap_size, 0x007F0000);
    CHECK_EQ(m.stack_top, 0x02000000);
    CHECK_EQ(m.stack_size, 0x00010000);
}

TEST(results)
{
    CHECK_EQ(pipos_holo_result(UMODE_RETURNED, 0), 0);
    CHECK_EQ(pipos_holo_result(UMODE_EXITED, 1), 1);
    CHECK_EQ(pipos_holo_result(UMODE_EXITED, 255), 255);
    CHECK_EQ(pipos_holo_result(UMODE_EXITED, 256), 0);
    CHECK_EQ(pipos_holo_result(UMODE_EXITED, -1), 255);
    CHECK_EQ(pipos_holo_result(UMODE_RETURNED, 0x1234), 0x34);
    CHECK_EQ(pipos_holo_result(UMODE_FAULT, 0), -PIP_EFAULT);
    CHECK_EQ(pipos_holo_result(UMODE_KILLED, 1), -PIP_EFAULT);
    CHECK_EQ(pipos_holo_load_errno(RIMG_E_NOFILE, 0), -PIP_ENOENT);
    CHECK_EQ(pipos_holo_load_errno(RIMG_E_NOFILE, 0x1000), -PIP_ENOENT);
    CHECK_EQ(pipos_holo_load_errno(RIMG_E_MAGIC, 0x1000), -PIP_ENOEXEC);
    CHECK_EQ(pipos_holo_load_errno(RIMG_E_SIZE, 0x1000), -PIP_ENOEXEC);
    CHECK_EQ(pipos_holo_load_errno(RIMG_E_BOUNDS, 0x1000), -PIP_ENOEXEC);
    CHECK_EQ(pipos_holo_load_errno(RIMG_E_CRC, 0x1000), -PIP_ENOEXEC);
}

/* RIMG_E_IO is a card error only when the file holds the loader's 0x60-byte head. */
TEST(load_io_error)
{
    CHECK_EQ(pipos_holo_load_errno(RIMG_E_IO, 0), -PIP_ENOEXEC);
    CHECK_EQ(pipos_holo_load_errno(RIMG_E_IO, 0x5F), -PIP_ENOEXEC);
    CHECK_EQ(pipos_holo_load_errno(RIMG_E_IO, 0x60), -PIP_EIO);
    CHECK_EQ(pipos_holo_load_errno(RIMG_E_IO, 0x1000), -PIP_EIO);
}
