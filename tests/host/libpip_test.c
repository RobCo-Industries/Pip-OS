/*
 * libpip.a on the host: pip_snprintf and pip_printf, the string
 * functions and the heap. The system calls are fakes below.
 */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "rc_test.h"

#include "libpip_names.h"
#include <pipos/heap.h>
#include <pipos/printf.h>
#include <pipos/string.h>
#include <pipos/syscall.h>
#define LIBPIP_NAMES_UNDO
#include "libpip_names.h"

/* ---- Fake system calls ---------------------------------------------- */

static char logged[4096];
static size_t logged_len;
static int log_calls;
static size_t log_longest;
static int32_t log_result;
static int mem_info_calls;

int32_t pip_log(const char *text)
{
    size_t n = strlen(text);

    log_calls++;
    if (n > log_longest) {
        log_longest = n;
    }
    if (log_result < 0) {
        return log_result;
    }
    if (logged_len + n < sizeof(logged)) {
        memcpy(logged + logged_len, text, n);
        logged_len += n;
        logged[logged_len] = '\0';
    }
    return 0;
}

/* Fails: a host address does not fit the 32-bit fields. */
int32_t pip_mem_info(pip_mem_info_t *out)
{
    mem_info_calls++;
    (void)out;
    return -PIP_EFAULT;
}

static void log_reset(void)
{
    logged[0] = '\0';
    logged_len = 0;
    log_calls = 0;
    log_longest = 0;
    log_result = 0;
}

/* ---- Heap: must run first, before any pip_heap_init ------------------ */

TEST(heap_lazy_setup)
{
    CHECK_EQ(mem_info_calls, 0);
    CHECK(pip_malloc(16) == NULL);          /* mem_info failed: empty heap */
    CHECK_EQ(mem_info_calls, 1);
    CHECK(pip_calloc(1, 16) == NULL);
    CHECK_EQ(pip_heap_free(), 0);
    CHECK_EQ(mem_info_calls, 1);            /* set up once */
}

/* ---- printf ---------------------------------------------------------- */

static char out[128];

#define FMT(expect, ...)                                                  \
    do {                                                                  \
        int n_ = pip_snprintf(out, sizeof(out), __VA_ARGS__);             \
        CHECK_STR(out, expect);                                           \
        CHECK_EQ(n_, strlen(expect));                                     \
    } while (0)

TEST(snprintf_conversions)
{
    FMT("42 -42 0", "%d %d %d", 42, -42, 0);
    FMT("-2147483648 2147483647", "%d %d", (int)0x80000000u, 0x7fffffff);
    FMT("4294967295 0", "%u %u", 0xffffffffu, 0u);
    FMT("beef BEEF", "%x %X", 0xbeefu, 0xbeefu);
    FMT("c=Z", "c=%c", 'Z');
    FMT("vault 101", "%s %d", "vault", 101);
    FMT("(null)", "%s", (char *)rc_launder(NULL));
    FMT("100%", "%d%%", 100);
}

