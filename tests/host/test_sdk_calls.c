/*
 * libpip.a's wrappers in sdk/lib/syscall.c, built on the host with a
 * fake pip_syscall(): each passes its own number and its arguments in
 * order, and returns the call's result. Every call of PIP_SYSCALLS has
 * a wrapper: a missing one fails the build.
 */
#include <setjmp.h>
#include <stdint.h>

#include "../../sdk/lib/syscall.c"

#include "rc_test.h"

#define RET 0x5a5a

static uint32_t got[5];
static int calls;
static jmp_buf exit_jmp;

int32_t pip_syscall(uint32_t nr, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    got[0] = nr;
    got[1] = a0;
    got[2] = a1;
    got[3] = a2;
    got[4] = a3;
    calls++;
    if (nr == PIP_SYS_EXIT) {
        longjmp(exit_jmp, 1);
    }
    return RET;
}

#define CHECK_CALL(nr, a0, a1, a2, a3)                                           \
    do {                                                                         \
        CHECK_EQ(got[0], (uint32_t)(nr));                                         \
        CHECK_EQ(got[1], (uint32_t)(a0));                                         \
        CHECK_EQ(got[2], (uint32_t)(a1));                                         \
        CHECK_EQ(got[3], (uint32_t)(a2));                                         \
        CHECK_EQ(got[4], (uint32_t)(a3));                                         \
    } while (0)

/* Each wrapper exists with the name pip_<name>. */
#define ADDR(NAME, name) (void (*)(void))pip_##name,
static void (*const wrappers[])(void) = { PIP_SYSCALLS(ADDR) };

TEST(every_call_has_a_wrapper)
{
    unsigned i;

    CHECK_EQ(sizeof(wrappers) / sizeof(wrappers[0]), PIP_SYSCALL_COUNT);
    for (i = 0; i < sizeof(wrappers) / sizeof(wrappers[0]); i++) {
        CHECK(wrappers[i] != NULL);
    }
}

