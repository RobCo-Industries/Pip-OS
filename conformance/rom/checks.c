/*
 * The automatic checks: one function per system call, in the order of
 * the syscall table of PIPOS_ABI.md. Each runs the call on its main path
 * and on the error paths the spec names. None needs a person.
 */
#include "conform.h"

#define PTR(a) ((void *)(uintptr_t)(a))

/* Read by the companion tape through its argument string. */
static const char rom_text[] = "conform: ROM slot text read by the tape\n";

static uint8_t big[PIP_SAMPLE_MAX + 1];

/* ---- graphics ------------------------------------------------------------ */

void check_draw_line(void)
{
    ck_eq("line_drawn", pip_draw_line(10, 10, 100, 60), 0);
    ck_eq("line_offscreen", pip_draw_line(-500, -500, 1000, 900), 0);
}

void check_draw_point(void)
{
    ck_eq("point_drawn", pip_draw_point(5, 5, 0xFFFFFF), 0);
    ck_eq("point_offscreen", pip_draw_point(-1, 400, 0xFFFFFF), 0);
}

void check_draw_text(void)
{
    ck_eq("text_width", pip_draw_text(0, 0, "ABC", 2), 3 * 8 * 2);
    ck_eq("text_scale9", pip_draw_text(0, 0, "ABC", 9), -PIP_EINVAL);
    ck_eq("text_scale_first", pip_draw_text(0, 0, PTR(BAD_KERNEL), 0), -PIP_EINVAL);
    ck_eq("text_bad_ptr", pip_draw_text(0, 0, PTR(BAD_KERNEL), 1), -PIP_EFAULT);
}

void check_clear(void)
{
    ck_eq("clear_ok", pip_clear(0x000000), 0);
}

void check_gfx_info(void)
{
    pip_gfx_info_t gi;
    int32_t rc = pip_gfx_info(&gi);

    ck_eq("gfx_info_rc", rc, 0);
    ck("gfx_info_fields", rc == 0 && gi.width == PIP_SCREEN_W && gi.height == PIP_SCREEN_H &&
       gi.format == PIP_GFX_FORMAT_XRGB && gi.backend <= PIP_GFX_BACKEND_SPI &&
       (gi.fg_default >> 24) == 0);
    ck_eq("gfx_info_bad_ptr", pip_gfx_info(PTR(BAD_KERNEL)), -PIP_EFAULT);
}

void check_rect(void)
{
    ck_eq("rect_filled", pip_rect_xywh(20, 20, 30, 20, PIP_RECT_FILLED), 0);
    ck_eq("rect_outline", pip_rect_xywh(60, 20, 30, 20, PIP_RECT_OUTLINE), 0);
    ck_eq("rect_zero_size", pip_rect_xywh(0, 0, 0, 10, PIP_RECT_FILLED), 0);
    ck_eq("rect_bad_fill", pip_rect_xywh(0, 0, 4, 4, 2), -PIP_EINVAL);
}

void check_polyline(void)
{
    static const int16_t tri[] = { 100, 100, 140, 100, 120, 70 };

    ck_eq("poly_closed", pip_polyline(tri, 3, 1), 0);
    ck_eq("poly_count1", pip_polyline(tri, 1, 0), -PIP_EINVAL);
    ck_eq("poly_count257", pip_polyline(tri, PIP_POLY_MAX + 1, 0), -PIP_EINVAL);
    ck_eq("poly_bad_closed", pip_polyline(tri, 3, 2), -PIP_EINVAL);
    ck_eq("poly_bad_ptr", pip_polyline(PTR(BAD_KERNEL), 3, 0), -PIP_EFAULT);
}

void check_arc(void)
{
    pip_arc_t a = { 200, 120, 30, 0, 360, PIP_ARC_OUTLINE };

    ck_eq("arc_circle", pip_arc(&a), 0);
    a.radius = 0;
    ck_eq("arc_radius0", pip_arc(&a), -PIP_EINVAL);
    a.radius = 10;
    a.start_deg = 360;
    ck_eq("arc_start360", pip_arc(&a), -PIP_EINVAL);
    ck_eq("arc_null", pip_arc(PTR(0)), -PIP_EFAULT);
}

