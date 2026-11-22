/*
 * The save folder reached through an 8.3 alias. test_storage.py gives the
 * PIPBOY or the SAVE directory entry an alias that is not its name and
 * writes both aliases to /CARD.TXT as "<PIPBOY alias> <SAVE alias>".
 */
#include "rt.h"

static pip_dirent_t ents[PIP_DIR_LIST_MAX];
static char card[40], pip[13], save[13];
static char path[80];
static uint8_t back[64];

static const char *at(const char *fmt, const char *a, const char *b)
{
    pip_snprintf(path, sizeof(path), fmt, a, b);
    return path;
}

static const char *lower(const char *s)
{
    static char out[80];
    unsigned i;

    for (i = 0; s[i] != '\0' && i + 1 < sizeof(out); i++) {
        out[i] = s[i] >= 'A' && s[i] <= 'Z' ? (char)(s[i] - 'A' + 'a') : s[i];
    }
    out[i] = '\0';
    return out;
}

static int listed(int32_t n, const char *name, int dir)
{
    for (int32_t i = 0; i < n; i++) {
        if (strcmp(ents[i].name, name) == 0) {
            return (ents[i].flags & PIP_DIRENT_DIR) == (dir ? PIP_DIRENT_DIR : 0);
        }
    }
    return 0;
}

static int read_card(void)
{
    int32_t h = pip_file_open("/CARD.TXT");
    int32_t n = h >= 0 ? pip_file_read(h, card, sizeof(card) - 1) : -1;
    char *sp;

    if (h >= 0) {
        pip_file_close(h);
    }
    if (n <= 0) {
        return 0;
    }
    card[n] = '\0';
    sp = strchr(card, ' ');
    if (sp == NULL || sp - card > 12 || strlen(sp + 1) > 12) {
        return 0;
    }
    *sp = '\0';
    strlcpy(pip, card, sizeof(pip));
    strlcpy(save, sp + 1, sizeof(save));
    return 1;
}

/* Every entry of /<pip> works by the name dir_list reports, except the save folder. */
static int listed_usable(int32_t n)
{
    static pip_dirent_t copy[PIP_DIR_LIST_MAX];
    int ok = n > 0;

    memcpy(copy, ents, sizeof(copy));
    for (int32_t i = 0; i < n; i++) {
        const char *p = at("/%s/%s", pip, copy[i].name);
        int is_save = strcmp(copy[i].name, save) == 0;
        int32_t rc;

        if (copy[i].flags & PIP_DIRENT_DIR) {
            rc = pip_dir_list(p, ents, PIP_DIR_LIST_MAX, 0);
        } else {
            rc = pip_file_open(p);
            if (rc >= 0) {
                pip_file_close(rc);
            }
        }
        ok = ok && (is_save ? rc == -PIP_EPERM : rc >= 0);
    }
    return ok;
}

int pip_main(const pipos_startup_t *startup)
{
    int32_t n, h;

    (void)startup;
    rt_check("card", read_card());
    n = pip_dir_list("/", ents, PIP_DIR_LIST_MAX, 0);
    rt_check("root_lists_pip", listed(n, pip, 1));
    n = pip_dir_list(at("/%s", pip, ""), ents, PIP_DIR_LIST_MAX, 0);
    rt_check("pip_lists_save", listed(n, save, 1) && listed(n, "LONGFO~1", 1));
    rt_check("listed_usable", listed_usable(n));

    rt_check_eq("ls_alias", pip_dir_list(at("/%s/%s", pip, save), ents, 4, 0), -PIP_EPERM);
    rt_check_eq("ls_alias_lower",
                pip_dir_list(lower(at("/%s/%s", pip, save)), ents, 4, 0), -PIP_EPERM);
    rt_check_eq("open_alias", pip_file_open(at("/%s/%s/A.SAV", pip, save)), -PIP_EPERM);
    rt_check_eq("open_alias_pip", pip_file_open(at("/%s/SAVE/A.SAV", pip, "")), -PIP_EPERM);
    rt_check_eq("open_alias_save", pip_file_open(at("/PIPBOY/%s/A.SAV", save, "")), -PIP_EPERM);
    rt_check_eq("ls_plain", pip_dir_list("/PIPBOY/SAVE", ents, 4, 0), -PIP_EPERM);
    rt_check_eq("open_plain", pip_file_open("/pipboy/save/a.sav"), -PIP_EPERM);

    /* Ordinary folders reached through their own aliases. */
    rt_check_eq("ls_neighbour", pip_dir_list(at("/%s/LONGFO~1", pip, ""), ents, 4, 0), 1);
    rt_check("ls_neighbour_name", strcmp(ents[0].name, "N.TXT") == 0);
    h = pip_file_open(at("/%s/longfo~1/n.txt", pip, ""));
    rt_check("open_neighbour", h >= 0 && pip_file_read(h, back, sizeof(back)) == 10 &&
                               memcmp(back, "neighbour\n", 10) == 0);
    pip_file_close(h);
    h = pip_file_open("/SAVEGA~1/G.TXT");
    rt_check("open_savega", h >= 0);
    pip_file_close(h);

    /* Saves still work on this card. */
    memset(back, 0, sizeof(back));
    rt_check("save_read", pip_read_save("A", back, sizeof(back)) == 6 &&
                          memcmp(back, "secret", 6) == 0);
    rt_check_eq("save_write", pip_write_save("ALIAS", "fresh", 5), 5);
    memset(back, 0, sizeof(back));
    rt_check("save_read_back", pip_read_save("alias", back, sizeof(back)) == 5 &&
                               memcmp(back, "fresh", 5) == 0);
    return rt_checks_done();
}
