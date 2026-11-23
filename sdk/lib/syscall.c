/* System call wrappers: one pip_syscall(), so one `svc #0`, each. */
#include <pipos/syscall.h>

#define U(v) ((uint32_t)(v))
#define P(p) ((uint32_t)(uintptr_t)(p))

int32_t pip_draw_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    return pip_syscall(PIP_SYS_DRAW_LINE, U(x0), U(y0), U(x1), U(y1));
}

int32_t pip_draw_point(int32_t x, int32_t y, uint32_t color)
{
    return pip_syscall(PIP_SYS_DRAW_POINT, U(x), U(y), U(color), 0);
}

int32_t pip_draw_text(int32_t x, int32_t y, const char *text, uint32_t scale)
{
    return pip_syscall(PIP_SYS_DRAW_TEXT, U(x), U(y), P(text), U(scale));
}

int32_t pip_clear(uint32_t color)
{
    return pip_syscall(PIP_SYS_CLEAR, U(color), 0, 0, 0);
}

int32_t pip_gfx_info(pip_gfx_info_t *out)
{
    return pip_syscall(PIP_SYS_GFX_INFO, P(out), 0, 0, 0);
}

int32_t pip_rect(int32_t x, int32_t y, uint32_t wh, uint32_t fill)
{
    return pip_syscall(PIP_SYS_RECT, U(x), U(y), U(wh), U(fill));
}

int32_t pip_polyline(const int16_t *xy, uint32_t count, uint32_t closed)
{
    return pip_syscall(PIP_SYS_POLYLINE, P(xy), U(count), U(closed), 0);
}

int32_t pip_arc(const pip_arc_t *a)
{
    return pip_syscall(PIP_SYS_ARC, P(a), 0, 0, 0);
}

int32_t pip_blit_1bpp(const pip_blit_t *b)
{
    return pip_syscall(PIP_SYS_BLIT_1BPP, P(b), 0, 0, 0);
}

int32_t pip_blit_rgb(const pip_blit_t *b)
{
    return pip_syscall(PIP_SYS_BLIT_RGB, P(b), 0, 0, 0);
}

int32_t pip_set_clip(int32_t x, int32_t y, uint32_t wh)
{
    return pip_syscall(PIP_SYS_SET_CLIP, U(x), U(y), U(wh), 0);
}

int32_t pip_set_color(uint32_t fg, uint32_t bg)
{
    return pip_syscall(PIP_SYS_SET_COLOR, U(fg), U(bg), 0, 0);
}

int32_t pip_text_metrics(const char *text, uint32_t scale)
{
    return pip_syscall(PIP_SYS_TEXT_METRICS, P(text), U(scale), 0, 0);
}

int32_t pip_present(void)
{
    return pip_syscall(PIP_SYS_PRESENT, 0, 0, 0, 0);
}

int32_t pip_set_font(uint32_t font_id)
{
    return pip_syscall(PIP_SYS_SET_FONT, U(font_id), 0, 0, 0);
}

int32_t pip_read_buttons(void)
{
    return pip_syscall(PIP_SYS_READ_BUTTONS, 0, 0, 0, 0);
}

int32_t pip_read_dial(void)
{
    return pip_syscall(PIP_SYS_READ_DIAL, 0, 0, 0, 0);
}

int32_t pip_poll_event(pip_event_t *out)
{
    return pip_syscall(PIP_SYS_POLL_EVENT, P(out), 0, 0, 0);
}

int32_t pip_wait_event(pip_event_t *out, uint32_t timeout_ms)
{
    return pip_syscall(PIP_SYS_WAIT_EVENT, P(out), U(timeout_ms), 0, 0);
}

int32_t pip_tone(uint32_t freq_hz, uint32_t duration_ms)
{
    return pip_syscall(PIP_SYS_TONE, U(freq_hz), U(duration_ms), 0, 0);
}

int32_t pip_sample(const uint8_t *data, uint32_t length)
{
    return pip_syscall(PIP_SYS_SAMPLE, P(data), U(length), 0, 0);
}

int32_t pip_audio_stop(void)
{
    return pip_syscall(PIP_SYS_AUDIO_STOP, 0, 0, 0, 0);
}

int32_t pip_volume(uint32_t level)
{
    return pip_syscall(PIP_SYS_VOLUME, U(level), 0, 0, 0);
}

int32_t pip_audio_status(void)
{
    return pip_syscall(PIP_SYS_AUDIO_STATUS, 0, 0, 0, 0);
}

int32_t pip_play_seq(const pip_note_t *notes, uint32_t count)
{
    return pip_syscall(PIP_SYS_PLAY_SEQ, P(notes), U(count), 0, 0);
}

