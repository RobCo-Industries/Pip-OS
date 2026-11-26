#include "bootlines.h"

#include <robco/klibc.h>
#include <robco/rimg.h>

const char *const pipos_boot_head[] = {
    "*************** PIP-OS(R) V7.1.0.8 ***************",
    "COPYRIGHT 2075 ROBCO IND.",
    "**************************************************",
    "",
    "LOADER V1.1",
    "EXEC VERSION 41.10",
    "64K RAM SYSTEM",
    "38911 BYTES FREE",
    NULL,
};

const char *const pipos_boot_tail[] = {
    "",
    "**************************************************",
    "PIP-OS KERNEL INITIALIZED",
    "PIP-OS READY",
    "**************************************************",
    NULL,
};

int pipos_fmt_version(uint32_t version, char *buf, size_t size)
{
    return k_snprintf(buf, size, "%u.%u.%u", (unsigned)(version >> 16),
                      (unsigned)((version >> 8) & 0xffu), (unsigned)(version & 0xffu));
}

void pipos_fmt_name(const char *raw, size_t raw_len, char *out, size_t size, int upper)
{
    size_t i;
    size_t n = 0;

    if (size == 0) {
        return;
    }
    for (i = 0; i < raw_len && raw[i] != '\0' && n + 1 < size; i++) {
        char c = raw[i];

        if (c <= ' ' || c > '~') {
            c = '_';
        } else if (upper && c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        out[n++] = c;
    }
    if (n == 0 && size > 1) {
        out[n++] = '-';
    }
    out[n] = '\0';
}

int pipos_holo_line(unsigned count, char *buf, size_t size)
{
    if (count == 0) {
        return k_snprintf(buf, size, "NO HOLOTAPE FOUND");
    }
    return k_snprintf(buf, size, "%u HOLOTAPE%s FOUND", count, count == 1 ? "" : "S");
}

int pipos_rom_line(const char *name, uint32_t version, char *buf, size_t size)
{
    char v[24];

    pipos_fmt_version(version, v, sizeof(v));
    return k_snprintf(buf, size, "LOAD ROM(1): %s %s", name, v);
}

int pipos_rom_error_line(int card, int rc, char *buf, size_t size)
{
    const char *why;

    if (!card) {
        why = "NO SD CARD";
    } else {
        switch (rc) {
        case RIMG_E_NOFILE:  why = "NO FILE /PIPBOY/ROM1.ROM"; break;
        case RIMG_E_IO:      why = "READ ERROR"; break;
        case RIMG_E_MAGIC:   why = "NOT A ROM IMAGE"; break;
        case RIMG_E_VERSION: why = "UNKNOWN HEADER VERSION"; break;
        case RIMG_E_SIZE:    why = "FILE SIZE MISMATCH"; break;
        case RIMG_E_BOUNDS:  why = "DOES NOT FIT THE ROM SLOT"; break;
        case RIMG_E_ABI:     why = "WRONG ABI VERSION"; break;
        case RIMG_E_CRC:     why = "CHECKSUM MISMATCH"; break;
        default:             why = "UNKNOWN ERROR"; break;
        }
    }
    return k_snprintf(buf, size, "ROM(1) NOT LOADED: %s", why);
}

int pipos_is_holotape_name(const char *name)
{
    size_t n = k_strlen(name);
    const char *ext;

    if (n < 5) {
        return 0;
    }
    ext = name + n - 4;
    return ext[0] == '.' && (ext[1] | 0x20) == 'h' && (ext[2] | 0x20) == 't' &&
           (ext[3] | 0x20) == 'p';
}

void pipos_marker_word(const char *in, char *out, size_t size)
{
    size_t n = 0;

    if (size == 0) {
        return;
    }
    for (; in != NULL && *in != '\0' && n + 1 < size; in++) {
        char c = *in;

        out[n++] = (c < '!' || c > '~') ? '_' : c;
    }
    if (n == 0 && size > 1) {
        out[n++] = '-';
    }
    out[n] = '\0';
}

int pipos_line_blank(const char *text)
{
    for (; *text != '\0'; text++) {
        if (*text > 0x20 && *text < 0x7f) {
            return 0;
        }
    }
    return 1;
}
