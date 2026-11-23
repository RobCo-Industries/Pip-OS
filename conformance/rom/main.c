/*
 * The Pip-OS conformance ROM. It runs every system call of the ABI on
 * its main path and its error paths, asks a person to press buttons and
 * judge sound and picture, then shows a PASS/FAIL table and a summary.
 *
 * Mode: /PIPBOY/CONFORM.CFG starting with "auto" makes the run automatic.
 * Interactive steps are then SKIP. Without the file, each prompt waits
 * for an answer and becomes SKIP after its timeout.
 *
 * Exit status: 0 when no check failed, 1 otherwise.
 */
#include "conform.h"

#define MAX_RESULTS   192
#define NAME_COLS     28          /* name characters shown on screen */
#define ROWS_PER_PAGE 12
#define ROW_H         PIP_FONT_H
#define TOP_Y         20
#define PAGE_WAIT_MS  60000u      /* interactive: a page turns by itself after this */
#define AUTO_PAGE_MS  300u
#define SUMMARY_MS    600000u     /* interactive: the summary stays 10 minutes */
#define AUTO_SUMMARY_MS 1000u

int auto_mode;
int32_t first_last_status;

typedef struct {
    const char *name;
    const char *call;
    uint8_t res;
    uint8_t has_values;
    int32_t got, expected;
} result_t;

static result_t results[MAX_RESULTS];
static int nresults;
static int counts[4];
const char *cur_call = "-";
static uint32_t fg = 0x33E066, dim;

static const char *const res_text[4] = { "PASS", "FAIL", "SKIP", "ABSENT" };

typedef struct {
    uint32_t nr;
    const char *name;
    void (*check)(void);
} call_entry_t;

/* One entry per call of PIP_SYSCALLS: a missing check_<name> fails the link. */
#define CONFORM_ENTRY(NAME, name) { PIP_SYS_##NAME, #name, check_##name },
static const call_entry_t calls[] = { PIP_SYSCALLS(CONFORM_ENTRY) };
#undef CONFORM_ENTRY

_Static_assert(sizeof(calls) / sizeof(calls[0]) == PIP_SYSCALL_COUNT,
               "the conformance table must list every call of abi.h");

void ck_res(const char *name, int res, int has_values, int32_t got, int32_t expected)
{
    char line[PIP_LOG_MAX];
    int n;

    if (nresults < MAX_RESULTS) {
        result_t *r = &results[nresults];

        r->name = name;
        r->call = cur_call;
        r->res = (uint8_t)res;
        r->has_values = (uint8_t)has_values;
        r->got = got;
        r->expected = expected;
    }
    nresults++;
    counts[res]++;
    n = pip_snprintf(line, sizeof(line),
                     "@RC stage=conform ev=check n=%d call=%s name=%s result=%s", nresults,
                     cur_call, name, res_text[res]);
    if (has_values && n > 0 && n < (int)sizeof(line)) {
        pip_snprintf(line + n, sizeof(line) - (size_t)n, " got=%d expected=%d", (int)got,
                     (int)expected);
    }
    n = (int)strlen(line);
    if (n < (int)sizeof(line) - 1) {
        line[n] = '\n';
        line[n + 1] = '\0';
    }
    pip_log(line);
}

void ck(const char *name, int ok)
{
    ck_res(name, ok ? R_PASS : R_FAIL, 0, 0, 0);
}

void ck_eq(const char *name, int32_t got, int32_t expected)
{
    ck_res(name, got == expected ? R_PASS : R_FAIL, 1, got, expected);
}

/* ---- screen ---------------------------------------------------------------- */

static void text(int32_t x, int32_t y, uint32_t color, const char *s, uint32_t scale)
{
    pip_set_color(color, PIP_COLOR_NONE);
    pip_draw_text(x, y, s, scale);
}

