/* The INI parser on top of RobCo-Core's ini.c, built into the same archive. */
#include <robco/ini.h>

#include <pipos/ini.h>

_Static_assert(PIP_INI_MAX_LINE == INI_MAX_LINE, "INI limits differ");
_Static_assert(PIP_INI_MAX_SECTION == INI_MAX_SECTION, "INI limits differ");
_Static_assert(PIP_INI_MAX_KEY == INI_MAX_KEY, "INI limits differ");
_Static_assert(PIP_INI_MAX_VALUE == INI_MAX_VALUE, "INI limits differ");
_Static_assert(PIP_INI_STOPPED == INI_STOPPED, "INI codes differ");
_Static_assert(PIP_INI_ERR_ARG == INI_ERR_ARG, "INI codes differ");
_Static_assert(PIP_INI_ERR_SYNTAX == INI_ERR_SYNTAX, "INI codes differ");
_Static_assert(PIP_INI_ERR_TOO_LONG == INI_ERR_TOO_LONG, "INI codes differ");
_Static_assert(PIP_INI_ERR_CHAR == INI_ERR_CHAR, "INI codes differ");
_Static_assert(PIP_INI_ERR_NOT_FOUND == INI_ERR_NOT_FOUND, "INI codes differ");
_Static_assert(PIP_INI_ERR_SPACE == INI_ERR_SPACE, "INI codes differ");
_Static_assert(PIP_INI_ERR_VALUE == INI_ERR_VALUE, "INI codes differ");
_Static_assert(PIP_INI_ERR_RANGE == INI_ERR_RANGE, "INI codes differ");

int pip_ini_parse(const char *text, size_t len, pip_ini_handler_t fn, void *ctx,
                  unsigned *err_line)
{
    return ini_parse(text, len, fn, ctx, err_line);
}

int pip_ini_get(const char *text, size_t len, const char *section, const char *key,
                char *out, size_t out_size)
{
    return ini_get(text, len, section, key, out, out_size);
}

int pip_ini_get_int(const char *text, size_t len, const char *section, const char *key,
                    int32_t min, int32_t max, int32_t *out)
{
    return ini_get_int(text, len, section, key, min, max, out);
}

int pip_ini_get_bool(const char *text, size_t len, const char *section, const char *key,
                     int *out)
{
    return ini_get_bool(text, len, section, key, out);
}

const char *pip_ini_strerror(int code)
{
    return ini_strerror(code);
}