/* Without the format attribute: an unknown conversion prints as written. */
static int vs(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = pip_vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

TEST(snprintf_unknown)
{
    CHECK_EQ(vs(out, sizeof(out), "a%qb%", 0), 4);
    CHECK_STR(out, "a%qb");
}

TEST(snprintf_width_and_zero)
{
    FMT("   42", "%5d", 42);
    FMT("00042", "%05d", 42);
    FMT("  -42", "%5d", -42);
    FMT("-0042", "%05d", -42);
    FMT("0000001a", "%08x", 0x1au);
    FMT("   ab", "%5s", "ab");
    FMT("abcdef", "%3s", "abcdef");
    FMT("12345", "%2d", 12345);
}

TEST(snprintf_pointer)
{
    char expect[64];
    int i, n = (int)(2 * sizeof(void *));
    uintptr_t v = (uintptr_t)0x1234u;

    expect[0] = '0';
    expect[1] = 'x';
    for (i = 0; i < n; i++) {
        expect[2 + i] = "0123456789abcdef"[(v >> (4 * (n - 1 - i))) & 0xf];
    }
    expect[2 + n] = '\0';
    FMT(expect, "%p", (void *)v);
}

TEST(snprintf_truncation)
{
    char buf[8];

    memset(buf, 'x', sizeof(buf));
    CHECK_EQ(pip_snprintf(buf, sizeof(buf), "%s", "0123456789"), 10);
    CHECK_STR(buf, "0123456");

    memset(buf, 'x', sizeof(buf));
    CHECK_EQ(pip_snprintf(buf, 1, "%d", 12345), 5);
    CHECK_EQ(buf[0], '\0');
    CHECK_EQ(buf[1], 'x');

    memset(buf, 'x', sizeof(buf));
    CHECK_EQ(pip_snprintf(buf, 0, "%d", 12345), 5);
    CHECK_EQ(buf[0], 'x');

    CHECK_EQ(pip_snprintf(buf, sizeof(buf), "%s", "1234567"), 7);
    CHECK_STR(buf, "1234567");
}

TEST(vsnprintf)
{
    char buf[16];

    CHECK_EQ(vs(buf, sizeof(buf), "%s-%u", "rob", 78u), 6);
    CHECK_STR(buf, "rob-78");
}

TEST(printf_short)
{
    log_reset();
    CHECK_EQ(pip_printf("pip %d\n", 3000), 9);
    CHECK_STR(logged, "pip 3000\n");
    CHECK_EQ(log_calls, 1);
    CHECK_EQ(pip_printf("%s", ""), 0);
    CHECK_EQ(log_calls, 1);
}

TEST(printf_splits_long_output)
{
    static char text[701];
    int i;

    for (i = 0; i < 700; i++) {
        text[i] = (char)('a' + i % 26);
    }
    text[700] = '\0';
    log_reset();
    CHECK_EQ(pip_printf("%s", text), 700);
    CHECK_STR(logged, text);
    CHECK_EQ(log_calls, 3);                 /* 255 + 255 + 190 */
    CHECK_EQ(log_longest, 255);

    log_reset();
    text[255] = '\0';
    CHECK_EQ(pip_printf("%s", text), 255);
    CHECK_EQ(log_calls, 1);
}

TEST(printf_cuts_at_max)
{
    static char text[2000];

    memset(text, 'q', sizeof(text) - 1);
    text[sizeof(text) - 1] = '\0';
    log_reset();
    CHECK_EQ(pip_printf("%s", text), PIP_PRINTF_MAX);
    CHECK_EQ(logged_len, PIP_PRINTF_MAX);
    CHECK_EQ(log_calls, 5);                 /* 4 * 255 + 3 */
}

TEST(printf_log_error)
{
    log_reset();
    log_result = -PIP_EFAULT;
    CHECK_EQ(pip_printf("x"), -PIP_EFAULT);
    log_reset();
}

/* ---- Strings ---------------------------------------------------------- */

TEST(mem_functions)
{
    unsigned char a[16], b[16];

    CHECK(pipt_memset(a, 0x5a, sizeof(a)) == a);
    CHECK_EQ(a[0], 0x5a);
    CHECK_EQ(a[15], 0x5a);
    pipt_memset(a, 1, 0);
    CHECK_EQ(a[0], 0x5a);

    CHECK(pipt_memcpy(b, "abcdefgh", 8) == b);
    CHECK_MEM(b, "abcdefgh", 8);
    pipt_memcpy(b, "zz", 0);
    CHECK_EQ(b[0], 'a');

    CHECK_EQ(pipt_memcmp("abc", "abc", 3), 0);
    CHECK(pipt_memcmp("abc", "abd", 3) < 0);
    CHECK(pipt_memcmp("abd", "abc", 3) > 0);
    CHECK(pipt_memcmp("\x80", "\x01", 1) > 0);  /* unsigned bytes */
    CHECK_EQ(pipt_memcmp("a", "b", 0), 0);
}

TEST(memmove_overlap)
{
    char m[16];

    strcpy(m, "0123456789");
    CHECK(pipt_memmove(m + 2, m, 8) == m + 2);
    CHECK_STR(m, "0101234567");

    strcpy(m, "0123456789");
    CHECK(pipt_memmove(m, m + 2, 8) == m);
    CHECK_STR(m, "2345678989");

    strcpy(m, "0123456789");
    pipt_memmove(m, m, 10);
    CHECK_STR(m, "0123456789");
    pipt_memmove(m + 1, m, 0);
    CHECK_STR(m, "0123456789");
}

TEST(str_length_and_compare)
{
    CHECK_EQ(pipt_strlen(""), 0);
    CHECK_EQ(pipt_strlen("pipboy"), 6);
    CHECK_EQ(pipt_strnlen("pipboy", 3), 3);
    CHECK_EQ(pipt_strnlen("pip", 10), 3);
    CHECK_EQ(pipt_strnlen("pip", 0), 0);
    CHECK_EQ(pipt_strnlen((const char *)rc_launder(NULL), 0), 0);

    CHECK_EQ(pipt_strcmp("vault", "vault"), 0);
    CHECK(pipt_strcmp("a", "b") < 0);
    CHECK(pipt_strcmp("b", "a") > 0);
    CHECK(pipt_strcmp("ab", "abc") < 0);
    CHECK(pipt_strcmp("\xff", "a") > 0);

    CHECK_EQ(pipt_strncmp("vault101", "vault111", 5), 0);
    CHECK(pipt_strncmp("vault101", "vault111", 7) < 0);
    CHECK_EQ(pipt_strncmp("a", "b", 0), 0);
    CHECK_EQ(pipt_strncmp("ab", "ab", 10), 0);
}

TEST(str_chr)
{
    const char *s = "megaton";

    CHECK(pipt_strchr(s, 'g') == s + 2);
    CHECK(pipt_strchr(s, 'm') == s);
    CHECK(pipt_strchr(s, 'z') == NULL);
    CHECK(pipt_strchr(s, '\0') == s + 7);
    CHECK(pipt_strchr("a\xe9", 0xe9) != NULL);
}

TEST(str_lcpy)
{
    char buf[8];

    memset(buf, 'x', sizeof(buf));
    CHECK_EQ(pipt_strlcpy(buf, "ok", sizeof(buf)), 2);
    CHECK_STR(buf, "ok");
    CHECK_EQ(pipt_strlcpy(buf, "megaton bomb", sizeof(buf)), 12);
    CHECK_STR(buf, "megaton");
    CHECK_EQ(pipt_strlcpy(buf, "1234567", sizeof(buf)), 7);
    CHECK_STR(buf, "1234567");
    memset(buf, 'x', sizeof(buf));
    CHECK_EQ(pipt_strlcpy(buf, "abc", 0), 3);
    CHECK_EQ(buf[0], 'x');
    CHECK_EQ(pipt_strlcpy(buf, "abc", 1), 3);
    CHECK_EQ(buf[0], '\0');
    CHECK_EQ(buf[1], 'x');
}

/* ---- Heap ------------------------------------------------------------- */

static _Alignas(16) unsigned char arena[64 * 1024];

TEST(heap_alloc_free_reuse)
{
    unsigned char *a, *b, *c;
    size_t start;

    pip_heap_init(arena, sizeof(arena));
    start = pip_heap_free();
    CHECK(start > sizeof(arena) - 64);
    CHECK(start < sizeof(arena));

    CHECK(pip_malloc(0) == NULL);
    a = pip_malloc(100);
    b = pip_malloc(100);
    CHECK(a != NULL && b != NULL && a != b);
    CHECK(a >= arena && a + 100 <= arena + sizeof(arena));
    CHECK(b >= a + 100 || a >= b + 100);
    memset(a, 0x11, 100);
    memset(b, 0x22, 100);
    CHECK_EQ(a[99], 0x11);
    CHECK(pip_heap_free() < start - 200);

    pip_free(a);
    c = pip_malloc(100);
    CHECK(c == a);                          /* first fit reuses the hole */
    CHECK_EQ(b[0], 0x22);
    pip_free(b);
    pip_free(c);
    pip_free(NULL);
    pip_free(arena + 8);                    /* not from the heap: ignored */
    CHECK_EQ(pip_heap_free(), start);
}

TEST(heap_alignment)
{
    size_t sizes[] = { 1, 3, 7, 8, 9, 15, 16, 17, 100, 1000 };
    size_t i;

    pip_heap_init(arena + 3, sizeof(arena) - 3);   /* odd base */
    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        void *p = pip_malloc(sizes[i]);

        CHECK(p != NULL);
        CHECK_EQ((uintptr_t)p & 7u, 0);
    }
}

