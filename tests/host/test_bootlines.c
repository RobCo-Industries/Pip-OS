#include <robco/rimg.h>

#include "bootlines.h"
#include "rc_test.h"

TEST(head_and_tail_text)
{
    CHECK_STR(pipos_boot_head[0], "*************** PIP-OS(R) V7.1.0.8 ***************");
    CHECK_STR(pipos_boot_head[4], "LOADER V1.1");
    CHECK_STR(pipos_boot_head[7], "38911 BYTES FREE");
    CHECK(pipos_boot_head[8] == NULL);
    CHECK_STR(pipos_boot_tail[2], "PIP-OS KERNEL INITIALIZED");
    CHECK_STR(pipos_boot_tail[3], "PIP-OS READY");
}

TEST(lines_fit_the_screen)
{
    unsigned i;

    for (i = 0; pipos_boot_head[i] != NULL; i++) {
        CHECK(strlen(pipos_boot_head[i]) < PIPOS_LINE_MAX);
    }
    for (i = 0; pipos_boot_tail[i] != NULL; i++) {
        CHECK(strlen(pipos_boot_tail[i]) < PIPOS_LINE_MAX);
    }
}

TEST(version)
{
    char buf[24];

    CHECK_EQ(pipos_fmt_version((3u << 16) | (0u << 8) | 3u, buf, sizeof(buf)), 5);
    CHECK_STR(buf, "3.0.3");
    pipos_fmt_version((1u << 16) | (2u << 8) | 10u, buf, sizeof(buf));
    CHECK_STR(buf, "1.2.10");
    pipos_fmt_version(0xffffffffu, buf, sizeof(buf));
    CHECK_STR(buf, "65535.255.255");
    pipos_fmt_version(0, buf, sizeof(buf));
    CHECK_STR(buf, "0.0.0");
}

TEST(name)
{
    char out[PIPOS_ROM_NAME_MAX + 1];
    const char full[16] = { 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
                            'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P' };

    pipos_fmt_name("deitrix\0\0\0\0\0\0\0\0", 16, out, sizeof(out), 1);
    CHECK_STR(out, "DEITRIX");
    pipos_fmt_name("deitrix", 7, out, sizeof(out), 0);
    CHECK_STR(out, "deitrix");
    pipos_fmt_name(full, 16, out, sizeof(out), 0);
    CHECK_STR(out, "ABCDEFGHIJKLMNOP");
    pipos_fmt_name("a b\x01\xff", 5, out, sizeof(out), 1);
    CHECK_STR(out, "A_B__");
    pipos_fmt_name("\0\0", 2, out, sizeof(out), 1);
    CHECK_STR(out, "-");
}

TEST(holotape_line)
{
    char buf[PIPOS_LINE_MAX];

    pipos_holo_line(0, buf, sizeof(buf));
    CHECK_STR(buf, "NO HOLOTAPE FOUND");
    pipos_holo_line(1, buf, sizeof(buf));
    CHECK_STR(buf, "1 HOLOTAPE FOUND");
    pipos_holo_line(12, buf, sizeof(buf));
    CHECK_STR(buf, "12 HOLOTAPES FOUND");
}

TEST(rom_line)
{
    char buf[PIPOS_LINE_MAX];

    pipos_rom_line("DEITRIX", (3u << 16) | 3u, buf, sizeof(buf));
    CHECK_STR(buf, "LOAD ROM(1): DEITRIX 3.0.3");
}

TEST(rom_error_lines)
{
    char buf[PIPOS_LINE_MAX];

    pipos_rom_error_line(0, RIMG_E_IO, buf, sizeof(buf));
    CHECK_STR(buf, "ROM(1) NOT LOADED: NO SD CARD");
    pipos_rom_error_line(1, RIMG_E_NOFILE, buf, sizeof(buf));
    CHECK_STR(buf, "ROM(1) NOT LOADED: NO FILE /PIPBOY/ROM1.ROM");
    pipos_rom_error_line(1, RIMG_E_MAGIC, buf, sizeof(buf));
    CHECK_STR(buf, "ROM(1) NOT LOADED: NOT A ROM IMAGE");
    pipos_rom_error_line(1, RIMG_E_BOUNDS, buf, sizeof(buf));
    CHECK_STR(buf, "ROM(1) NOT LOADED: DOES NOT FIT THE ROM SLOT");
    pipos_rom_error_line(1, RIMG_E_CRC, buf, sizeof(buf));
    CHECK_STR(buf, "ROM(1) NOT LOADED: CHECKSUM MISMATCH");
    pipos_rom_error_line(1, 99, buf, sizeof(buf));
    CHECK_STR(buf, "ROM(1) NOT LOADED: UNKNOWN ERROR");
}

TEST(rom_error_lines_fit)
{
    char buf[PIPOS_LINE_MAX];
    int rc;

    for (rc = RIMG_E_NOFILE; rc <= RIMG_E_CRC; rc++) {
        CHECK(pipos_rom_error_line(1, rc, buf, sizeof(buf)) < (int)PIPOS_LINE_MAX);
    }
}

TEST(holotape_names)
{
    CHECK(pipos_is_holotape_name("GAME.HTP"));
    CHECK(pipos_is_holotape_name("game.htp"));
    CHECK(pipos_is_holotape_name("A.HtP"));
    CHECK(!pipos_is_holotape_name(".HTP"));
    CHECK(!pipos_is_holotape_name("GAME.HTPX"));
    CHECK(!pipos_is_holotape_name("GAME.TXT"));
    CHECK(!pipos_is_holotape_name("GAMEHTP"));
    CHECK(!pipos_is_holotape_name(""));
}

TEST(marker_word)
{
    char out[16];

    pipos_marker_word("kernel panic now", out, sizeof(out));
    CHECK_STR(out, "kernel_panic_no");
    pipos_marker_word("", out, sizeof(out));
    CHECK_STR(out, "-");
    pipos_marker_word(NULL, out, sizeof(out));
    CHECK_STR(out, "-");
    pipos_marker_word("a\tb\x7f", out, sizeof(out));
    CHECK_STR(out, "a_b_");
}

TEST(blank_lines)
{
    CHECK_EQ(pipos_line_blank(""), 1);
    CHECK_EQ(pipos_line_blank("   "), 1);
    CHECK_EQ(pipos_line_blank("\t\x7f"), 1);
    CHECK_EQ(pipos_line_blank(" A "), 0);
    CHECK_EQ(pipos_line_blank("*"), 0);
    /* The boot text has two blank lines among its fixed ones. */
    CHECK_EQ(pipos_line_blank(pipos_boot_head[3]), 1);
    CHECK_EQ(pipos_line_blank(pipos_boot_tail[0]), 1);
    CHECK_EQ(pipos_line_blank(pipos_boot_head[0]), 0);
}