void check_blit_1bpp(void)
{
    static const uint8_t bits[2] = { 0xA5, 0x5A };
    pip_blit_t b = { 10, 200, 8, 2, 1, 0, (uint32_t)(uintptr_t)bits, 0xFFFFFF };

    ck_eq("blit1_drawn", pip_blit_1bpp(&b), 0);
    b.stride = 0;
    ck_eq("blit1_short_stride", pip_blit_1bpp(&b), -PIP_EINVAL);
    b.stride = 1;
    b.data = BAD_KERNEL;
    ck_eq("blit1_bad_data", pip_blit_1bpp(&b), -PIP_EFAULT);
}

void check_blit_rgb(void)
{
    static const uint32_t px[4] = { 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFFFF };
    pip_blit_t b = { 30, 200, 2, 2, 8, 0, (uint32_t)(uintptr_t)px, 0 };

    ck_eq("blitrgb_drawn", pip_blit_rgb(&b), 0);
    b.flags = 1;
    ck_eq("blitrgb_bad_flags", pip_blit_rgb(&b), -PIP_EINVAL);
    b.flags = 0;
    b.data = BAD_STRADDLE;
    ck_eq("blitrgb_straddle", pip_blit_rgb(&b), -PIP_EFAULT);
}

void check_set_clip(void)
{
    ck_eq("clip_set", pip_set_clip_xywh(10, 10, 100, 100), 0);
    ck_eq("clip_zero_width", pip_set_clip(10, 10, PIP_WH(0, 5)), -PIP_EINVAL);
    ck_eq("clip_reset", pip_set_clip(0, 0, 0), 0);
}

void check_set_color(void)
{
    ck_eq("color_set", pip_set_color(0xFFFFFF, PIP_COLOR_NONE), 0);
}

void check_text_metrics(void)
{
    ck_eq("metrics_value", pip_text_metrics("Hi", 3), (int32_t)((48u << 16) | 48u));
    ck_eq("metrics_scale0", pip_text_metrics("Hi", 0), -PIP_EINVAL);
    ck_eq("metrics_bad_ptr", pip_text_metrics(PTR(BAD_KERNEL), 1), -PIP_EFAULT);
}

void check_present(void)
{
    int32_t rc = pip_present();

    ck_res("present_ok", rc == 0 ? R_PASS : rc == -PIP_ENODEV ? R_ABSENT : R_FAIL, 1, rc, 0);
}

void check_set_font(void)
{
    ck_eq("font_system", pip_set_font(PIP_FONT_SYSTEM), 0);
    ck_eq("font_other", pip_set_font(1), -PIP_EINVAL);
}

/* ---- input ------------------------------------------------------------- */

void check_read_buttons(void)
{
    int32_t b = pip_read_buttons();

    ck_res("buttons_mask", b >= 0 && b < (1 << PIP_BTN_COUNT) ? R_PASS : R_FAIL, 1, b, 0);
}

void check_read_dial(void)
{
    int32_t d = pip_read_dial();

    ck_res("dial_range", d >= 0 && d <= PIP_DIAL_MAX && d % PIP_DIAL_DETENT_DEG == 0 ?
           R_PASS : R_FAIL, 1, d, 0);
}

void check_poll_event(void)
{
    pip_event_t ev;

    drain_events();
    ck_eq("poll_empty", pip_poll_event(&ev), 0);
    ck_eq("poll_bad_ptr", pip_poll_event(PTR(BAD_KERNEL)), -PIP_EFAULT);
}

/* The design guarantees a wait of at least timeout_ms. A stalled host only makes it longer. */
void check_wait_event(void)
{
    pip_event_t ev;
    int32_t t0, rc, dt;

    drain_events();
    ck_eq("wait_zero", pip_wait_event(&ev, 0), -PIP_ETIMEDOUT);
    t0 = pip_ticks_ms();
    rc = pip_wait_event(&ev, 100);
    dt = elapsed_ms(t0, pip_ticks_ms());
    ck_eq("wait_100_rc", rc, -PIP_ETIMEDOUT);
    ck_res("wait_100_time", dt >= 100 && dt < 5000 ? R_PASS : R_FAIL, 1, dt, 100);
    ck_eq("wait_range", pip_wait_event(&ev, 0x80000000u), -PIP_ERANGE);
}