TEST(heap_exhaustion_and_recovery)
{
    void *ptrs[64];
    size_t start;
    int n, i;

    pip_heap_init(arena, sizeof(arena));
    start = pip_heap_free();
    for (n = 0; n < 64; n++) {
        ptrs[n] = pip_malloc(4096);
        if (ptrs[n] == NULL) {
            break;
        }
    }
    CHECK(n >= 14 && n < 16);               /* 64 KiB of 4 KiB blocks plus headers */
    CHECK(pip_malloc(4096) == NULL);
    CHECK(pip_heap_free() < 4096);
    CHECK(pip_malloc(sizeof(arena)) == NULL);
    CHECK(pip_malloc((size_t)-1) == NULL);

    for (i = 0; i < n; i += 2) {
        pip_free(ptrs[i]);
    }
    CHECK(pip_heap_free() >= (size_t)(n / 2) * 4096);
    ptrs[0] = pip_malloc(4096);             /* a hole is reused */
    CHECK(ptrs[0] != NULL);
    for (i = 0; i < n; i++) {
        if (i % 2 == 1 || i == 0) {
            pip_free(ptrs[i]);
        }
    }
    CHECK_EQ(pip_heap_free(), start);       /* everything merged back */
    CHECK(pip_malloc(start) != NULL);       /* the whole heap in one block */
    CHECK_EQ(pip_heap_free(), 0);
    CHECK(pip_malloc(1) == NULL);
}

