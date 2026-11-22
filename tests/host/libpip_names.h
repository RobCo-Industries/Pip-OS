/*
 * Host build of libpip.a: its C library functions get a pipt_ prefix so
 * they do not replace the host libc's. The Makefile force-includes this
 * file into the library sources. libpip_test.c includes it around the
 * SDK headers only, then again with LIBPIP_NAMES_UNDO.
 */
#ifndef LIBPIP_NAMES_UNDO
#define memcpy  pipt_memcpy
#define memmove pipt_memmove
#define memset  pipt_memset
#define memcmp  pipt_memcmp
#define strlen  pipt_strlen
#define strnlen pipt_strnlen
#define strcmp  pipt_strcmp
#define strncmp pipt_strncmp
#define strchr  pipt_strchr
#define strlcpy pipt_strlcpy
#else
#undef memcpy
#undef memmove
#undef memset
#undef memcmp
#undef strlen
#undef strnlen
#undef strcmp
#undef strncmp
#undef strchr
#undef strlcpy
#endif
