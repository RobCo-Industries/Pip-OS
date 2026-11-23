/* Kernel-internal interfaces between the Pip-OS source files. */
#ifndef PIPOS_H
#define PIPOS_H

#include <stdint.h>

#include <robco/input.h>
#include <robco/loader.h>

#include "settings.h"

#define PIPOS_STAGE      "pipos"
#define PIPOS_VOLUME     "sd"
#define PIPOS_CFG_PATH   "/PIPBOY/PIPOS.CFG"
#define PIPOS_ROM_PATH   "/PIPBOY/ROM1.ROM"
#define PIPOS_HOLO_DIR   "/PIPBOY/HOLO"

/* ROM slot from the Pip-Boy chain memory map. */
#define PIPOS_ROM_BASE   0x00400000u
#define PIPOS_ROM_SIZE   0x00400000u

/* Fatal kernel error: one marker, a message on screen, halt. */
void pipos_panic(const char *why) __attribute__((noreturn));

/* card.c: SD card, configuration file, holotape directory, ROM file. */
int pipos_card_mount(void);   /* 1 when the card is mounted, else 0 */
void pipos_config_load(int card, pipos_settings_t *s);
unsigned pipos_holo_count(int card);
/* Loads and validates the ROM. Returns a rimg_err_t code. */
int pipos_rom_load(int card, loader_result_t *res);

/* rom.c: runs the loaded ROM in user mode until it ends, and reports how. */
void pipos_rom_run(const loader_result_t *res);

/* main.c: one pass of the main loop's work: audio, input, script commands, group polls. */
void pipos_poll(void);

/* sys_audio.c: the level main.c gave audio_set_volume() at boot; the volume call starts from it. */
void pipos_audio_start_volume(unsigned level);
/* sys_audio.c: main.c reports audio_init()'s result at boot; sys_info's PIP_HW_AUDIO reads it. */
void pipos_audio_driver_result(int rc);
int pipos_audio_driver_ok(void);

/* sys_input.c: every RobCo-Core event main.c takes; empties the image's event queue. */
void pipos_input_event(const input_event_t *ev);
void pipos_input_flush(void);

/* screen.c: boot text on the screen console and the UART. */
void pipos_screen_delay(uint32_t ms);
void pipos_screen_line(const char *text);

/*
 * sys_gfx.c: on, the display shows black and present only updates the
 * back buffer; off, the last presented frame (or the console) comes back.
 */
void pipos_gfx_blank(int on);

/* sys_holo.c: 1 while a holotape runs. */
int pipos_holo_running(void);

#endif