TEST(heap_free_merges)
{
    void *a, *b, *c;
    size_t start;

    pip_heap_init(arena, sizeof(arena));
    start = pip_heap_free();
    a = pip_malloc(20000);
    b = pip_malloc(20000);
    c = pip_malloc(20000);
    CHECK(a != NULL && b != NULL && c != NULL);
    CHECK(pip_malloc(20000) == NULL);
    pip_free(b);
    pip_free(a);
    pip_free(c);
    CHECK_EQ(pip_heap_free(), start);
    CHECK(pip_malloc(start) != NULL);
}

TEST(heap_calloc)
{
    unsigned char *p;
    size_t i;
    int zero = 1;

    pip_heap_init(arena, sizeof(arena));
    p = pip_malloc(512);
    memset(p, 0xa5, 512);
    pip_free(p);
    p = pip_calloc(128, 4);
    CHECK(p != NULL);
    for (i = 0; p != NULL && i < 512; i++) {
        zero &= p[i] == 0;
    }
    CHECK(zero);
    CHECK(pip_calloc(0, 16) == NULL);
    CHECK(pip_calloc((size_t)-1 / 2 + 2, 2) == NULL);   /* n * size overflows */
    CHECK(pip_calloc((size_t)1 << (sizeof(size_t) * 4), (size_t)1 << (sizeof(size_t) * 4)) == NULL);
    CHECK(pip_calloc(1024, 1024) == NULL);              /* fits size_t, not the heap */
}
