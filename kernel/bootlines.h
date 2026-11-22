/*
 * The boot screen text and its formatting rules. Pure code: it builds
 * and runs on the host in the unit tests.
 */
#ifndef PIPOS_BOOTLINES_H
#define PIPOS_BOOTLINES_H

#include <stddef.h>
#include <stdint.h>

#define PIPOS_LINE_MAX      80u
#define PIPOS_ROM_NAME_MAX  16u

/* The banner and the fixed lines before the holotape line, NULL-terminated. */
extern const char *const pipos_boot_head[];
/* The lines after the ROM line, NULL-terminated. */
extern const char *const pipos_boot_tail[];

/* "3.0.3" from (major << 16) | (minor << 8) | patch. Returns the length. */
int pipos_fmt_version(uint32_t version, char *buf, size_t size);

/*
 * Copies a 16-byte header name, which may lack a NUL. Bytes outside
 * printable ASCII and spaces become '_'; with upper set, letters are
 * upper case. An empty name gives "-". out holds at least 17 bytes.
 */
void pipos_fmt_name(const char *raw, size_t raw_len, char *out, size_t size, int upper);

/* "NO HOLOTAPE FOUND", "1 HOLOTAPE FOUND" or "<n> HOLOTAPES FOUND". */
int pipos_holo_line(unsigned count, char *buf, size_t size);

/* "LOAD ROM(1): <NAME> <version>". name is already formatted. */
int pipos_rom_line(const char *name, uint32_t version, char *buf, size_t size);

/*
 * The line shown instead of the ROM line. card is 0 when no card is
 * mounted. rc is a rimg_err_t code.
 */
int pipos_rom_error_line(int card, int rc, char *buf, size_t size);

/* Non-zero for a file name that ends in ".HTP", any case, with a base name. */
int pipos_is_holotape_name(const char *name);

/* One marker value: bytes outside 0x21 to 0x7e become '_'. Empty gives "-". */
void pipos_marker_word(const char *in, char *out, size_t size);

#endif
