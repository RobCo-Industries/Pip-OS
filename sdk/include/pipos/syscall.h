/*
 * System call wrappers for ROM and holotape code, in libpip.a: one
 * function per call of the spec's table, named pip_<name>, with the
 * spec's argument and return types. Each makes one `svc #0` through
 * pip_syscall(): number in r7, arguments in r0 to r3, result in r0.
 * A negative result is -PIP_E*. A call this kernel does not serve yet
 * returns -PIP_ENOSYS. docs/ROM_DEVELOPMENT.md lists which ones it serves.
 */
#ifndef PIPOS_SYSCALL_H
#define PIPOS_SYSCALL_H

#include <stdint.h>

#include <pipos/abi.h>

/* Any call by number. Unknown numbers return -PIP_ENOSYS. */
int32_t pip_syscall(uint32_t nr, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3);

/* Graphics. Coordinates are on the 320x240 surface, colours 0x00RRGGBB. */
/* Line in the foreground colour, both end points included. 0. */
int32_t pip_draw_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1);
/* One pixel in `color`. 0. */
int32_t pip_draw_point(int32_t x, int32_t y, uint32_t color);
/* Text, top left at x, y, scale 1 to 8. The width drawn in pixels. */
int32_t pip_draw_text(int32_t x, int32_t y, const char *text, uint32_t scale);
/* Fills the whole surface, ignoring the clip. 0. */
int32_t pip_clear(uint32_t color);
/* Fills *out. 0. */
int32_t pip_gfx_info(pip_gfx_info_t *out);
/* wh is PIP_WH(w, h). fill: PIP_RECT_OUTLINE or PIP_RECT_FILLED. 0. See pip_rect_xywh(). */
int32_t pip_rect(int32_t x, int32_t y, uint32_t wh, uint32_t fill);
/* count x, y pairs, 2 to 256. closed 1 joins the ends. 0. */
int32_t pip_polyline(const int16_t *xy, uint32_t count, uint32_t closed);
/* 0. */
int32_t pip_arc(const pip_arc_t *a);
/* Set bits draw in b->color. 0. */
int32_t pip_blit_1bpp(const pip_blit_t *b);
/* 0x00RRGGBB pixels. 0. */
int32_t pip_blit_rgb(const pip_blit_t *b);
/* wh is PIP_WH(w, h); 0 resets the clip. 0. See pip_set_clip_xywh(). */
int32_t pip_set_clip(int32_t x, int32_t y, uint32_t wh);
/* bg PIP_COLOR_NONE is transparent. 0. */
int32_t pip_set_color(uint32_t fg, uint32_t bg);
/* (width << 16) | height: PIP_TM_WIDTH() and PIP_TM_HEIGHT(). */
int32_t pip_text_metrics(const char *text, uint32_t scale);
/* Copies the back buffer to the display. 0. */
int32_t pip_present(void);
/* PIP_FONT_SYSTEM only. 0, or -PIP_EINVAL. */
int32_t pip_set_font(uint32_t font_id);

/* Input. */
/* PIP_BTN_*_BIT mask of the buttons down now. */
int32_t pip_read_buttons(void);
/* Dial position, 0 to 359 degrees. */
int32_t pip_read_dial(void);
/* 1 if an event was read into *out, 0 if none. */
int32_t pip_poll_event(pip_event_t *out);
/* 1, or -PIP_ETIMEDOUT. PIP_WAIT_FOREVER waits forever. */
int32_t pip_wait_event(pip_event_t *out, uint32_t timeout_ms);

/* Audio. One voice; sounds play in the background. */
/* 20 to 20000 Hz, 1 to 60000 ms. 0. */
int32_t pip_tone(uint32_t freq_hz, uint32_t duration_ms);
/* 8-bit unsigned at the rate of pip_sample_rate, 8000 Hz unless set, at most 65536 bytes. 0. */
int32_t pip_sample(const uint8_t *data, uint32_t length);
/* 0. */
int32_t pip_audio_stop(void);
/* 0 to 100. The previous level. */
int32_t pip_volume(uint32_t level);
/* 1 while a sound plays, else 0. */
int32_t pip_audio_status(void);
/* 1 to 256 notes. 0. */
int32_t pip_play_seq(const pip_note_t *notes, uint32_t count);
/*
 * Like pip_sample, but plays right after a sample that plays, with no gap.
 * One can wait: then -PIP_EBUSY. ABI 1.1: -PIP_ENOSYS on a 1.0 Pip-OS.
 */
#if PIPOS_ABI_TARGET >= 0x00010001
int32_t pip_sample_queue(const uint8_t *data, uint32_t length);
#else
int32_t pip_sample_queue(const uint8_t *data, uint32_t length) PIP_ABI_HIDDEN(sample_queue, "1.1");
#endif
/*
 * 8000 or 37500 Hz for the plays that start after it. A play keeps the
 * rate it started with. The previous rate, or -PIP_EINVAL. 8000 at every
 * image start. ABI 1.5: -PIP_ENOSYS on an older Pip-OS.
 */