/* ---- audio ------------------------------------------------------------- */

/* Polls audio_status until it reads 0, at most ms. Returns the time it took, or -1. */
static int32_t wait_silent(int32_t ms)
{
    int32_t t0 = pip_ticks_ms();

    while (pip_audio_status() != 0) {
        if (elapsed_ms(t0, pip_ticks_ms()) > ms) {
            return -1;
        }
        pip_sleep_ms(5);
    }
    return elapsed_ms(t0, pip_ticks_ms());
}

void check_tone(void)
{
    ck_eq("tone_ok", pip_tone(880, 2000), 0);
    ck_eq("tone_playing", pip_audio_status(), 1);
    ck_eq("tone_freq19", pip_tone(19, 100), -PIP_EINVAL);
    ck_eq("tone_ms0", pip_tone(440, 0), -PIP_EINVAL);
    /* A call refused for its arguments leaves the current sound playing. */
    ck_eq("tone_refused_keeps", pip_audio_status(), 1);
    pip_audio_stop();
}

void check_sample(void)
{
    uint32_t i;

    for (i = 0; i < 4000; i++) {
        big[i] = (uint8_t)((i / 10) % 2 ? 160 : 96);   /* 400 Hz square, half volume */
    }
    ck_eq("sample_ok", pip_sample(big, 4000), 0);
    ck_eq("sample_playing", pip_audio_status(), 1);
    ck_eq("sample_len0", pip_sample(big, 0), -PIP_EINVAL);
    ck_eq("sample_len_first", pip_sample(PTR(BAD_KERNEL), PIP_SAMPLE_MAX + 1), -PIP_EINVAL);
    ck_eq("sample_bad_ptr", pip_sample(PTR(BAD_KERNEL), 100), -PIP_EFAULT);
    pip_audio_stop();
}

void check_audio_stop(void)
{
    pip_tone(440, 2000);
    ck_eq("stop_ok", pip_audio_stop(), 0);
    ck_eq("stop_silent", pip_audio_status(), 0);
    ck_eq("stop_idle", pip_audio_stop(), 0);
}

void check_volume(void)
{
    int32_t prev = pip_volume(40);

    ck_res("volume_prev", prev >= 0 && prev <= PIP_VOLUME_MAX ? R_PASS : R_FAIL, 1, prev, 0);
    ck_eq("volume_reads_back", pip_volume(40), 40);
    ck_eq("volume_101", pip_volume(101), -PIP_EINVAL);
    /* At volume 0 a sound still plays and audio_status reports it. */
    pip_volume(0);
    pip_tone(440, 2000);
    ck_eq("volume0_status", pip_audio_status(), 1);
    pip_audio_stop();
    if (prev >= 0) {
        pip_volume((uint32_t)prev);
    }
}

/* A 100 ms tone ends by itself. Upper bound for a stalled host: 3 s. */
void check_audio_status(void)
{
    int32_t t;

    pip_audio_stop();
    ck_eq("status_idle", pip_audio_status(), 0);
    pip_tone(1000, 100);
    t = wait_silent(3000);
    ck_res("status_ends", t >= 0 ? R_PASS : R_FAIL, 1, t, 100);
}

void check_play_seq(void)
{
    static pip_note_t notes[3] = { { 523, 300 }, { PIP_NOTE_REST, 50 }, { 659, 300 } };
    static const pip_note_t bad[2] = { { 523, 100 }, { 10, 100 } };

    ck_eq("seq_ok", pip_play_seq(notes, 3), 0);
    ck_eq("seq_playing", pip_audio_status(), 1);
    pip_audio_stop();
    ck_eq("seq_count0", pip_play_seq(notes, 0), -PIP_EINVAL);
    ck_eq("seq_bad_note", pip_play_seq(bad, 2), -PIP_EINVAL);
    ck_eq("seq_bad_plays_none", pip_audio_status(), 0);
    ck_eq("seq_null", pip_play_seq(PTR(0), 2), -PIP_EFAULT);
}

