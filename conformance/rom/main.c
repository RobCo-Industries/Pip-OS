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

/*
 * Result slots: a full run records 208 checks. The last slot is kept for
 * the results_overflow FAIL, so a check that does not fit is never lost
 * without a trace.
 */
#define MAX_RESULTS   320
#define NAME_COLS     28          /* name characters shown on screen */
#define ROWS_PER_PAGE 12
#define ROW_H         PIP_FONT_H
#define TOP_Y         20
#define PAGE_WAIT_MS  600000u     /* interactive: a page turns by itself after 10 minutes */
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
static int nresults;      /* checks run: the n of the serial lines */
static int nstored;       /* results in the table */
static int ndropped;      /* checks that found no free slot */
static int overflow_row;   /* 1 while record_overflow() records its FAIL */
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

    /* The last slot holds only the overflow FAIL: see record_overflow(). */
    if (nstored < MAX_RESULTS - 1 || (overflow_row && nstored == MAX_RESULTS - 1)) {
        result_t *r = &results[nstored++];

        r->name = name;
        r->call = cur_call;
        r->res = (uint8_t)res;
        r->has_values = (uint8_t)has_values;
        r->got = got;
        r->expected = expected;
    } else {
        ndropped++;
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

/*
 * Checks that found no free slot make one FAIL in the reserved last slot:
 * on screen, on serial, in the counts and in the exit status. got is the
 * number of checks dropped from the pages.
 */
static void record_overflow(void)
{
    int dropped = ndropped;

    if (dropped == 0) {
        return;
    }
    cur_call = "none";
    overflow_row = 1;
    ck_res("results_overflow", R_FAIL, 1, dropped, 0);
    overflow_row = 0;
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
    int i, n = nstored;

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
    int i, first = 0, last = 0;

    pip_clear(0);
    pip_snprintf(line, sizeof(line), "RESULTS  page %d of %d", page + 1, pages);
    text(0, 0, fg, line, 1);
    pip_set_color(dim, PIP_COLOR_NONE);
    pip_draw_line(0, TOP_Y - 3, PIP_SCREEN_W - 1, TOP_Y - 3);
    for (i = 0; i < ROWS_PER_PAGE && page * ROWS_PER_PAGE + i < nrows; i++) {
        const row_t *row = &rows[page * ROWS_PER_PAGE + i];

        draw_row(TOP_Y + i * ROW_H, row);
        if (row->result >= 0 && !row->detail) {
            if (first == 0) {
                first = row->result + 1;
            }
            last = row->result + 1;
        }
    }
    pip_draw_line(0, PIP_SCREEN_H - 19, PIP_SCREEN_W - 1, PIP_SCREEN_H - 19);
    text(0, PIP_SCREEN_H - PIP_FONT_H, dim,
         auto_mode ? "Automatic run" : "DOWN next  UP back  SELECT summary", 1);
    /* Drop stale presses before the page shows: a press after this is an answer. */
    drain_events();
    pip_present();
    /* first and last: the row numbers of the checks this page shows. */
    pip_printf("@RC stage=conform ev=page n=%d of=%d first=%d last=%d last_name=%s\n",
               page + 1, pages, first, last, last ? results[last - 1].name : "-");
}

/* The page of row r. */
static int page_of_row(int r)
{
    return r / ROWS_PER_PAGE;
}

/* The first page with a FAIL, or -1. */
static int first_fail_page(void)
{
    int r;

    for (r = 0; r < nrows; r++) {
        if (rows[r].result >= 0 && results[rows[r].result].res == R_FAIL) {
            return page_of_row(r);
        }
    }
    return -1;
}

/* "RESULT: FAILED, see pages 2, 5" when it fits a line, else the first page only. */
static void fail_line(char *line, size_t size)
{
    int fail_pages[MAX_RESULTS * 3 / ROWS_PER_PAGE + 1];
    int r, i, n, count = 0;

    for (r = 0; r < nrows; r++) {
        int p = page_of_row(r);

        if (rows[r].result >= 0 && results[rows[r].result].res == R_FAIL &&
            (count == 0 || fail_pages[count - 1] != p)) {
            fail_pages[count++] = p;
        }
    }
    n = pip_snprintf(line, size, "RESULT: FAILED, see page%s", count > 1 ? "s" : "");
    for (i = 0; i < count; i++) {
        if (n + 5 > PIP_SCREEN_W / PIP_FONT_W) {
            pip_snprintf(line, size, "RESULT: FAILED, first on page %d", fail_pages[0] + 1);
            return;
        }
        n += pip_snprintf(line + n, size - (size_t)n, i ? ", %d" : " %d", fail_pages[i] + 1);
    }
}

/* Shows the summary. Returns the button pressed, or -1. */
static int show_summary(int pages)
{
    char line[48];
    int i;

    pip_clear(0);
    centered(4, fg, "CONFORMANCE", 2);
    for (i = 0; i < 4; i++) {
        pip_snprintf(line, sizeof(line), "%3d", counts[i]);
        text(56, 42 + i * 34, i == R_FAIL && counts[i] ? fg : i == R_PASS ? fg : dim,
             res_text[i], 2);
        text(PIP_SCREEN_W - 56 - 3 * 2 * PIP_FONT_W, 42 + i * 34,
             i == R_FAIL && counts[i] ? fg : i == R_PASS ? fg : dim, line, 2);
    }
    if (counts[R_FAIL]) {
        fail_line(line, sizeof(line));
    } else {
        pip_snprintf(line, sizeof(line), "RESULT: NO FAILURES");
    }
    centered(180, fg, line, 1);
    if (auto_mode) {
        centered(PIP_SCREEN_H - PIP_FONT_H, dim, "Automatic run", 1);
    } else {
        centered(204, dim, "UP or LEFT: back   SELECT: end", 1);
        centered(PIP_SCREEN_H - PIP_FONT_H, dim, "DOWN and RIGHT do nothing", 1);
    }
    drain_events();
    pip_present();
    pip_printf("@RC stage=conform ev=summary total=%d pass=%d fail=%d skip=%d absent=%d "
               "mode=%s pages=%d rows=%d\n", nresults, counts[R_PASS], counts[R_FAIL], counts[R_SKIP],
               counts[R_ABSENT], auto_mode ? "auto" : "interactive", pages, nstored);
    if (auto_mode) {
        pip_sleep_ms(AUTO_SUMMARY_MS);
        return -1;
    }
    for (;;) {
        int b = wait_press(SUMMARY_MS);

        if (b < 0 || b == PIP_BTN_SELECT || b == PIP_BTN_UP || b == PIP_BTN_LEFT) {
            return b;
        }
    }
}

/*
 * The results pages, then the summary. Interactive: the pages open on the
 * first FAIL. DOWN or RIGHT turns the page, UP or LEFT goes back, SELECT
 * jumps to the summary. From the summary, UP or LEFT goes back to the last
 * page and SELECT ends the run. Every wait ends after 10 minutes.
 */
static void show_results(void)
{
    int pages, page = 0;

    build_rows();
    pages = (nrows + ROWS_PER_PAGE - 1) / ROWS_PER_PAGE;
    if (!auto_mode && first_fail_page() > 0) {
        page = first_fail_page();
    }
    for (;;) {
        int b;

        if (page >= pages) {
            b = show_summary(pages);
            if (b != PIP_BTN_UP && b != PIP_BTN_LEFT) {
                return;
            }
            page = pages - 1;
            continue;
        }
        draw_page(page, pages);
        if (auto_mode) {
            pip_sleep_ms(AUTO_PAGE_MS);
            page++;
            continue;
        }
        b = wait_press(PAGE_WAIT_MS);
        if (b == PIP_BTN_SELECT) {
            page = pages;
        } else if (b == PIP_BTN_UP || b == PIP_BTN_LEFT) {
            if (page > 0) {
                page--;
            }
        } else {
            page++;
        }
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
    record_overflow();
    show_results();
    return counts[R_FAIL] ? 1 : 0;
}
