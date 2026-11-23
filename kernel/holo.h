/*
 * Pure rules of the holotape group: file names, the listing check, the
 * holo_enumerate entry, the startup block and the launch result.
 * holo_rules.c builds on the host too. See docs/pending/holo.md.
 */
#ifndef PIPOS_HOLO_H
#define PIPOS_HOLO_H

#include <stddef.h>
#include <stdint.h>

#include <pipos/abi.h>

/* Holotape slot and heap region from the Pip-Boy chain memory map. */
#define PIPOS_HOLO_BASE         0x00800000u
#define PIPOS_HOLO_SIZE         0x00400000u
#define PIPOS_HOLO_HEAP_BASE    0x01800000u
#define PIPOS_HOLO_HEAP_REGION  0x00800000u
#define PIPOS_HOLO_STACK_TOP    0x02000000u
#define PIPOS_HOLO_HEAP_SIZE    (PIPOS_HOLO_HEAP_REGION - PIP_STACK_SIZE)

/* The startup block and the umode return stub, at the bottom of the stack. */
#define PIPOS_HOLO_STARTUP      (PIPOS_HOLO_STACK_TOP - PIP_STACK_SIZE)
#define PIPOS_HOLO_STUB         (PIPOS_HOLO_STARTUP + 128u)

/* Bytes of a holotape file the listing reads: stub, header and META block. */
#define PIPOS_HOLO_HEAD         0x120u

/* Longest 8.3 name with its NUL. */
#define PIPOS_HOLO_NAME_SIZE    13u

/*
 * Checks a holotape file name: 1 to 8 name characters, ".", then "HTP",
 * any case. Name characters are A-Z, 0-9 and the FAT short-name marks
 * ! # $ % & ' ( ) - @ ^ _ ` { } ~. Copies it upper-cased into
 * out[PIPOS_HOLO_NAME_SIZE]. 0 or -PIP_EINVAL.
 */
int32_t pipos_holo_name(const char *in, char *out);

/* The name holo_enumerate lists for a directory entry: the 8.3 alias, or the name when it has none. */
const char *pipos_holo_listed_name(const char *name, const char *short_name);

/*
 * 1 when a holo_launch for `want` (checked, upper case) would make the
 * file system open this entry: its long name or its alias equals `want`,
 * ignoring case, as FatFs compares them. The first entry that matches is
 * the one the file system opens.
 */
int pipos_holo_entry_match(const char *want, const char *name, const char *short_name);

/*
 * 1 when the first `len` bytes of a file of `fat_size` bytes pass the
 * listing checks: magic ROBCO78, version and size (steps 1 to 3 of
 * IMAGE_FORMAT.md). The CRC is not checked.
 */
int pipos_holo_listed(const void *head, size_t len, uint32_t fat_size);

/* Fills a holo_enumerate entry from a file that passed pipos_holo_listed(). */
void pipos_holo_info_fill(pip_holo_info_t *out, const char *file, uint32_t fat_size,
                          const void *head);

/* The startup block of a holotape, with its argument string (at most PIP_ARGS_MAX bytes). */
void pipos_startup_holo(pipos_startup_t *s, const char *args);

/* mem_info contents for an image of `kind`. */
void pipos_mem_info_holo(pip_mem_info_t *m);

/*
 * What holo_launch returns for a holotape that ended with a umode.h
 * reason and code: the low 8 bits of the code after a return or an
 * exit, -PIP_EFAULT after a fault or a kill.
 */
int32_t pipos_holo_result(unsigned reason, int code);

/* A rimg_err_t from the loader as holo_launch's error: -PIP_ENOENT or -PIP_ENOEXEC. */
int32_t pipos_holo_load_errno(int rimg_err);

#endif