/*
 * The first sample lasts 2 s, so the calls after it run while it plays even
 * on a stalled host. The kernel checks the arguments before the queue
 * state: errors win over -PIP_EBUSY.
 */
void check_sample_queue(void)
{
    uint32_t i;

    for (i = 0; i < 16000; i++) {
        big[i] = (uint8_t)((i / 10) % 2 ? 160 : 96);   /* 400 Hz square, half volume */
    }
    pip_audio_stop();
    ck_eq("squeue_starts", pip_sample_queue(big, 16000), 0);
    ck_eq("squeue_playing", pip_audio_status(), 1);
    ck_eq("squeue_chained", pip_sample_queue(big, 800), 0);
    ck_eq("squeue_busy", pip_sample_queue(big, 800), -PIP_EBUSY);
    ck_eq("squeue_len0", pip_sample_queue(big, 0), -PIP_EINVAL);
    ck_eq("squeue_len65537", pip_sample_queue(big, PIP_SAMPLE_MAX + 1), -PIP_EINVAL);
    ck_eq("squeue_bad_ptr", pip_sample_queue(PTR(BAD_KERNEL), 100), -PIP_EFAULT);
    ck_eq("squeue_still_busy", pip_sample_queue(big, 800), -PIP_EBUSY);
    /* audio_stop drops the playing and the waiting sample. */
    ck_eq("squeue_stop", pip_audio_stop(), 0);
    ck_eq("squeue_stop_silent", pip_audio_status(), 0);
    ck_eq("squeue_restarts", pip_sample_queue(big, 16000), 0);
    ck_eq("squeue_restart_chains", pip_sample_queue(big, 800), 0);
    ck_eq("squeue_restart_busy", pip_sample_queue(big, 800), -PIP_EBUSY);
    pip_audio_stop();
}

/* ---- sensors, time, system --------------------------------------------- */

static const char *const sensor_names[PIP_SENSOR_COUNT] = {
    "sensor_rtc", "sensor_battery", "sensor_geiger", "sensor_gps",
    "sensor_accel", "sensor_temp", "sensor_heart",
};

void check_read_sensor(void)
{
    uint32_t v, t;
    int32_t rc, rt;
    unsigned s;

    /* Sensor 0 answers like get_time. ABSENT only for -PIP_ENODEV, the code for no clock. */
    rc = pip_read_sensor(PIP_SENSOR_RTC, &v);
    rt = pip_get_time(&t);
    ck_res(sensor_names[0], rc != rt ? R_FAIL : rc == 0 ? R_PASS :
           rc == -PIP_ENODEV ? R_ABSENT : R_FAIL, 1, rc, rc != rt ? rt : -PIP_ENODEV);
    for (s = 1; s < PIP_SENSOR_COUNT; s++) {
        int32_t avail = pip_sensor_available(s);

        rc = pip_read_sensor(s, &v);
        if (avail == 1) {
            ck_res(sensor_names[s], rc == 0 ? R_PASS : R_FAIL, 1, rc, 0);
        } else {
            ck_res(sensor_names[s], avail == 0 && rc == -PIP_ENODEV ? R_ABSENT : R_FAIL, 1, rc,
                   -PIP_ENODEV);
        }
    }
    ck_eq("sensor_7", pip_read_sensor(PIP_SENSOR_COUNT, &v), -PIP_EINVAL);
    ck_eq("sensor_bad_ptr", pip_read_sensor(PIP_SENSOR_GEIGER, PTR(BAD_KERNEL)), -PIP_EFAULT);
}

void check_sensor_available(void)
{
    uint32_t v;

    ck_eq("available_rtc", pip_sensor_available(PIP_SENSOR_RTC),
          pip_read_sensor(PIP_SENSOR_RTC, &v) == 0);
    ck_eq("available_7", pip_sensor_available(PIP_SENSOR_COUNT), -PIP_EINVAL);
}

void check_get_time(void)
{
    uint32_t t = 0;
    int32_t rc = pip_get_time(&t);

    ck_res("time_at_boot", rc == 0 ? R_PASS : rc == -PIP_ENODEV ? R_ABSENT : R_FAIL, 1, rc, 0);
    ck_eq("time_bad_ptr", pip_get_time(PTR(BAD_KERNEL)), -PIP_EFAULT);
}

