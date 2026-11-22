/* The pure part of the system-call dispatch and the handlers' helpers. See sys.h. */
#include <robco/klibc.h>

#include "sys.h"
#include "sysabi.h"

int pipos_sys_index_build(pipos_sys_index_t *ix, const pipos_sys_table_t *const *tables,
                          unsigned count, uint32_t *bad_nr)
{
    unsigned t, i;

    for (i = 0; i < PIP_SYS_NR_LIMIT; i++) {
        ix->fn[i] = NULL;
        ix->owner[i] = -1;
    }
    for (t = 0; t < count; t++) {
        const pipos_sys_table_t *tab = tables[t];

        for (i = 0; i < tab->count; i++) {
            uint32_t nr = tab->entries[i].nr;

            *bad_nr = nr;
            if (nr >= PIP_SYS_NR_LIMIT || PIPOS_SYS_OWNER(nr) < 0) {
                return PIPOS_SYS_EABI;
            }
            if (ix->owner[nr] >= 0) {
                return PIPOS_SYS_EDUP;
            }
            if (PIPOS_SYS_OWNER(nr) != tab->group) {
                return PIPOS_SYS_EOWNER;
            }
            ix->owner[nr] = (int8_t)tab->group;
            ix->fn[nr] = tab->entries[i].fn;
        }
    }
    *bad_nr = 0;
    return PIPOS_SYS_OK;
}

pipos_sys_fn pipos_sys_lookup(const pipos_sys_index_t *ix, uint32_t nr)
{
    return nr < PIP_SYS_NR_LIMIT ? ix->fn[nr] : NULL;
}

int32_t pipos_sys_dispatch(const pipos_sys_index_t *ix, const pipos_sys_ctx_t *ctx, uint32_t nr,
                           uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    pipos_sys_fn fn = pipos_sys_lookup(ix, nr);

    return fn != NULL ? fn(ctx, a0, a1, a2, a3) : -PIP_ENOSYS;
}

void pipos_sys_served(const pipos_sys_index_t *ix, uint32_t mask[PIP_SYS_NR_LIMIT / 32])
{
    unsigned nr;

    for (nr = 0; nr < PIP_SYS_NR_LIMIT / 32; nr++) {
        mask[nr] = 0;
    }
    for (nr = 0; nr < PIP_SYS_NR_LIMIT; nr++) {
        if (ix->fn[nr] != NULL) {
            mask[nr / 32] |= 1u << (nr % 32);
        }
    }
}

int32_t pipos_sys_rom_only(const pipos_sys_ctx_t *ctx)
{
    return ctx->kind == PIP_KIND_ROM ? 0 : -PIP_EPERM;
}

static int32_t check(const pipos_sys_ctx_t *ctx, uintptr_t ptr, size_t n, int write)
{
    if (ptr + n < ptr) {
        return -PIP_EFAULT;     /* wraps past the top of the address space */
    }
    return ctx->mem->range_ok(ptr, n, write) ? 0 : -PIP_EFAULT;
}

int32_t pipos_user_check_out(const pipos_sys_ctx_t *ctx, uintptr_t ptr, size_t n)
{
    return check(ctx, ptr, n, 1);
}

int32_t pipos_user_check_in(const pipos_sys_ctx_t *ctx, uintptr_t ptr, size_t n)
{
    return check(ctx, ptr, n, 0);
}

int32_t pipos_user_copy_out(const pipos_sys_ctx_t *ctx, uintptr_t ptr, const void *src, size_t n)
{
    int32_t rc = check(ctx, ptr, n, 1);

    if (rc == 0 && n > 0) {
        k_memcpy((void *)ptr, src, n);
    }
    return rc;
}

int32_t pipos_user_copy_in(const pipos_sys_ctx_t *ctx, void *dst, uintptr_t ptr, size_t n)
{
    int32_t rc = check(ctx, ptr, n, 0);

    if (rc == 0 && n > 0) {
        k_memcpy(dst, (const void *)ptr, n);
    }
    return rc;
}

long pipos_user_strlen(const pipos_sys_ctx_t *ctx, uintptr_t ptr, size_t size)
{
    if (size == 0) {
        return -PIP_EINVAL;
    }
    return pipos_str_check(ctx->mem->strnlen_ok(ptr, size),
                           check(ctx, ptr, size, 0) == 0);
}

long pipos_user_str(const pipos_sys_ctx_t *ctx, char *dst, uintptr_t ptr, size_t size)
{
    long len = pipos_user_strlen(ctx, ptr, size);

    if (len >= 0) {
        k_memcpy(dst, (const void *)ptr, (size_t)len + 1u);
    }
    return len;
}
