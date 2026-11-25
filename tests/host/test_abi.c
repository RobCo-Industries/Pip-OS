/*
 * sdk/include/pipos/abi.h against RobCo-Core's PIPOS_ABI.md: struct sizes
 * and offsets, the syscall numbers, the PIP_SYSCALLS table and the limits.
 */
#include <stddef.h>
#include <string.h>

#include <pipos/abi.h>

#include "rc_test.h"

TEST(struct_sizes)
{
    CHECK_EQ(sizeof(pipos_startup_t), 128);
    CHECK_EQ(sizeof(pip_gfx_info_t), 16);
    CHECK_EQ(sizeof(pip_arc_t), 12);
    CHECK_EQ(sizeof(pip_blit_t), 20);
    CHECK_EQ(sizeof(pip_event_t), 16);
    CHECK_EQ(sizeof(pip_note_t), 4);
    CHECK_EQ(sizeof(pip_dirent_t), 20);
    CHECK_EQ(sizeof(pip_holo_info_t), 232);
    CHECK_EQ(sizeof(pip_sys_info_t), 32);
    CHECK_EQ(sizeof(pip_mem_info_t), 16);
}

TEST(startup_offsets)
{
    CHECK_EQ(offsetof(pipos_startup_t, abi_version), 0x00);
    CHECK_EQ(offsetof(pipos_startup_t, size), 0x04);
    CHECK_EQ(offsetof(pipos_startup_t, heap_base), 0x08);
    CHECK_EQ(offsetof(pipos_startup_t, heap_size), 0x0C);
    CHECK_EQ(offsetof(pipos_startup_t, stack_top), 0x10);
    CHECK_EQ(offsetof(pipos_startup_t, kind), 0x14);
    CHECK_EQ(offsetof(pipos_startup_t, flags), 0x18);
    CHECK_EQ(offsetof(pipos_startup_t, args), 0x1C);
    CHECK_EQ(sizeof(((pipos_startup_t *)0)->args), 100);
}

TEST(gfx_offsets)
{
    CHECK_EQ(offsetof(pip_gfx_info_t, width), 0x00);
    CHECK_EQ(offsetof(pip_gfx_info_t, height), 0x02);
    CHECK_EQ(offsetof(pip_gfx_info_t, format), 0x04);
    CHECK_EQ(offsetof(pip_gfx_info_t, backend), 0x08);
    CHECK_EQ(offsetof(pip_gfx_info_t, fg_default), 0x0C);

    CHECK_EQ(offsetof(pip_arc_t, cx), 0x00);
    CHECK_EQ(offsetof(pip_arc_t, cy), 0x02);
    CHECK_EQ(offsetof(pip_arc_t, radius), 0x04);
    CHECK_EQ(offsetof(pip_arc_t, start_deg), 0x06);
    CHECK_EQ(offsetof(pip_arc_t, end_deg), 0x08);
    CHECK_EQ(offsetof(pip_arc_t, fill), 0x0A);

    CHECK_EQ(offsetof(pip_blit_t, x), 0x00);
    CHECK_EQ(offsetof(pip_blit_t, y), 0x02);
    CHECK_EQ(offsetof(pip_blit_t, w), 0x04);
    CHECK_EQ(offsetof(pip_blit_t, h), 0x06);
    CHECK_EQ(offsetof(pip_blit_t, stride), 0x08);
    CHECK_EQ(offsetof(pip_blit_t, flags), 0x0A);
    CHECK_EQ(offsetof(pip_blit_t, data), 0x0C);
    CHECK_EQ(offsetof(pip_blit_t, color), 0x10);
}

TEST(input_audio_offsets)
{
    CHECK_EQ(offsetof(pip_event_t, type), 0x00);
    CHECK_EQ(offsetof(pip_event_t, code), 0x04);
    CHECK_EQ(offsetof(pip_event_t, value), 0x08);
    CHECK_EQ(offsetof(pip_event_t, time_ms), 0x0C);

    CHECK_EQ(offsetof(pip_note_t, freq_hz), 0x00);
    CHECK_EQ(offsetof(pip_note_t, duration_ms), 0x02);
}

