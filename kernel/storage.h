/*
 * Pure rules of the storage group: save names, file paths, error codes and
 * the handle table. storage_rules.c builds on the host too.
 */
#ifndef PIPOS_STORAGE_H
#define PIPOS_STORAGE_H

#include <stdint.h>

#include <pipos/abi.h>

#define PIPOS_SAVE_DIR          "/PIPBOY/SAVE"
#define PIPOS_STORAGE_HANDLES   8u

/* Bytes of the "sd:" prefix plus the longest path built from a save name: "~NAME.SAV". */
#define PIPOS_SAVE_PATH_MAX     40u

/*
 * Checks a save name of at most PIP_SAVE_NAME_MAX characters, upper-cases
 * it into out[PIP_SAVE_NAME_MAX + 1]. 0 or -PIP_EINVAL.
 */
int32_t pipos_save_name(const char *name, char *out);

/*
 * Builds "<prefix>/PIPBOY/SAVE/<mark><name>.SAV" into out[size]. mark is
 * 0 for the save itself, or '~' or '#' for a work file.
 */
void pipos_save_path(const char *prefix, const char *name, char mark, char *out,
                     unsigned size);

/*
 * Checks an absolute path (at most PIP_PATH_MAX bytes, NUL terminated)
 * and copies it upper-cased into out[PIP_PATH_MAX + 1]. root_ok allows
 * the root "/" itself. 0, -PIP_EINVAL for a bad spelling, or -PIP_EPERM
 * for the save folder and anything in it.
 */
int32_t pipos_file_path(const char *path, int root_ok, char *out);

/* A negative FS_ERR_* code as a -PIP_E* code. Anything else is -PIP_EIO. */
int32_t pipos_storage_errno(int fs_err);

/* The handle table: bit n of *used is handle n. */
int32_t pipos_handle_alloc(uint32_t *used);          /* handle, or -PIP_ENOMEM */
int32_t pipos_handle_check(uint32_t used, int32_t h); /* 0 or -PIP_EBADF */
void pipos_handle_free(uint32_t *used, int32_t h);
unsigned pipos_handle_count(uint32_t used);

/* sys_storage.c: open file handles of the running image, for "state". */
unsigned pipos_storage_open_count(void);

#endif