static void centered(int32_t y, uint32_t color, const char *s, uint32_t scale)
{
    int32_t w = (int32_t)PIP_TM_WIDTH(pip_text_metrics(s, scale));

    text((PIP_SCREEN_W - w) / 2, y, color, s, scale);
}

static void progress(int step, int total, const char *call)
{
    char line[48];

    pip_clear(0);
    centered(8, fg, "PIP-OS CONFORMANCE", 2);
    pip_snprintf(line, sizeof(line), "Call %d of %d", step, total);
    centered(100, fg, line, 1);
    centered(120, dim, call, 1);
    pip_present();
}

/* ---- the run --------------------------------------------------------------- */

static void read_mode(const pipos_startup_t *su)
{
    char buf[8] = { 0 };
    int32_t h;

    auto_mode = strncmp(su->args, "auto", 4) == 0;
    h = pip_file_open("/PIPBOY/CONFORM.CFG");
    if (h >= 0) {
        if (pip_file_read(h, buf, 4) == 4 && memcmp(buf, "auto", 4) == 0) {
            auto_mode = 1;
        }
        pip_file_close(h);
    }
}

static void run_automatic(void)
{
    int total = (int)(sizeof(calls) / sizeof(calls[0]));
    int i;

    for (i = 0; i < total; i++) {
        progress(i + 1, total, calls[i].name);
        cur_call = calls[i].name;
        calls[i].check();
        /* A check may change the drawing state: reset it. */
        pip_set_clip(0, 0, 0);
        pip_set_font(PIP_FONT_SYSTEM);
    }
    cur_call = "none";
    check_unknown();
}

/* ---- results pages ------------------------------------------------------ */

/* Screen rows: one per result, one with got and expected under a FAIL, blanks (-1). */
typedef struct {
    int16_t result;
    uint8_t detail;
} row_t;

static row_t rows[MAX_RESULTS * 3];
static int nrows;

static void build_rows(void)
{
    int i, n = nresults < MAX_RESULTS ? nresults : MAX_RESULTS;

    nrows = 0;
    for (i = 0; i < n; i++) {
        int pair = results[i].res == R_FAIL && results[i].has_values;

        if (pair && nrows % ROWS_PER_PAGE == ROWS_PER_PAGE - 1) {
            /* Keep a FAIL and its values on one page: a blank row first. */
            rows[nrows].result = -1;
            rows[nrows++].detail = 0;
        }
        rows[nrows].result = (int16_t)i;
        rows[nrows++].detail = 0;
        if (pair) {
            rows[nrows].result = (int16_t)i;
            rows[nrows++].detail = 1;
        }
    }
}

static void draw_row(int y, const row_t *row)
{
    const result_t *r;
    char line[48];
    int n, i;

    if (row->result < 0) {
        return;
    }
    r = &results[row->result];
    if (row->detail) {
        pip_snprintf(line, sizeof(line), "    got %d, expected %d", (int)r->got,
                     (int)r->expected);
        text(0, y, fg, line, 1);
        return;
    }
    /* libpip's printf has no string width or precision: pad by hand. */
    n = pip_snprintf(line, sizeof(line), "%3d ", row->result + 1);
    for (i = 0; r->name[i] != '\0' && i < NAME_COLS; i++) {
        line[n++] = r->name[i];
    }
    for (i = n; i < 4 + NAME_COLS + 1; i++) {
        line[i] = i == n ? ' ' : '.';
    }
    line[i] = '\0';
    text(0, y, dim, line, 1);
    if (r->res == R_FAIL) {
        /* FAIL stands out: dark text on a lit cell. */
        pip_set_color(0, fg);
        pip_draw_text(PIP_SCREEN_W - 6 * PIP_FONT_W, y, "  FAIL", 1);
    } else {
        n = (int)strlen(res_text[r->res]);
        text(PIP_SCREEN_W - n * PIP_FONT_W, y, fg, res_text[r->res], 1);
    }
}