int32_t pip_sample_queue(const uint8_t *data, uint32_t length)
{
    return pip_syscall(PIP_SYS_SAMPLE_QUEUE, P(data), U(length), 0, 0);
}

int32_t pip_read_sensor(uint32_t sensor, uint32_t *value)
{
    return pip_syscall(PIP_SYS_READ_SENSOR, U(sensor), P(value), 0, 0);
}

int32_t pip_sensor_available(uint32_t sensor)
{
    return pip_syscall(PIP_SYS_SENSOR_AVAILABLE, U(sensor), 0, 0, 0);
}

int32_t pip_get_time(uint32_t *unix_seconds)
{
    return pip_syscall(PIP_SYS_GET_TIME, P(unix_seconds), 0, 0, 0);
}

int32_t pip_get_battery(void)
{
    return pip_syscall(PIP_SYS_GET_BATTERY, 0, 0, 0, 0);
}

int32_t pip_ticks_ms(void)
{
    return pip_syscall(PIP_SYS_TICKS_MS, 0, 0, 0, 0);
}

int32_t pip_sleep_ms(uint32_t ms)
{
    return pip_syscall(PIP_SYS_SLEEP_MS, U(ms), 0, 0, 0);
}

void pip_exit(int32_t status)
{
    pip_syscall(PIP_SYS_EXIT, U(status), 0, 0, 0);
    for (;;) {
    }
}

int32_t pip_sys_info(pip_sys_info_t *out)
{
    return pip_syscall(PIP_SYS_SYS_INFO, P(out), 0, 0, 0);
}

int32_t pip_mem_info(pip_mem_info_t *out)
{
    return pip_syscall(PIP_SYS_MEM_INFO, P(out), 0, 0, 0);
}

int32_t pip_power(uint32_t op)
{
    return pip_syscall(PIP_SYS_POWER, U(op), 0, 0, 0);
}

int32_t pip_log(const char *text)
{
    return pip_syscall(PIP_SYS_LOG, P(text), 0, 0, 0);
}

int32_t pip_rand(void)
{
    return pip_syscall(PIP_SYS_RAND, 0, 0, 0, 0);
}

int32_t pip_set_time(uint32_t unix_seconds)
{
    return pip_syscall(PIP_SYS_SET_TIME, U(unix_seconds), 0, 0, 0);
}

int32_t pip_read_save(const char *name, void *buf, uint32_t size)
{
    return pip_syscall(PIP_SYS_READ_SAVE, P(name), P(buf), U(size), 0);
}

int32_t pip_write_save(const char *name, const void *buf, uint32_t size)
{
    return pip_syscall(PIP_SYS_WRITE_SAVE, P(name), P(buf), U(size), 0);
}

int32_t pip_save_info(const char *name, uint32_t *size)
{
    return pip_syscall(PIP_SYS_SAVE_INFO, P(name), P(size), 0, 0);
}

int32_t pip_save_delete(const char *name)
{
    return pip_syscall(PIP_SYS_SAVE_DELETE, P(name), 0, 0, 0);
}

int32_t pip_file_open(const char *path)
{
    return pip_syscall(PIP_SYS_FILE_OPEN, P(path), 0, 0, 0);
}

int32_t pip_file_read(int32_t handle, void *buf, uint32_t size)
{
    return pip_syscall(PIP_SYS_FILE_READ, U(handle), P(buf), U(size), 0);
}

int32_t pip_file_close(int32_t handle)
{
    return pip_syscall(PIP_SYS_FILE_CLOSE, U(handle), 0, 0, 0);
}

int32_t pip_dir_list(const char *path, pip_dirent_t *out, uint32_t max, uint32_t start)
{
    return pip_syscall(PIP_SYS_DIR_LIST, P(path), P(out), U(max), U(start));
}

int32_t pip_holo_enumerate(pip_holo_info_t *out, uint32_t max)
{
    return pip_syscall(PIP_SYS_HOLO_ENUMERATE, P(out), U(max), 0, 0);
}

int32_t pip_holo_launch(const char *name, const char *args)
{
    return pip_syscall(PIP_SYS_HOLO_LAUNCH, P(name), P(args), 0, 0);
}

int32_t pip_holo_last_status(void)
{
    return pip_syscall(PIP_SYS_HOLO_LAST_STATUS, 0, 0, 0, 0);
}

int32_t pip_holo_eject_event(void)
{
    return pip_syscall(PIP_SYS_HOLO_EJECT_EVENT, 0, 0, 0, 0);
}