void check_get_battery(void)
{
    int32_t b = pip_get_battery();

    ck_res("battery", b >= 0 && b <= 100 ? R_PASS : b == -PIP_ENODEV ? R_ABSENT : R_FAIL, 1, b,
           -PIP_ENODEV);
}

void check_ticks_ms(void)
{
    int32_t a = pip_ticks_ms(), b;

    pip_sleep_ms(20);
    b = pip_ticks_ms();
    ck_res("ticks_range", a >= 0 && b >= 0 ? R_PASS : R_FAIL, 1, a, 0);
    ck_res("ticks_advance", elapsed_ms(a, b) >= 20 ? R_PASS : R_FAIL, 1, elapsed_ms(a, b), 20);
}

/* sleep_ms sleeps at least ms. The upper bound only catches a hang: 5 s. */
void check_sleep_ms(void)
{
    int32_t a = pip_ticks_ms(), rc, dt;

    rc = pip_sleep_ms(50);
    dt = elapsed_ms(a, pip_ticks_ms());
    ck_eq("sleep_rc", rc, 0);
    ck_res("sleep_50", dt >= 50 && dt < 5000 ? R_PASS : R_FAIL, 1, dt, 50);
    ck_eq("sleep_range", pip_sleep_ms(0x80000000u), -PIP_ERANGE);
}

/* exit ends the run, so the companion tape exercises it. */
void check_exit(void)
{
    ck_eq("exit_low8", pip_holo_launch(TAPE_FILE, "exit 300"), 300 & 0xFF);
    ck_eq("exit_zero", pip_holo_launch(TAPE_FILE, "exit 0"), 0);
}

void check_sys_info(void)
{
    pip_sys_info_t si;
    int32_t rc = pip_sys_info(&si);

    ck_eq("sysinfo_rc", rc, 0);
    ck("sysinfo_fields", rc == 0 && si.size == 32 && si.abi_version == PIP_ABI_VERSION &&
       si.mem_size != 0 && si.reserved[0] == 0 && si.reserved[1] == 0);
    ck_eq("sysinfo_bad_ptr", pip_sys_info(PTR(BAD_KERNEL)), -PIP_EFAULT);
    if (rc == 0) {
        pip_printf("conform: board_rev 0x%08x soc 0x%x mem %u pipos 0x%06x\n",
                   (unsigned)si.board_rev, (unsigned)si.soc, (unsigned)si.mem_size,
                   (unsigned)si.pipos_version);
    }
}

void check_mem_info(void)
{
    pip_mem_info_t mi;
    int32_t rc = pip_mem_info(&mi);

    ck("meminfo_rom", rc == 0 && mi.heap_base == PIP_ROM_HEAP_BASE &&
       mi.heap_size == PIP_ROM_HEAP_SIZE && mi.stack_top == PIP_ROM_STACK_TOP &&
       mi.stack_size == PIP_STACK_SIZE);
    ck_eq("meminfo_straddle", pip_mem_info(PTR(BAD_STRADDLE)), -PIP_EFAULT);
}

/* Reboot and halt end the run: only the screen ops are called. */
void check_power(void)
{
    ck_eq("power_screen_off", pip_power(PIP_POWER_SCREEN_OFF), 0);
    pip_sleep_ms(100);
    ck_eq("power_screen_on", pip_power(PIP_POWER_SCREEN_ON), 0);
    ck_eq("power_bad_op", pip_power(4), -PIP_EINVAL);
}

void check_log(void)
{
    static char full[PIP_LOG_MAX];

    ck_eq("log_ok", pip_log("conform: log line\n"), 0);
    memset(full, 'x', sizeof(full));
    ck_eq("log_no_nul", pip_log(full), -PIP_EINVAL);
    ck_eq("log_bad_ptr", pip_log(PTR(BAD_KERNEL)), -PIP_EFAULT);
}

void check_rand(void)
{
    int32_t first = pip_rand(), v = first;
    int i, ok = first >= 0, differ = 0;

    for (i = 0; i < 16 && ok; i++) {
        v = pip_rand();
        ok = v >= 0;
        differ |= v != first;
    }
    ck_res("rand_range", ok ? R_PASS : R_FAIL, 1, v, 0);
    ck("rand_varies", differ);
}