TEST(file_holo_offsets)
{
    CHECK_EQ(offsetof(pip_dirent_t, name), 0x00);
    CHECK_EQ(sizeof(((pip_dirent_t *)0)->name), 13);
    CHECK_EQ(offsetof(pip_dirent_t, flags), 0x0D);
    CHECK_EQ(offsetof(pip_dirent_t, reserved), 0x0E);
    CHECK_EQ(offsetof(pip_dirent_t, size), 0x10);

    CHECK_EQ(offsetof(pip_holo_info_t, file), 0x00);
    CHECK_EQ(offsetof(pip_holo_info_t, subtype), 0x0D);
    CHECK_EQ(offsetof(pip_holo_info_t, flags), 0x0E);
    CHECK_EQ(offsetof(pip_holo_info_t, size), 0x10);
    CHECK_EQ(offsetof(pip_holo_info_t, image_version), 0x14);
    CHECK_EQ(offsetof(pip_holo_info_t, name), 0x18);
    CHECK_EQ(offsetof(pip_holo_info_t, title), 0x28);
    CHECK_EQ(offsetof(pip_holo_info_t, icon), 0x68);
}

TEST(info_offsets)
{
    CHECK_EQ(offsetof(pip_sys_info_t, size), 0x00);
    CHECK_EQ(offsetof(pip_sys_info_t, abi_version), 0x04);
    CHECK_EQ(offsetof(pip_sys_info_t, pipos_version), 0x08);
    CHECK_EQ(offsetof(pip_sys_info_t, board_rev), 0x0C);
    CHECK_EQ(offsetof(pip_sys_info_t, soc), 0x10);
    CHECK_EQ(offsetof(pip_sys_info_t, mem_size), 0x14);
    CHECK_EQ(offsetof(pip_sys_info_t, hw_flags), 0x18);
    CHECK_EQ(offsetof(pip_sys_info_t, reserved), 0x1C);

    CHECK_EQ(offsetof(pip_mem_info_t, heap_base), 0x00);
    CHECK_EQ(offsetof(pip_mem_info_t, heap_size), 0x04);
    CHECK_EQ(offsetof(pip_mem_info_t, stack_top), 0x08);
    CHECK_EQ(offsetof(pip_mem_info_t, stack_size), 0x0C);
}

/* The spec's table, number and name, typed from PIPOS_ABI.md. */
static const struct {
    unsigned nr;
    const char *name;
} spec[] = {
    { 0x01, "draw_line" }, { 0x02, "draw_point" }, { 0x03, "draw_text" },
    { 0x04, "clear" }, { 0x05, "gfx_info" }, { 0x06, "rect" }, { 0x07, "polyline" },
    { 0x08, "arc" }, { 0x09, "blit_1bpp" }, { 0x0A, "blit_rgb" }, { 0x0B, "set_clip" },
    { 0x0C, "set_color" }, { 0x0D, "text_metrics" }, { 0x0E, "present" },
    { 0x0F, "set_font" }, { 0x10, "read_buttons" }, { 0x11, "read_dial" },
    { 0x12, "poll_event" }, { 0x13, "wait_event" }, { 0x20, "tone" }, { 0x21, "sample" },
    { 0x22, "audio_stop" }, { 0x23, "volume" }, { 0x24, "audio_status" },
    { 0x25, "play_seq" }, { 0x26, "sample_queue" }, { 0x30, "read_sensor" }, { 0x31, "sensor_available" },
    { 0x40, "get_time" }, { 0x41, "get_battery" }, { 0x42, "ticks_ms" },
    { 0x43, "sleep_ms" }, { 0x44, "exit" }, { 0x45, "sys_info" }, { 0x46, "mem_info" },
    { 0x47, "power" }, { 0x48, "log" }, { 0x49, "rand" }, { 0x4A, "set_time" },
    { 0x50, "read_save" }, { 0x51, "write_save" }, { 0x52, "save_info" },
    { 0x53, "save_delete" }, { 0x54, "file_open" }, { 0x55, "file_read" },
    { 0x56, "file_close" }, { 0x57, "dir_list" }, { 0x60, "holo_enumerate" },
    { 0x61, "holo_launch" }, { 0x62, "holo_last_status" }, { 0x63, "holo_eject_event" },
};

#define ENTRY(NAME, name) { PIP_SYS_##NAME, #name },
static const struct {
    unsigned nr;
    const char *name;
} table[] = { PIP_SYSCALLS(ENTRY) };

