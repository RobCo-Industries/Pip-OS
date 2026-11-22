/*
 * Checks libpip.a on the target: printf through log, the string
 * functions, and the heap up to exhaustion and back. Prints
 * "@RC stage=testrom ev=check name=<name> ok=<0|1>" per check, then
 * "ev=checks total=<n> failed=<n>". Uses nothing but the SDK.
 */
#include <pipos/pip.h>

static int total, failed;

static void check(const char *name, int ok)
{
    total++;
    if (!ok) {
        failed++;
    }
    pip_printf("@RC stage=testrom ev=check name=%s ok=%d\n", name, ok ? 1 : 0);
}

static int same(const char *a, const char *b)
{
    return strcmp(a, b) == 0;
}

static void printf_checks(void)
{
    static char line[700];
    char buf[64];
    int ok, i, n;

    ok = pip_snprintf(buf, sizeof(buf), "%d %d %u", -42, 0, 4000000000u) == 16 &&
         same(buf, "-42 0 4000000000");
    ok = ok && pip_snprintf(buf, sizeof(buf), "%x %X %08x", 0xbeefu, 0xbeefu, 0x1au) == 18 &&
         same(buf, "beef BEEF 0000001a");
    ok = ok && pip_snprintf(buf, sizeof(buf), "[%5s][%c][%%][%5d][%05d]", "ab", 'z', -7, -7) == 27 &&
         same(buf, "[   ab][z][%][   -7][-0007]");
    ok = ok && pip_snprintf(buf, sizeof(buf), "%p", (void *)0x00400120) == 10 &&
         same(buf, "0x00400120");
    check("printf_conv", ok);

    n = pip_snprintf(buf, 8, "%s", "0123456789");
    check("printf_trunc", n == 10 && same(buf, "0123456"));

    /* 600 bytes in one call: pip_printf splits them into log calls. */
    for (i = 0; i < 600; i++) {
        line[i] = (char)('a' + i % 26);
    }
    line[600] = '\0';
    n = pip_printf("%s\n", line);
    check("printf_long", n == 601);

    /* A NUL from %c prints as '?' and the rest of the line still prints. */
    n = pip_printf("sdkcheck: nul a%cb\n", 0);
    check("printf_nul", n == 18);
}

static void string_checks(void)
{
    char buf[32];
    char m[16] = "0123456789";

    memset(buf, 'x', sizeof(buf));
    memcpy(buf, "abc", 4);
    check("str_mem", memcmp(buf, "abc", 4) == 0 && buf[4] == 'x' && memcmp("a", "b", 1) < 0 &&
                         memcmp("abc", "abd", 0) == 0);

    memmove(m + 2, m, 8);                   /* forward overlap */
    m[10] = '\0';
    {
        int ok = same(m, "0101234567");

        memmove(m, m + 2, 8);               /* backward overlap */
        check("str_memmove", ok && same(m, "0123456767"));
    }

    check("str_len_cmp", strlen("pip") == 3 && strlen("") == 0 && strnlen("pipboy", 3) == 3 &&
                             strnlen("pi", 8) == 2 && strcmp("a", "b") < 0 &&
                             strcmp("b", "a") > 0 && strcmp("vault", "vault") == 0 &&
                             strncmp("vault101", "vault111", 5) == 0 &&
                             strncmp("vault101", "vault111", 7) < 0);

    check("str_chr", strchr("vault", 'u') != NULL && *strchr("vault", 'u') == 'u' &&
                         strchr("vault", 'z') == NULL && *strchr("vault", '\0') == '\0');

    check("str_lcpy", strlcpy(buf, "megaton", sizeof(buf)) == 7 && same(buf, "megaton") &&
                          strlcpy(buf, "megaton", 4) == 7 && same(buf, "meg"));
}

#define BLOCK   (256u * 1024u)
#define MAXPTR  64

static void heap_checks(void)
{
    static void *ptrs[MAXPTR];
    size_t start = pip_heap_free();
    unsigned char *p, *q;
    int n, i, ok;

    p = pip_malloc(1);
    q = pip_malloc(13);
    check("heap_align", p != NULL && q != NULL && ((uintptr_t)p & 7u) == 0 &&
                            ((uintptr_t)q & 7u) == 0 && p != q);
    pip_free(q);
    pip_free(p);

    p = pip_malloc(256);
    memset(p, 0xa5, 256);
    pip_free(p);
    q = pip_calloc(64, 4);
    ok = q != NULL;
    for (i = 0; ok && i < 256; i++) {
        ok = q[i] == 0;
    }
    pip_free(q);
    check("heap_calloc", ok && pip_calloc(0x10000u, 0x10000u) == NULL);

    for (n = 0; n < MAXPTR; n++) {
        ptrs[n] = pip_malloc(BLOCK);
        if (ptrs[n] == NULL) {
            break;
        }
        memset(ptrs[n], n, BLOCK);
    }
    check("heap_exhaust", n > 16 && n < MAXPTR && pip_heap_free() < BLOCK &&
                              pip_malloc(BLOCK) == NULL);

    for (i = 0; i < n; i++) {
        pip_free(ptrs[i]);
    }
    p = pip_malloc(start);
    check("heap_recover", pip_heap_free() == 0 && p != NULL);
    pip_free(p);
    check("heap_free", pip_heap_free() == start && start > 0x007e0000u);
}

int pip_main(const pipos_startup_t *startup)
{
    (void)startup;
    printf_checks();
    string_checks();
    heap_checks();
    pip_printf("@RC stage=testrom ev=checks total=%d failed=%d\n", total, failed);
    return failed;
}