/* The software clock only: Pip-OS never writes the chip. The old time comes back. */
void check_set_time(void)
{
    uint32_t before = 0, now = 0;
    int had = pip_get_time(&before) == 0;
    int32_t t0 = pip_ticks_ms();

    ck_eq("set_time_ok", pip_set_time(1700000000u), 0);
    ck_eq("set_time_reads", pip_get_time(&now), 0);
    ck_res("set_time_value", now >= 1700000000u && now < 1700000060u ? R_PASS : R_FAIL, 1,
           (int32_t)(now - 1700000000u), 0);
    if (had) {
        pip_set_time(before + (uint32_t)elapsed_ms(t0, pip_ticks_ms()) / 1000u);
    }
}

/* ---- saves ------------------------------------------------------------- */

static uint8_t save_data[300];
static uint8_t save_back[300];

static void fill_save(void)
{
    size_t i;

    for (i = 0; i < sizeof(save_data); i++) {
        save_data[i] = (uint8_t)(i * 7 + 1);
    }
}

void check_read_save(void)
{
    int32_t rc;

    fill_save();
    pip_write_save(SAVE_NAME, save_data, sizeof(save_data));
    memset(save_back, 0, sizeof(save_back));
    rc = pip_read_save(SAVE_NAME, save_back, sizeof(save_back));
    ck_eq("save_read", rc, (int32_t)sizeof(save_data));
    ck("save_read_same", memcmp(save_back, save_data, sizeof(save_data)) == 0);
    ck_eq("save_read_missing", pip_read_save("CONFNONE", save_back, 4), -PIP_ENOENT);
    ck_eq("save_read_straddle", pip_read_save(SAVE_NAME, PTR(BAD_STRADDLE), 8), -PIP_EFAULT);
}

void check_write_save(void)
{
    /* A name with no NUL within 9 bytes. */
    static const char no_nul[12] = { 'C', 'O', 'N', 'F', 'T', 'E', 'S', 'T', 'X', 'Y', 'Z', 'W' };

    fill_save();
    ck_eq("save_write", pip_write_save(SAVE_NAME, save_data, sizeof(save_data)),
          (int32_t)sizeof(save_data));
    ck_eq("save_too_big", pip_write_save(SAVE_NAME, big, PIP_SAVE_MAX + 1), -PIP_ERANGE);
    ck_eq("save_bad_name", pip_write_save("bad name", save_data, 4), -PIP_EINVAL);
    ck_eq("save_name_no_nul", pip_write_save(no_nul, save_data, 4), -PIP_EINVAL);
}

void check_save_info(void)
{
    uint32_t size = 0;
    int32_t rc = pip_save_info(SAVE_NAME, &size);

    ck_eq("save_info_rc", rc, 0);
    ck_eq("save_info_size", (int32_t)size, (int32_t)sizeof(save_data));
    ck_eq("save_info_missing", pip_save_info("CONFNONE", &size), -PIP_ENOENT);
}

void check_save_delete(void)
{
    uint32_t size;

    ck_eq("save_delete", pip_save_delete(SAVE_NAME), 0);
    ck_eq("save_gone", pip_save_info(SAVE_NAME, &size), -PIP_ENOENT);
    ck_eq("save_delete_missing", pip_save_delete(SAVE_NAME), -PIP_ENOENT);
}

/* ---- files ------------------------------------------------------------- */

/* The size of file name in /PIPBOY/HOLO from dir_list, or -1. */
static int32_t holo_file_size(const char *name)
{
    static pip_dirent_t d[PIP_DIR_LIST_MAX];
    int32_t n = pip_dir_list("/PIPBOY/HOLO", d, PIP_DIR_LIST_MAX, 0), i;

    n = n > PIP_DIR_LIST_MAX ? PIP_DIR_LIST_MAX : n;   /* a wrong count reads no further */

    for (i = 0; i < n; i++) {
        if (strcmp(d[i].name, name) == 0 && !(d[i].flags & PIP_DIRENT_DIR)) {
            return (int32_t)d[i].size;
        }
    }
    return -1;
}