TEST(syscall_table_matches_spec)
{
    unsigned i;

    CHECK_EQ(sizeof(table) / sizeof(table[0]), sizeof(spec) / sizeof(spec[0]));
    CHECK_EQ(sizeof(table) / sizeof(table[0]), PIP_SYSCALL_COUNT);
    for (i = 0; i < sizeof(spec) / sizeof(spec[0]); i++) {
        CHECK_EQ(table[i].nr, spec[i].nr);
        CHECK_STR(table[i].name, spec[i].name);
        CHECK(table[i].nr < PIP_SYS_NR_LIMIT);
        if (i > 0) {
            CHECK(table[i].nr > table[i - 1].nr);
        }
    }
}

TEST(constants)
{
    CHECK_EQ(PIP_COLOR_NONE, 0x01000000u);
    CHECK_EQ(PIP_EV_BUTTON, 1);
    CHECK_EQ(PIP_EV_DIAL, 2);
    CHECK_EQ(PIP_BTN_UP_BIT, 0x01u);
    CHECK_EQ(PIP_BTN_DOWN_BIT, 0x02u);
    CHECK_EQ(PIP_BTN_LEFT_BIT, 0x04u);
    CHECK_EQ(PIP_BTN_RIGHT_BIT, 0x08u);
    CHECK_EQ(PIP_BTN_SELECT_BIT, 0x10u);
    CHECK_EQ(PIP_BTN_DIAL_BIT, 0x20u);
    CHECK_EQ(PIP_BTN_BACK, 6);
    CHECK_EQ(PIP_BTN_BACK_BIT, 0x40u);
    CHECK_EQ(PIP_BTN_COUNT, 7);
    CHECK_EQ(PIP_SENSOR_RTC, 0);
    CHECK_EQ(PIP_SENSOR_HEART, 6);
    CHECK_EQ(PIP_POWER_REBOOT, 0);
    CHECK_EQ(PIP_POWER_SCREEN_ON, 3);
    CHECK_EQ(PIP_HOLO_DATA, 2);
    CHECK_EQ(PIP_WAIT_FOREVER, 0xFFFFFFFFu);
    CHECK_EQ(PIP_WH(320, 240), 0x014000F0u);
    CHECK_EQ(PIP_WH(0x1ffff, 1), 0xFFFF0001u);
    CHECK_EQ(PIP_TM_WIDTH(0x00280010u), 40u);
    CHECK_EQ(PIP_TM_HEIGHT(0x00280010u), 16u);
    CHECK_EQ(PIP_TEXT_MAX, 255);
    CHECK_EQ(PIP_SCALE_MAX, 8);
    CHECK_EQ(PIP_POLY_MAX, 256);
    CHECK_EQ(PIP_ARC_RADIUS_MAX, 512);
    CHECK_EQ(PIP_EVENT_QUEUE, 32);
    CHECK_EQ(PIP_TONE_FREQ_MAX, 20000);
    CHECK_EQ(PIP_TONE_MS_MAX, 60000);
    CHECK_EQ(PIP_SAMPLE_MAX, 65536);
    CHECK_EQ(PIP_SEQ_MAX, 256);
    CHECK_EQ(PIP_VOLUME_MAX, 100);
    CHECK_EQ(PIP_SAVE_MAX, 65536);
    CHECK_EQ(PIP_SAVE_NAME_MAX, 8);
    CHECK_EQ(PIP_PATH_MAX, 127);
    CHECK_EQ(PIP_FILES_MAX, 8);
    CHECK_EQ(PIP_DIR_LIST_MAX, 64);
    CHECK_EQ(PIP_HOLO_ENUM_MAX, 32);
    CHECK_EQ(PIP_ARGS_MAX + 1, PIP_ARGS_SIZE);
    CHECK_EQ(PIP_LOG_MAX, 256);
}

/* ABI 1.4: the holotape slot. */
TEST(abi_1_4)
{
    CHECK_EQ(PIP_ABI_VERSION, 0x00010004);
    CHECK_EQ(PIP_EV_TAPE, 3);
    CHECK_EQ(PIP_HW_TAPE, 0x8u);
    CHECK_EQ(PIP_HOLO_SLOT, 0x1u);
    CHECK_EQ(PIP_HOLO_EV_INSERTED, 0x1u);
    CHECK_EQ(PIP_HOLO_EV_REMOVED, 0x2u);
    CHECK_EQ(PIP_HOLO_EV_BAD, 0x4u);
    CHECK_EQ(sizeof(((pip_holo_info_t *)0)->flags), 2);
}