#if PIPOS_ABI_TARGET >= 0x00010005
int32_t pip_sample_rate(uint32_t hz);
#else
int32_t pip_sample_rate(uint32_t hz) PIP_ABI_HIDDEN(sample_rate, "1.5");
#endif
/*
 * The kind, PIP_SND_*, of the sounds the image starts after it. A play
 * keeps the kind it started with. The previous kind, -PIP_EINVAL for a
 * kind above 3, or -PIP_EPERM for radio or sfx from a holotape. sfx at a
 * ROM's start, games at a holotape's.
 *
 * sound_level: which is a PIP_SND_* kind or PIP_LEVEL_MASTER. level is 0
 * to 100, or PIP_LEVEL_ASK to read. The level before, -PIP_EINVAL, or
 * -PIP_EPERM when a holotape sets a level.
 *
 * sound_store: writes the master and the four levels to the boot card. 0,
 * -PIP_EPERM for a holotape, -PIP_ENOSPC for a full card or -PIP_EIO.
 *
 * ABI 1.6: -PIP_ENOSYS on an older Pip-OS.
 */
#if PIPOS_ABI_TARGET >= 0x00010006
int32_t pip_sound_kind(uint32_t kind);
int32_t pip_sound_level(uint32_t which, uint32_t level);
int32_t pip_sound_store(void);
#else
int32_t pip_sound_kind(uint32_t kind) PIP_ABI_HIDDEN(sound_kind, "1.6");
int32_t pip_sound_level(uint32_t which, uint32_t level) PIP_ABI_HIDDEN(sound_level, "1.6");
int32_t pip_sound_store(void) PIP_ABI_HIDDEN(sound_store, "1.6");
#endif

/* Time, sensors and power. */
/* PIP_SENSOR_*. 0, or -PIP_ENODEV. */
int32_t pip_read_sensor(uint32_t sensor, uint32_t *value);
/* 1 when pip_read_sensor() would succeed, else 0. */
int32_t pip_sensor_available(uint32_t sensor);
/* 0, or -PIP_ENODEV when no clock is set. */
int32_t pip_get_time(uint32_t *unix_seconds);
/* Percent, or -PIP_ENODEV. */
int32_t pip_get_battery(void);

/* System. */
/* Milliseconds since boot, 0 to 2^31 - 1, then wraps to 0. */
int32_t pip_ticks_ms(void);
/* Waits at least ms milliseconds, then returns 0. ms above 0x7FFFFFFF returns -PIP_ERANGE at once. */
int32_t pip_sleep_ms(uint32_t ms);
/* Ends the image with status, 0 to 255. Does not return. */
__attribute__((noreturn)) void pip_exit(int32_t status);
/* Fills *out. 0, or -PIP_EFAULT for a bad pointer. */
int32_t pip_sys_info(pip_sys_info_t *out);
/* Fills *out. 0, or -PIP_EFAULT for a bad pointer. */
int32_t pip_mem_info(pip_mem_info_t *out);
/* PIP_POWER_*. 0, does not return, or -PIP_EINVAL. ROM only. */
int32_t pip_power(uint32_t op);
/* Prints text on the serial console, at most 255 bytes. No newline is added. */
int32_t pip_log(const char *text);
/* 0 to 2^31 - 1, or -PIP_EIO when the generator fails. */
int32_t pip_rand(void);
/* Sets the software clock. 0. ROM only. */
int32_t pip_set_time(uint32_t unix_seconds);

/* Saves: /PIPBOY/SAVE/<NAME>.SAV, NAME 1 to 8 of A-Z 0-9 _. */
/* Bytes read, at most size. */
int32_t pip_read_save(const char *name, void *buf, uint32_t size);
/* Bytes written, at most 65536. */
int32_t pip_write_save(const char *name, const void *buf, uint32_t size);
/* Stores the size. 0. */
int32_t pip_save_info(const char *name, uint32_t *size);
/* 0. */
int32_t pip_save_delete(const char *name);

/* Files, read only. */
/* A handle, 0 to 7. */
int32_t pip_file_open(const char *path);
/* Bytes read, 0 at the end. */
int32_t pip_file_read(int32_t handle, void *buf, uint32_t size);
/* 0. */
int32_t pip_file_close(int32_t handle);
/* Entries written, max 1 to 64, from entry start. */
int32_t pip_dir_list(const char *path, pip_dirent_t *out, uint32_t max, uint32_t start);

/* Holotapes. */
/* Entries written, max 1 to 32. */
int32_t pip_holo_enumerate(pip_holo_info_t *out, uint32_t max);
/* The holotape's exit status, 0 to 255. ROM only. */
int32_t pip_holo_launch(const char *name, const char *args);
/* What the last pip_holo_launch() returned, or -PIP_ENOENT. */
int32_t pip_holo_last_status(void);
/*
 * PIP_HOLO_EV_* bits of the slot changes since the last call, which it
 * clears. 0 from a holotape, with PIP_HW_TAPE clear, and before ABI 1.4.
 */
int32_t pip_holo_eject_event(void);

/* rect and set_clip with w and h apart: PIP_WH() packs them. */
static inline int32_t pip_rect_xywh(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t fill)
{
    return pip_rect(x, y, PIP_WH(w, h), fill);
}

static inline int32_t pip_set_clip_xywh(int32_t x, int32_t y, uint32_t w, uint32_t h)
{
    return pip_set_clip(x, y, PIP_WH(w, h));
}

#endif