static int32_t tape_size(void)
{
    return holo_file_size(TAPE_FILE);
}

void check_file_open(void)
{
    int32_t h[PIP_FILES_MAX], extra, i, opened = 0;

    for (i = 0; i < PIP_FILES_MAX; i++) {
        h[i] = pip_file_open(TAPE_PATH);
        opened += h[i] == i;
    }
    ck_eq("open_handles", opened, PIP_FILES_MAX);
    extra = pip_file_open(TAPE_PATH);
    ck_eq("open_ninth", extra, -PIP_ENOMEM);
    for (i = 0; i < PIP_FILES_MAX; i++) {
        if (h[i] >= 0) {
            pip_file_close(h[i]);
        }
    }
    ck_eq("open_lower_case", pip_file_open("/pipboy/holo/conftape.htp"), 0);
    pip_file_close(0);
    ck_eq("open_missing", pip_file_open("/PIPBOY/NOFILE.TXT"), -PIP_ENOENT);
    ck_eq("open_relative", pip_file_open("PIPBOY/PIPOS.CFG"), -PIP_EINVAL);
    ck_eq("open_save_dir", pip_file_open("/PIPBOY/SAVE/CONFTEST.SAV"), -PIP_EPERM);
    ck_eq("open_folder", pip_file_open("/PIPBOY/HOLO"), -PIP_EINVAL);
}

void check_file_read(void)
{
    static uint8_t buf[512];
    int32_t size = tape_size();
    int32_t h = pip_file_open(TAPE_PATH), n, total = 0, rc = 0;
    int magic_ok = 0, reads = 0;

    if (h < 0) {
        ck_res("read_whole", R_FAIL, 1, h, 0);
        return;
    }
    /* At most 8 MiB: a kernel that never reports the end cannot hang the run. */
    while ((n = pip_file_read(h, buf, sizeof(buf))) > 0 && ++reads < 16384) {
        if (total == 0 && n >= 0x28) {
            magic_ok = memcmp(buf + 0x20, "ROBCO78", 8) == 0;
        }
        total += n;
    }
    rc = n;
    ck_eq("read_whole", total, size);
    ck("read_magic", magic_ok);
    ck_eq("read_eof", rc, 0);
    ck_eq("read_size0", pip_file_read(h, buf, 0), 0);
    ck_eq("read_bad_buf", pip_file_read(h, PTR(BAD_KERNEL), 4), -PIP_EFAULT);
    pip_file_close(h);
    ck_eq("read_closed", pip_file_read(h, buf, 4), -PIP_EBADF);
}

void check_file_close(void)
{
    int32_t h = pip_file_open(TAPE_PATH);

    ck_eq("close_ok", pip_file_close(h), 0);
    ck_eq("close_twice", pip_file_close(h), -PIP_EBADF);
    ck_eq("close_bad", pip_file_close(PIP_FILES_MAX), -PIP_EBADF);
}

void check_dir_list(void)
{
    static pip_dirent_t d[4];

    ck_res("dir_tape", tape_size() > 0 ? R_PASS : R_FAIL, 1, tape_size(), 0);
    ck_res("dir_root", pip_dir_list("/", d, 4, 0) >= 1 ? R_PASS : R_FAIL, 0, 0, 0);
    ck_eq("dir_past_end", pip_dir_list("/PIPBOY/HOLO", d, 4, 1000), 0);
    ck_eq("dir_max0", pip_dir_list("/PIPBOY", d, 0, 0), -PIP_EINVAL);
    ck_eq("dir_file", pip_dir_list(TAPE_PATH, d, 4, 0), -PIP_EINVAL);
    ck_eq("dir_bad_out", pip_dir_list("/PIPBOY", PTR(BAD_STRADDLE), 1, 0), -PIP_EFAULT);
}

/* ---- holotapes --------------------------------------------------------- */

