/*
 * The display backends: HDMI through fb.h, the ILI9341 SPI panel
 * through tft.h, or both. See docs/ARCHITECTURE.md, "Display path".
 */
#ifndef PIPOS_DISPLAY_H
#define PIPOS_DISPLAY_H

#include <stdint.h>

#include "settings.h"

/* Kernel-only RAM from the Pip-Boy memory map, "Back buffers". */
#define GFX_BB_BASE     0x00C00000u
/* The last frame sent to the panel, same size as the back buffer. */
#define GFX_SHADOW_BASE 0x00C80000u
/* The panel console's frame, so the back buffer keeps the image's pixels. */
#define GFX_CONSOLE_BASE 0x00D00000u
/* Test builds only: the panel transcript, up to the end of the region. */
#define GFX_TAP_BASE    0x00D80000u
#define GFX_TAP_END     0x01000000u

/* Panel console: the 8x16 font on 320x240. */
#define PIPOS_PANEL_COLS 40u
#define PIPOS_PANEL_ROWS 15u

/*
 * Runs after the settings: starts the panel when the backend asks for
 * it and prints ev=display. A panel that fails leaves HDMI in use.
 */
void pipos_display_start(const pipos_settings_t *s);

/* 1 when ROM frames go to HDMI, 1 when they go to the panel. */
int pipos_display_hdmi(void);
int pipos_display_panel(void);

/* Sends the changed part of the back buffer to the panel. 0 or -PIP_EIO. */
int32_t pipos_display_send(const uint32_t *bb);
/* The next send covers the whole frame. */
void pipos_display_full(void);
/* Panel display and sleep: on is blanked. */
void pipos_display_blank(int on);

/* Panel console: a line of boot text, the console redraw, the panic text. */
void pipos_display_line(const char *text);
void pipos_display_console(void);
/* Replaces the panel console's lines with these n lines, in one frame. 0 clears it. */
void pipos_display_text(const char *const lines[], unsigned n);
/* The same lines, held for the next console redraw, with nothing sent. */
void pipos_display_set_text(const char *const lines[], unsigned n);
void pipos_display_panic(const char *why);

/* Test builds: the tftdump script command. Returns 0, or -1 when not built in. */
struct input_script_reply;
int pipos_display_dump(struct input_script_reply *reply);

#endif
