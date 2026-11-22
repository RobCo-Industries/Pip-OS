/* Kernel-internal interfaces between the Pip-OS source files. */
#ifndef PIPOS_H
#define PIPOS_H

#include <stdint.h>

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

/* screen.c: boot text on the screen console and the UART. */
void pipos_screen_delay(uint32_t ms);
void pipos_screen_line(const char *text);

#endif
