/*
 * INI parser of libpip.a, for small config files held in memory. It is
 * RobCo-Core's parser, src/lib/ini.c, under the pip_ prefix. No
 * allocation and no global state: every call is reentrant.
 *
 * Syntax, one item per line, LF or CR LF line ends:
 *   [section]          name of [A-Za-z0-9_.-], spaces around it allowed
 *   key = value        key of [A-Za-z0-9_.-]
 *   # comment          also ';', whole line or trailing
 * Keys before any section belong to the section "".
 * An unquoted value runs to the end of the line, or to a '#' or ';' that
 * starts the value or follows a space or tab. "a#b" keeps its '#'.
 * A value in double quotes may hold any text, with \" \\ and \n escapes.
 * Spaces and tabs around names and values are dropped.
 */
#ifndef PIPOS_INI_H
#define PIPOS_INI_H

#include <stddef.h>
#include <stdint.h>

#define PIP_INI_MAX_LINE        255     /* bytes per line, without the line end */
#define PIP_INI_MAX_SECTION     31      /* bytes per section name */
#define PIP_INI_MAX_KEY         31      /* bytes per key */
#define PIP_INI_MAX_VALUE       127     /* bytes per value, after escapes */

#define PIP_INI_STOPPED         1       /* the handler returned non-zero */

#define PIP_INI_ERR_ARG         (-1)    /* NULL text with a length, or no handler */
#define PIP_INI_ERR_SYNTAX      (-2)    /* line that is not a section, key or comment */
#define PIP_INI_ERR_TOO_LONG    (-3)    /* line, name, key or value above its limit */
#define PIP_INI_ERR_CHAR        (-4)    /* NUL, lone CR or other control byte */
#define PIP_INI_ERR_NOT_FOUND   (-5)    /* pip_ini_get*: no such key */
#define PIP_INI_ERR_SPACE       (-6)    /* pip_ini_get: value does not fit `out` */
#define PIP_INI_ERR_VALUE       (-7)    /* pip_ini_get_int/bool: not a number or a boolean */
#define PIP_INI_ERR_RANGE       (-8)    /* pip_ini_get_int: outside min..max */

/*
 * Called once per key in file order. The strings are NUL-terminated
 * copies that live until the handler returns. `line` counts from 1.
 * Return 0 to go on, non-zero to stop.
 */
typedef int (*pip_ini_handler_t)(const char *section, const char *key,
                                 const char *value, unsigned line, void *ctx);

/*
 * Parses `len` bytes of `text`. Returns 0 at the end, PIP_INI_STOPPED when
 * the handler stopped it, or a PIP_INI_ERR_* code. `err_line`, if not
 * NULL, receives the line of the error, or 0 when there is none.
 * Keys reach the handler up to the bad line: check the return code.
 */
int pip_ini_parse(const char *text, size_t len, pip_ini_handler_t fn, void *ctx,
                  unsigned *err_line);

/*
 * Copies the value of `key` in `section` ("" for keys before any section)
 * into `out`. The last one wins when a key repeats. The whole text must
 * parse. Returns 0, PIP_INI_ERR_NOT_FOUND, PIP_INI_ERR_SPACE or a parse error.
 */
int pip_ini_get(const char *text, size_t len, const char *section, const char *key,
                char *out, size_t out_size);

/* Decimal with an optional sign, or 0x hex. Checks min <= value <= max. */
int pip_ini_get_int(const char *text, size_t len, const char *section, const char *key,
                    int32_t min, int32_t max, int32_t *out);

/* true/false, yes/no, on/off, 1/0, any case. Stores 1 or 0. */
int pip_ini_get_bool(const char *text, size_t len, const char *section, const char *key,
                     int *out);

/* Short text for a PIP_INI_* code. */
const char *pip_ini_strerror(int code);

#endif