void check_holo_enumerate(void)
{
    static pip_holo_info_t hi[PIP_HOLO_ENUM_MAX];
    int32_t n = pip_holo_enumerate(hi, PIP_HOLO_ENUM_MAX), i;
    int tape = -1, data = -1;

    n = n > PIP_HOLO_ENUM_MAX ? PIP_HOLO_ENUM_MAX : n;

    for (i = 0; i < n; i++) {
        if (strcmp(hi[i].file, TAPE_FILE) == 0) {
            tape = hi[i].subtype;
        } else if (strcmp(hi[i].file, DATA_FILE) == 0) {
            data = hi[i].subtype;
        }
    }
    ck_res("enum_tape", tape == PIP_HOLO_GAME ? R_PASS : R_FAIL, 1, tape, PIP_HOLO_GAME);
    /* dir_list, not holo_enumerate, says whether the data tape is on the card. */
    if (holo_file_size(DATA_FILE) < 0) {
        ck_res("enum_data_tape", data < 0 ? R_ABSENT : R_FAIL, 1, data, -1);
    } else {
        ck_res("enum_data_tape", data == PIP_HOLO_DATA ? R_PASS : R_FAIL, 1, data, PIP_HOLO_DATA);
    }
    ck_eq("enum_max0", pip_holo_enumerate(hi, 0), -PIP_EINVAL);
    ck_eq("enum_max33", pip_holo_enumerate(hi, PIP_HOLO_ENUM_MAX + 1), -PIP_EINVAL);
    ck_eq("enum_null", pip_holo_enumerate(PTR(0), 1), -PIP_EFAULT);
}

void check_holo_launch(void)
{
    static char args[PIP_ARGS_SIZE + 4];
    int32_t rc;

    pip_snprintf(args, sizeof(args), "check 0x%08x", (unsigned)(uintptr_t)rom_text);
    ck_eq("launch_tape", pip_holo_launch(TAPE_FILE, args), TAPE_OK);
    ck_eq("launch_lower_case", pip_holo_launch("conftape.htp", "exit 7"), 7);
    /* A tape that writes to the ROM slot is killed; the ROM goes on. */
    ck_eq("launch_fault", pip_holo_launch(TAPE_FILE, "fault"), -PIP_EFAULT);
    ck_eq("launch_after_fault", pip_holo_launch(TAPE_FILE, "exit 5"), 5);
    /* Runs whenever the file is on the card, whatever holo_enumerate says. */
    if (holo_file_size(DATA_FILE) >= 0) {
        ck_eq("launch_data_tape", pip_holo_launch(DATA_FILE, "exit 1"), -PIP_ENOEXEC);
    } else {
        ck_res("launch_data_tape", R_ABSENT, 0, 0, 0);
    }
    ck_eq("launch_missing", pip_holo_launch("NOTAPE.HTP", ""), -PIP_ENOENT);
    ck_eq("launch_bad_name", pip_holo_launch("A B.HTP", ""), -PIP_EINVAL);
    ck_eq("launch_null_name", pip_holo_launch(PTR(0), ""), -PIP_EFAULT);
    /* 100 bytes and no NUL: the limit is 99 bytes before the NUL. */
    memset(args, 'a', sizeof(args));
    ck_eq("launch_args_no_nul", pip_holo_launch(TAPE_FILE, args), -PIP_EINVAL);
    args[PIP_ARGS_MAX] = '\0';
    memcpy(args, "exit 9 ", 7);
    rc = pip_holo_launch(TAPE_FILE, args);
    ck_eq("launch_args_99", rc, 9);
}

void check_holo_last_status(void)
{
    ck_eq("last_before_launch", first_last_status, -PIP_ENOENT);
    pip_holo_launch(TAPE_FILE, "exit 3");
    ck_eq("last_status", pip_holo_last_status(), 3);
    pip_holo_launch("NOTAPE.HTP", "");
    ck_eq("last_error", pip_holo_last_status(), -PIP_ENOENT);
}

void check_holo_eject_event(void)
{
    ck_eq("eject_none", pip_holo_eject_event(), 0);
}

/* ---- the rest ---------------------------------------------------------- */

void check_unknown(void)
{
    ck_eq("unknown_number", pip_syscall(0x7F, 0, 0, 0, 0), -PIP_ENOSYS);
    ck_eq("unknown_gap", pip_syscall(0x14, 0, 0, 0, 0), -PIP_ENOSYS);
}
