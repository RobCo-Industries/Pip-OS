/* Kernel-internal interfaces between the Pip-OS source files. */
#ifndef PIPOS_H
#define PIPOS_H

#include <stdint.h>

#include <robco/input.h>
#include <robco/loader.h>

#include "levels.h"
#include "settings.h"

#define PIPOS_STAGE      "pipos"
#define PIPOS_VOLUME     "sd"
#define PIPOS_CFG_PATH   "/PIPBOY/PIPOS.CFG"
#define PIPOS_SET_PATH   "/PIPBOY/PIPOS.SET"
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
/*
 * Writes the master and the four levels of *lv to PIPOS.SET with the
 * atomic whole-file write. 0, -PIP_ENOSPC for a full card, else -PIP_EIO.
 */
int32_t pipos_levels_store(const pipos_levels_t *lv);

/* rom.c: runs the loaded ROM in user mode until it ends, and reports how. */
void pipos_rom_run(const loader_result_t *res);

/* main.c: one pass of the main loop's work: audio, input, script commands, group polls. */
void pipos_poll(void);
/* main.c: the pipos_poll() passes since boot. The test slot backend reports it. */
uint32_t pipos_poll_passes(void);

/*
 * sys_audio.c: the master and the four levels of [audio] at boot, by
 * PIPOS_SND_*. Sets the driver's gain for an sfx play. The volume call
 * starts from the master.
 */
void pipos_audio_start_levels(unsigned master, const unsigned levels[]);
/*
 * sys_audio.c: a sound the kernel starts itself is sfx. Call with IRQs
 * masked: kernel_gain() sets the gain of an sfx play and returns the kind
 * that held before, right before the start. kernel_gain_undo() takes that
 * kind back when the driver refused the start, so the play that goes on
 * keeps its own gain.
 */
int pipos_audio_kernel_gain(void);
void pipos_audio_kernel_gain_undo(int prev, int rc);
/* sys_audio.c: main.c reports whether a backend plays sounds after boot; PIP_HW_AUDIO reads it. */
void pipos_audio_driver_result(int started);
int pipos_audio_driver_ok(void);
/* main.c reports whether the boot rest ramp reached its end; PIP_HW_AUDIO_MID reads it. */
void pipos_audio_rest_result(int done);
int pipos_audio_rest_mid(void);
/* Tick context: the driver is idle, so no image sound is owned. */
void pipos_audio_idle(void);

/* sys_input.c: every RobCo-Core event main.c takes; empties the image's event queue. */
void pipos_input_event(const input_event_t *ev);
void pipos_input_flush(void);

/* screen.c: boot text on the screen console and the UART. */
void pipos_screen_delay(uint32_t ms);
/* [display] char_delay_ms for the boot text. */
void pipos_screen_typing(uint32_t char_delay_ms);
void pipos_screen_line(const char *text);
/*
 * The next line shown is the boot text's second line: the note rises with
 * it. The boot screen says so, since a line can come before the boot text,
 * such as the panel's failure line.
 */
void pipos_screen_note_next(void);
/* The next line shown is the boot text's last line: the quiet second starts with it. */
void pipos_screen_last_next(void);
/*
 * After the last boot line. rom is 1 when a ROM loaded and runs next. Waits
 * for the note, then with typing on and a ROM, scrolls the text away.
 */
void pipos_screen_end(int rom);
/*
 * After a ROM ends, however, and before the image end hooks: writes the
 * scrolled boot text to the console and the panel's text, so the end's
 * console redraw shows it. Holotapes never call it.
 */
void pipos_screen_ready(void);
/* After the ROM's image end: wakes a screen the ROM left blanked. */
void pipos_screen_restore(void);

/*
 * dsound.c: the disk click. Card and slot work and the typed boot text add
 * to a debt of clicks; a tick callback pays it while the voice is free.
 * Reads before PIPOS.CFG is parsed are counted and wait for the setting.
 */
struct blk_dev;
/* The card's or the slot's device behind a wrapper that counts calls and blocks. */
struct blk_dev *pipos_dsound_card(struct blk_dev *dev);
struct blk_dev *pipos_dsound_slot(struct blk_dev *dev);
/* After a boot stage: prints ev=disk at=<at>, or holds it until the setting is known. */
void pipos_dsound_stage(const char *at);
/* The setting is known: on opens the gate and prints the held stages. */
void pipos_dsound_start(int on);
/* A boot line written with typing on: before it, and after it with the result. */
unsigned pipos_dsound_line(const char *text);
void pipos_dsound_line_done(unsigned owed);
/*
 * The boot note, at the second boot line: plays when the text from that
 * line on outlasts it at these delays. Main context.
 */
void pipos_dsound_note(uint32_t char_delay_ms, uint32_t line_delay_ms);
/*
 * The last boot line starts: with the sound on, closes the gate, drops what
 * is owed and prints ev=quiet dropped=<n> t_ms=<at_ms>. Returns 1 when it
 * closed the gate. pipos_dsound_reopen() opens it again, for a boot with no
 * ROM.
 */
int pipos_dsound_quiet(uint32_t at_ms);
void pipos_dsound_reopen(void);
/* With the sound on, plays the scroll's blip. Returns 1 when it plays. */
int pipos_dsound_blip(void);
/* Waits while the note plays, polling the trace. */
void pipos_dsound_note_wait(void);
/* Before the jump to a ROM: no click starts, and one that plays ends. */
void pipos_dsound_jump(void);
/* Right before ev=jump: the ROM's 700 ms start. */
void pipos_dsound_anchor(void);
/*
 * Right after umode_run() returns from a ROM: nothing owed, the gate
 * closed, a click that plays ends and the queue prints.
 */
void pipos_dsound_rom_returned(void);
/* Before ev=ready after a ROM: the gate opens, the boot pace. */
void pipos_dsound_rom_ended(void);
/*
 * In an image's audio call, right before it starts or stops a sound.
 * Stops a kernel click that plays, and no click starts until
 * pipos_dsound_call_done(). May run with IRQs masked.
 */
void pipos_dsound_cut_click(void);
/* 1 while a kernel click plays. */
int pipos_dsound_click_plays(void);
/*
 * Last in every audio call of an image, after a cut or not. outcome is a
 * PIPOS_ACALL_ value of dsound_rules.h. Lets clicks start again, restarts
 * the 300 ms for a ROM sound that started or stopped, and polls the trace.
 */
void pipos_dsound_call_done(int outcome);
/* Prints queued audio records with the sound on, before the jump. */
void pipos_dsound_poll(void);
/* Waits ms in steps of at most 100 ms, with a pipos_dsound_poll() after each. */
void pipos_dsound_delay(uint32_t ms);

/*
 * sys_gfx.c: on, the display shows black and present only updates the
 * back buffer; off, the last presented frame (or the console) comes back.
 */
void pipos_gfx_blank(int on);
/* Wakes a blanked screen, which shows the console. Does nothing otherwise. */
void pipos_gfx_wake(void);

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