TEST(wrappers_pass_number_and_arguments)
{
    calls = 0;
    CHECK_EQ(pip_draw_line(0x111, 0x122, 0x133, 0x144), RET);
    CHECK_CALL(PIP_SYS_DRAW_LINE, 0x111, 0x122, 0x133, 0x144);
    CHECK_EQ(pip_draw_point(0x111, 0x122, 0x133), RET);
    CHECK_CALL(PIP_SYS_DRAW_POINT, 0x111, 0x122, 0x133, 0);
    CHECK_EQ(pip_draw_text(0x111, 0x122, (const char *)(uintptr_t)0x133, 0x144), RET);
    CHECK_CALL(PIP_SYS_DRAW_TEXT, 0x111, 0x122, 0x133, 0x144);
    CHECK_EQ(pip_clear(0x111), RET);
    CHECK_CALL(PIP_SYS_CLEAR, 0x111, 0, 0, 0);
    CHECK_EQ(pip_gfx_info((pip_gfx_info_t *)(uintptr_t)0x111), RET);
    CHECK_CALL(PIP_SYS_GFX_INFO, 0x111, 0, 0, 0);
    CHECK_EQ(pip_rect(0x111, 0x122, 0x133, 0x144), RET);
    CHECK_CALL(PIP_SYS_RECT, 0x111, 0x122, 0x133, 0x144);
    CHECK_EQ(pip_polyline((const int16_t *)(uintptr_t)0x111, 0x122, 0x133), RET);
    CHECK_CALL(PIP_SYS_POLYLINE, 0x111, 0x122, 0x133, 0);
    CHECK_EQ(pip_arc((const pip_arc_t *)(uintptr_t)0x111), RET);
    CHECK_CALL(PIP_SYS_ARC, 0x111, 0, 0, 0);
    CHECK_EQ(pip_blit_1bpp((const pip_blit_t *)(uintptr_t)0x111), RET);
    CHECK_CALL(PIP_SYS_BLIT_1BPP, 0x111, 0, 0, 0);
    CHECK_EQ(pip_blit_rgb((const pip_blit_t *)(uintptr_t)0x111), RET);
    CHECK_CALL(PIP_SYS_BLIT_RGB, 0x111, 0, 0, 0);
    CHECK_EQ(pip_set_clip(0x111, 0x122, 0x133), RET);
    CHECK_CALL(PIP_SYS_SET_CLIP, 0x111, 0x122, 0x133, 0);
    CHECK_EQ(pip_set_color(0x111, 0x122), RET);
    CHECK_CALL(PIP_SYS_SET_COLOR, 0x111, 0x122, 0, 0);
    CHECK_EQ(pip_text_metrics((const char *)(uintptr_t)0x111, 0x122), RET);
    CHECK_CALL(PIP_SYS_TEXT_METRICS, 0x111, 0x122, 0, 0);
    CHECK_EQ(pip_present(), RET);
    CHECK_CALL(PIP_SYS_PRESENT, 0, 0, 0, 0);
    CHECK_EQ(pip_set_font(0x111), RET);
    CHECK_CALL(PIP_SYS_SET_FONT, 0x111, 0, 0, 0);
    CHECK_EQ(pip_read_buttons(), RET);
    CHECK_CALL(PIP_SYS_READ_BUTTONS, 0, 0, 0, 0);
    CHECK_EQ(pip_read_dial(), RET);
    CHECK_CALL(PIP_SYS_READ_DIAL, 0, 0, 0, 0);
    CHECK_EQ(pip_poll_event((pip_event_t *)(uintptr_t)0x111), RET);
    CHECK_CALL(PIP_SYS_POLL_EVENT, 0x111, 0, 0, 0);
    CHECK_EQ(pip_wait_event((pip_event_t *)(uintptr_t)0x111, 0x122), RET);
    CHECK_CALL(PIP_SYS_WAIT_EVENT, 0x111, 0x122, 0, 0);
    CHECK_EQ(pip_tone(0x111, 0x122), RET);
    CHECK_CALL(PIP_SYS_TONE, 0x111, 0x122, 0, 0);
    CHECK_EQ(pip_sample((const uint8_t *)(uintptr_t)0x111, 0x122), RET);
    CHECK_CALL(PIP_SYS_SAMPLE, 0x111, 0x122, 0, 0);
    CHECK_EQ(pip_sample_queue((const uint8_t *)(uintptr_t)0x111, 0x122), RET);
    CHECK_CALL(PIP_SYS_SAMPLE_QUEUE, 0x111, 0x122, 0, 0);
    CHECK_EQ(pip_audio_stop(), RET);
    CHECK_CALL(PIP_SYS_AUDIO_STOP, 0, 0, 0, 0);
    CHECK_EQ(pip_volume(0x111), RET);
    CHECK_CALL(PIP_SYS_VOLUME, 0x111, 0, 0, 0);
    CHECK_EQ(pip_audio_status(), RET);
    CHECK_CALL(PIP_SYS_AUDIO_STATUS, 0, 0, 0, 0);
    CHECK_EQ(pip_play_seq((const pip_note_t *)(uintptr_t)0x111, 0x122), RET);
    CHECK_CALL(PIP_SYS_PLAY_SEQ, 0x111, 0x122, 0, 0);
    CHECK_EQ(pip_read_sensor(0x111, (uint32_t *)(uintptr_t)0x122), RET);
    CHECK_CALL(PIP_SYS_READ_SENSOR, 0x111, 0x122, 0, 0);
    CHECK_EQ(pip_sensor_available(0x111), RET);
    CHECK_CALL(PIP_SYS_SENSOR_AVAILABLE, 0x111, 0, 0, 0);
    CHECK_EQ(pip_get_time((uint32_t *)(uintptr_t)0x111), RET);
    CHECK_CALL(PIP_SYS_GET_TIME, 0x111, 0, 0, 0);
    CHECK_EQ(pip_get_battery(), RET);
    CHECK_CALL(PIP_SYS_GET_BATTERY, 0, 0, 0, 0);
    CHECK_EQ(pip_ticks_ms(), RET);
    CHECK_CALL(PIP_SYS_TICKS_MS, 0, 0, 0, 0);
    CHECK_EQ(pip_sleep_ms(0x111), RET);
    CHECK_CALL(PIP_SYS_SLEEP_MS, 0x111, 0, 0, 0);
    CHECK_EQ(pip_sys_info((pip_sys_info_t *)(uintptr_t)0x111), RET);
    CHECK_CALL(PIP_SYS_SYS_INFO, 0x111, 0, 0, 0);
    CHECK_EQ(pip_mem_info((pip_mem_info_t *)(uintptr_t)0x111), RET);
    CHECK_CALL(PIP_SYS_MEM_INFO, 0x111, 0, 0, 0);
    CHECK_EQ(pip_power(0x111), RET);
    CHECK_CALL(PIP_SYS_POWER, 0x111, 0, 0, 0);
    CHECK_EQ(pip_log((const char *)(uintptr_t)0x111), RET);
    CHECK_CALL(PIP_SYS_LOG, 0x111, 0, 0, 0);
    CHECK_EQ(pip_rand(), RET);
    CHECK_CALL(PIP_SYS_RAND, 0, 0, 0, 0);
    CHECK_EQ(pip_set_time(0x111), RET);
    CHECK_CALL(PIP_SYS_SET_TIME, 0x111, 0, 0, 0);
    CHECK_EQ(pip_read_save((const char *)(uintptr_t)0x111, (void *)(uintptr_t)0x122, 0x133), RET);
    CHECK_CALL(PIP_SYS_READ_SAVE, 0x111, 0x122, 0x133, 0);
    CHECK_EQ(pip_write_save((const char *)(uintptr_t)0x111, (const void *)(uintptr_t)0x122, 0x133), RET);
    CHECK_CALL(PIP_SYS_WRITE_SAVE, 0x111, 0x122, 0x133, 0);
    CHECK_EQ(pip_save_info((const char *)(uintptr_t)0x111, (uint32_t *)(uintptr_t)0x122), RET);
    CHECK_CALL(PIP_SYS_SAVE_INFO, 0x111, 0x122, 0, 0);
    CHECK_EQ(pip_save_delete((const char *)(uintptr_t)0x111), RET);
    CHECK_CALL(PIP_SYS_SAVE_DELETE, 0x111, 0, 0, 0);
    CHECK_EQ(pip_file_open((const char *)(uintptr_t)0x111), RET);
    CHECK_CALL(PIP_SYS_FILE_OPEN, 0x111, 0, 0, 0);
    CHECK_EQ(pip_file_read(0x111, (void *)(uintptr_t)0x122, 0x133), RET);
    CHECK_CALL(PIP_SYS_FILE_READ, 0x111, 0x122, 0x133, 0);
    CHECK_EQ(pip_file_close(0x111), RET);
    CHECK_CALL(PIP_SYS_FILE_CLOSE, 0x111, 0, 0, 0);
    CHECK_EQ(pip_dir_list((const char *)(uintptr_t)0x111, (pip_dirent_t *)(uintptr_t)0x122, 0x133, 0x144), RET);
    CHECK_CALL(PIP_SYS_DIR_LIST, 0x111, 0x122, 0x133, 0x144);
    CHECK_EQ(pip_holo_enumerate((pip_holo_info_t *)(uintptr_t)0x111, 0x122), RET);
    CHECK_CALL(PIP_SYS_HOLO_ENUMERATE, 0x111, 0x122, 0, 0);
    CHECK_EQ(pip_holo_launch((const char *)(uintptr_t)0x111, (const char *)(uintptr_t)0x122), RET);
    CHECK_CALL(PIP_SYS_HOLO_LAUNCH, 0x111, 0x122, 0, 0);
    CHECK_EQ(pip_holo_last_status(), RET);
    CHECK_CALL(PIP_SYS_HOLO_LAST_STATUS, 0, 0, 0, 0);
    CHECK_EQ(pip_holo_eject_event(), RET);
    CHECK_CALL(PIP_SYS_HOLO_EJECT_EVENT, 0, 0, 0, 0);
    CHECK_EQ(calls, PIP_SYSCALL_COUNT - 1);
}

TEST(exit_passes_status)
{
    calls = 0;
    if (setjmp(exit_jmp) == 0) {
        pip_exit(-3);
    }
    CHECK_EQ(calls, 1);
    CHECK_CALL(PIP_SYS_EXIT, 0xfffffffdu, 0, 0, 0);
}

TEST(packing_helpers)
{
    CHECK_EQ(pip_rect_xywh(-1, 2, 30, 40, PIP_RECT_FILLED), RET);
    CHECK_CALL(PIP_SYS_RECT, 0xffffffffu, 2, 0x001E0028u, 1);
    CHECK_EQ(pip_set_clip_xywh(5, -6, 320, 240), RET);
    CHECK_CALL(PIP_SYS_SET_CLIP, 5, 0xfffffffau, 0x014000F0u, 0);
}