static void draw_page(int page, int pages)
{
    char line[48];
    int i;

    pip_clear(0);
    pip_snprintf(line, sizeof(line), "RESULTS  page %d of %d", page + 1, pages);
    text(0, 0, fg, line, 1);
    pip_set_color(dim, PIP_COLOR_NONE);
    pip_draw_line(0, TOP_Y - 3, PIP_SCREEN_W - 1, TOP_Y - 3);
    for (i = 0; i < ROWS_PER_PAGE && page * ROWS_PER_PAGE + i < nrows; i++) {
        draw_row(TOP_Y + i * ROW_H, &rows[page * ROWS_PER_PAGE + i]);
    }
    pip_draw_line(0, PIP_SCREEN_H - 19, PIP_SCREEN_W - 1, PIP_SCREEN_H - 19);
    text(0, PIP_SCREEN_H - PIP_FONT_H, dim,
         auto_mode ? "Automatic run" : "DOWN next  UP back  SELECT end", 1);
    /* Drop stale presses before the page shows: a press after this is an answer. */
    drain_events();
    pip_present();
    pip_printf("@RC stage=conform ev=page n=%d of=%d\n", page + 1, pages);
}

static void show_pages(void)
{
    int pages, page = 0;

    build_rows();
    pages = (nrows + ROWS_PER_PAGE - 1) / ROWS_PER_PAGE;
    while (page < pages) {
        int b;

        draw_page(page, pages);
        if (auto_mode) {
            pip_sleep_ms(AUTO_PAGE_MS);
            page++;
            continue;
        }
        b = wait_press(PAGE_WAIT_MS);
        if (b == PIP_BTN_SELECT) {
            break;
        }
        if ((b == PIP_BTN_UP || b == PIP_BTN_LEFT) && page > 0) {
            page--;
        } else if (b != PIP_BTN_UP && b != PIP_BTN_LEFT) {
            page++;
        }
    }
}

static void show_summary(void)
{
    char line[32];
    int i;

    pip_clear(0);
    centered(4, fg, "CONFORMANCE", 2);
    for (i = 0; i < 4; i++) {
        pip_snprintf(line, sizeof(line), "%3d", counts[i]);
        text(40, 44 + i * 36, i == R_FAIL && counts[i] ? fg : i == R_PASS ? fg : dim,
             res_text[i], 3);
        text(PIP_SCREEN_W - 40 - 3 * 3 * PIP_FONT_W, 44 + i * 36,
             i == R_FAIL && counts[i] ? fg : i == R_PASS ? fg : dim, line, 3);
    }
    centered(192, fg, counts[R_FAIL] ? "RESULT: FAILED" : "RESULT: NO FAILURES", 1);
    centered(PIP_SCREEN_H - PIP_FONT_H, dim, "Press any button to end", 1);
    drain_events();
    pip_present();
    pip_printf("@RC stage=conform ev=summary total=%d pass=%d fail=%d skip=%d absent=%d "
               "mode=%s\n", nresults, counts[R_PASS], counts[R_FAIL], counts[R_SKIP],
               counts[R_ABSENT], auto_mode ? "auto" : "interactive");
    if (auto_mode) {
        pip_sleep_ms(AUTO_SUMMARY_MS);
    } else {
        wait_press(SUMMARY_MS);
    }
}

int pip_main(const pipos_startup_t *startup)
{
    pip_gfx_info_t gi;

    /* Before anything launches a holotape. */
    first_last_status = pip_holo_last_status();
    if (pip_gfx_info(&gi) == 0) {
        fg = gi.fg_default;
    }
    dim = (fg >> 1) & 0x7F7F7Fu;
    read_mode(startup);
    pip_printf("@RC stage=conform ev=start mode=%s calls=%d\n",
               auto_mode ? "auto" : "interactive", PIP_SYSCALL_COUNT);
    run_automatic();
    run_interactive();
    show_pages();
    show_summary();
    return counts[R_FAIL] ? 1 : 0;
}
