/*
 * Pure rules of the holotape group. See holo.h.
 */
#include <robco/klibc.h>
#include <robco/rimg.h>
#include <robco/umode.h>

#include "holo.h"

static char upper(char c)
{
    return c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c;
}

static int name_char(char c)
{
    static const char marks[] = "!#$%&'()-@^_`{}~";
    unsigned i;

    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
        return 1;
    }
    for (i = 0; marks[i] != '\0'; i++) {
        if (c == marks[i]) {
            return 1;
        }
    }
    return 0;
}

int32_t pipos_holo_name(const char *in, char *out)
{
    size_t n = 0, dot = 0;

    while (in[n] != '\0') {
        if (n >= PIPOS_HOLO_NAME_SIZE - 1) {
            return -PIP_EINVAL;
        }
        out[n] = upper(in[n]);
        n++;
    }
    out[n] = '\0';
    /* NAME.HTP: 1 to 8 name characters, then exactly ".HTP". */
    if (n < 5 || n > 12) {
        return -PIP_EINVAL;
    }
    dot = n - 4;
    if (k_strcmp(out + dot, ".HTP") != 0) {
        return -PIP_EINVAL;
    }
    for (n = 0; n < dot; n++) {
        if (!name_char(out[n])) {
            return -PIP_EINVAL;
        }
    }
    return 0;
}

const char *pipos_holo_listed_name(const char *name, const char *short_name)
{
    return short_name[0] != '\0' ? short_name : name;
}

static int same_nocase(const char *a, const char *b)
{
    while (*a != '\0' && upper(*a) == upper(*b)) {
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

int pipos_holo_entry_match(const char *want, const char *name, const char *short_name)
{
    return same_nocase(want, name) || (short_name[0] != '\0' && same_nocase(want, short_name));
}

static const rimg_expect_t holo_expect = {
    .magic = RIMG_MAGIC_ROBCO78,
    .slot_base = PIPOS_HOLO_BASE,
    .slot_size = PIPOS_HOLO_SIZE,
    .abi_major = PIP_ABI_VERSION >> 16,
    .file_size = 0,
};

int pipos_holo_listed(const void *head, size_t len, uint32_t fat_size)
{
    rimg_expect_t e = holo_expect;
    int rc;

    if (len < PIPOS_HOLO_HEAD || fat_size < PIPOS_HOLO_HEAD) {
        return 0;
    }
    e.file_size = fat_size;
    /* Steps 1 to 5 stop at the first failure: past step 3 means 1 to 3 passed. */
    rc = rimg_check_header(head, fat_size, &e);
    return rc == RIMG_OK || rc == RIMG_E_BOUNDS || rc == RIMG_E_ABI;
}

void pipos_holo_info_fill(pip_holo_info_t *out, const char *file, uint32_t fat_size,
                          const void *head)
{
    const uint8_t *b = head;
    rimg_header_t h;

    k_memcpy(&h, b + RIMG_HDR_OFFSET, sizeof(h));
    k_memset(out, 0, sizeof(*out));
    k_strncpy(out->file, file, sizeof(out->file) - 1);
    out->subtype = (uint8_t)h.subtype;
    out->size = fat_size;
    out->image_version = h.image_version;
    k_memcpy(out->name, h.name, sizeof(out->name));
    k_memcpy(out->title, b + RIMG_META_OFFSET, sizeof(out->title));
    k_memcpy(out->icon, b + RIMG_META_OFFSET + sizeof(out->title), sizeof(out->icon));
}

void pipos_startup_holo(pipos_startup_t *s, const char *args)
{
    k_memset(s, 0, sizeof(*s));
    s->abi_version = PIP_ABI_VERSION;
    s->size = sizeof(*s);
    s->heap_base = PIPOS_HOLO_HEAP_BASE;
    s->heap_size = PIPOS_HOLO_HEAP_SIZE;
    s->stack_top = PIPOS_HOLO_STACK_TOP;
    s->kind = PIP_KIND_HOLOTAPE;
    k_strncpy(s->args, args, PIP_ARGS_MAX);
    s->args[PIP_ARGS_MAX] = '\0';
}

void pipos_mem_info_holo(pip_mem_info_t *m)
{
    m->heap_base = PIPOS_HOLO_HEAP_BASE;
    m->heap_size = PIPOS_HOLO_HEAP_SIZE;
    m->stack_top = PIPOS_HOLO_STACK_TOP;
    m->stack_size = PIP_STACK_SIZE;
}

int32_t pipos_holo_result(unsigned reason, int code)
{
    if (reason == UMODE_RETURNED || reason == UMODE_EXITED) {
        return (int32_t)((unsigned)code & 0xffu);
    }
    return -PIP_EFAULT;
}

int32_t pipos_holo_load_errno(int rimg_err)
{
    return rimg_err == RIMG_E_NOFILE ? -PIP_ENOENT : -PIP_ENOEXEC;
}
