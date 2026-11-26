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
/* main.c: the pipos_poll() passes since boot. The test slot backend reports it. */
uint32_t pipos_poll_passes(void);

/* sys_audio.c: the level main.c gave audio_set_volume() at boot; the volume call starts from it. */
void pipos_audio_start_volume(unsigned level);
/* sys_audio.c: main.c reports whether a backend plays sounds after boot; PIP_HW_AUDIO reads it. */
void pipos_audio_driver_result(int started);
int pipos_audio_driver_ok(void);

/* sys_input.c: every RobCo-Core event main.c takes; empties the image's event queue. */
void pipos_input_event(const input_event_t *ev);
void pipos_input_flush(void);

/* screen.c: boot text on the screen console and the UART. */
void pipos_screen_delay(uint32_t ms);
/* [display] char_delay_ms for the boot text. */
void pipos_screen_typing(uint32_t char_delay_ms);
void pipos_screen_line(const char *text);

/*
 * sys_gfx.c: on, the display shows black and present only updates the
 * back buffer; off, the last presented frame (or the console) comes back.
 */
void pipos_gfx_blank(int on);

/* sys_input.c: queues PIP_EV_TAPE with `bits` for the ROM, never for a holotape. */
void pipos_input_tape(uint32_t bits);

/*
 * slot.c: the holotape slot of ABI 1.4. See slot.h and slot_backend.h.
 * The slot card mounts as the library volume PIPOS_TAPE_VOLUME, read-only.
 */
#define PIPOS_TAPE_VOLUME "tape"
/* Boot: on is [holotape] slot. Off touches no pin and keeps ABI 1.3 behaviour. */
void pipos_slot_start(int on);
/* 1 when the setting is on and the backend started: PIP_HW_TAPE. */
int pipos_slot_enabled(void);
/* One pass of slot work: card detect, attach, mount, eject. From pipos_poll(). */
void pipos_slot_work(void);
/* 1 while the tape volume is mounted. */
int pipos_slot_mounted_now(void);
/*
 * A read on the tape volume failed: BAD with the card in, REMOVED without.
 * Returns 1 when card detect still reads the card in.
 */
int pipos_slot_read_error(void);
/* holo_eject_event: the pending bits; only the ROM clears them. */
uint32_t pipos_slot_take_bits(int caller_is_rom);
/* sys_holo.c tells the module when a slot tape starts and ends. */
void pipos_slot_tape_running(int running);
/* At each system call: 1 when the running slot tape's card left. */
int pipos_slot_eject_due(void);
/* "off", "empty", "settling", "mounted" or "bad", for the state command. */
const char *pipos_slot_state_name(void);

/* The umode_request_kill() reason when a slot tape's card leaves. */
#define PIPOS_KILL_EJECT 2

/* sys_holo.c: 1 while a holotape runs. */
int pipos_holo_running(void);

#endif
