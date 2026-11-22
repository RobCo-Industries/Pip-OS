/*
 * Storage rules with no hardware: save names, file paths, error codes,
 * the handle table. See storage.h.
 *
 * Paths are strict so that no spelling reaches the save folder: FatFs
 * would accept "." and "..", "\", trailing dots and spaces, and long
 * names, any of which could lead into /PIPBOY/SAVE by another way.
 */
#include <robco/fs.h>

#include "storage.h"

static char upper(char c)
{
    return c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c;
}

static int save_char(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

int32_t pipos_save_name(const char *name, char *out)
{
    unsigned i;

    for (i = 0; name[i] != '\0'; i++) {
        char c = upper(name[i]);

        if (i >= PIP_SAVE_NAME_MAX || !save_char(c)) {
            return -PIP_EINVAL;
        }
        out[i] = c;
    }
    out[i] = '\0';
    return i == 0 ? -PIP_EINVAL : 0;
}

static unsigned put(char *out, unsigned at, unsigned size, const char *s)
{
    while (*s != '\0' && at + 1 < size) {
        out[at++] = *s++;
    }
    out[at] = '\0';
    return at;
}

void pipos_save_path(const char *prefix, const char *name, char mark, char *out,
                     unsigned size)
{
    char m[2] = { mark, '\0' };
    unsigned at = put(out, 0, size, prefix);

    at = put(out, at, size, PIPOS_SAVE_DIR "/");
    at = put(out, at, size, m);
    at = put(out, at, size, name);
    put(out, at, size, ".SAV");
}

/* The characters of a FAT short name, after upper-casing. No space: see the docs. */
static int sfn_char(char c)
{
    static const char extra[] = "!#$%&'()-@^_`{}~";
    unsigned i;

    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
        return 1;
    }
    for (i = 0; extra[i] != '\0'; i++) {
        if (c == extra[i]) {
            return 1;
        }
    }
    return 0;
}

/* One component, s[0..len): BASE or BASE.EXT, 1 to 8 and 1 to 3 characters. */
static int sfn_ok(const char *s, unsigned len)
{
    unsigned base = 0, ext = 0, dots = 0, i;

    for (i = 0; i < len; i++) {
        if (s[i] == '.') {
            dots++;
        } else if (!sfn_char(s[i])) {
            return 0;
        } else if (dots == 0) {
            base++;
        } else {
            ext++;
        }
    }
    return dots <= 1 && base >= 1 && base <= 8 && ext <= 3 && (dots == 0 || ext >= 1);
}

static int same(const char *s, unsigned len, const char *word)
{
    unsigned i;

    for (i = 0; i < len; i++) {
        if (word[i] == '\0' || s[i] != word[i]) {
            return 0;
        }
    }
    return word[len] == '\0';
}

int32_t pipos_file_path(const char *path, int root_ok, char *out)
{
    unsigned n, start, comp = 0;
    int save_dir = 0;

    for (n = 0; path[n] != '\0'; n++) {
        if (n >= PIP_PATH_MAX) {
            return -PIP_EINVAL;
        }
        out[n] = upper(path[n]);
    }
    out[n] = '\0';
    if (n == 0 || out[0] != '/') {
        return -PIP_EINVAL;
    }
    if (n == 1) {
        return root_ok ? 0 : -PIP_EINVAL;
    }
    start = 1;
    for (;;) {
        unsigned end = start;

        while (out[end] != '\0' && out[end] != '/') {
            end++;
        }
        /* Covers "//", a trailing "/", ".", ".." and trailing dots or spaces. */
        if (!sfn_ok(out + start, end - start)) {
            return -PIP_EINVAL;
        }
        if (comp == 0) {
            save_dir = same(out + start, end - start, "PIPBOY");
        } else if (comp == 1) {
            save_dir = save_dir && same(out + start, end - start, "SAVE");
        }
        comp++;
        if (out[end] == '\0') {
            break;
        }
        start = end + 1;
    }
    return save_dir && comp >= 2 ? -PIP_EPERM : 0;
}

int32_t pipos_storage_errno(int fs_err)
{
    switch (fs_err) {
    case FS_ERR_NOENT: return -PIP_ENOENT;
    case FS_ERR_NOSPC: return -PIP_ENOSPC;
    case FS_ERR_TOOBIG: return -PIP_ERANGE;
    default:           return -PIP_EIO;
    }
}

int32_t pipos_handle_alloc(uint32_t *used)
{
    int32_t h;

    for (h = 0; h < (int32_t)PIPOS_STORAGE_HANDLES; h++) {
        if (!(*used & (1u << h))) {
            *used |= 1u << h;
            return h;
        }
    }
    return -PIP_ENOMEM;
}

int32_t pipos_handle_check(uint32_t used, int32_t h)
{
    if (h < 0 || h >= (int32_t)PIPOS_STORAGE_HANDLES || !(used & (1u << h))) {
        return -PIP_EBADF;
    }
    return 0;
}

void pipos_handle_free(uint32_t *used, int32_t h)
{
    if (pipos_handle_check(*used, h) == 0) {
        *used &= ~(1u << h);
    }
}

unsigned pipos_handle_count(uint32_t used)
{
    unsigned n = 0;

    for (; used != 0; used &= used - 1u) {
        n++;
    }
    return n;
}
