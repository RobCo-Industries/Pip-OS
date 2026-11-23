/*
 * Prints a greeting, the system and memory info, then lists the
 * holotapes on the card. The dial or UP and DOWN pick one, SELECT runs
 * it, LEFT ends the ROM with status 0.
 */
#include <pipos/pip.h>

#define MAX_TAPES  PIP_HOLO_ENUM_MAX
#define ROWS       7       /* tape rows on the screen */
#define ROW_Y      48
#define ROW_H      18
#define TEXT_MAX   36      /* characters of a tape name on a row */

static pip_holo_info_t tapes[MAX_TAPES];
static int count;          /* tapes listed, or -PIP_E* */
static int sel, top;       /* selected tape, first row shown */
static int runs;
static char result[48];    /* what the last launch gave */
static uint32_t fg, dim;

static void print_info(const pipos_startup_t *startup)
{
    pip_sys_info_t si;
    pip_mem_info_t mi;
    char *buf;
    int32_t t0;

    pip_printf("hello: Pip-OS ROM running, ABI 0x%08x\n", (unsigned)startup->abi_version);
    if (pip_sys_info(&si) == 0) {
        pip_printf("hello: Pip-OS %u.%u.%u, soc 0x%x, %u MiB RAM\n",
                   (unsigned)(si.pipos_version >> 16), (unsigned)((si.pipos_version >> 8) & 0xff),
                   (unsigned)(si.pipos_version & 0xff), (unsigned)si.soc,
                   (unsigned)(si.mem_size >> 20));
    }
    if (pip_mem_info(&mi) == 0) {
        pip_printf("hello: heap 0x%08x, %u KiB, stack top 0x%08x\n", (unsigned)mi.heap_base,
                   (unsigned)(mi.heap_size >> 10), (unsigned)mi.stack_top);
    }
    buf = pip_malloc(64);
    if (buf != NULL) {
        strlcpy(buf, "This line came from the heap.", 64);
        pip_printf("hello: %s\n", buf);
        pip_free(buf);
    }
    t0 = pip_ticks_ms();
    pip_sleep_ms(500);
    pip_printf("hello: slept %d ms\n", (int)(pip_ticks_ms() - t0));
}

/* The title from the META block, or the file name when it is empty. */
static const char *tape_label(const pip_holo_info_t *t)
{
    return t->title[0] != '\0' ? t->title : t->file;
}

static void describe(int32_t rc, char *out, size_t size)
{
    switch (rc) {
    case -PIP_EFAULT:  strlcpy(out, "killed by Pip-OS", size); break;
    case -PIP_ENOEXEC: strlcpy(out, "not a holotape", size); break;
    case -PIP_EIO:     strlcpy(out, "card error", size); break;
    case -PIP_ENOENT:  strlcpy(out, "tape not found", size); break;
    default:
        if (rc >= 0) {
            pip_snprintf(out, size, "exit status %d", (int)rc);
        } else {
            pip_snprintf(out, size, "error %d", (int)rc);
        }
    }
}

static void draw_row(int i, int y)
{
    char text[TEXT_MAX + 1];

    strlcpy(text, tape_label(&tapes[i]), sizeof(text));
    if (i == sel) {
        pip_set_color(fg, PIP_COLOR_NONE);
        pip_rect_xywh(4, y - 1, 312, ROW_H, PIP_RECT_FILLED);
        pip_set_color(0, PIP_COLOR_NONE);
    } else {
        pip_set_color(fg, PIP_COLOR_NONE);
    }
    pip_draw_text(12, y, text, 1);
}

/* The selected tape's icon from its META block, at the top right. */
static void draw_icon(const pip_holo_info_t *t)
{
    pip_blit_t b = { 284, 4, 32, 32, 4, 0, (uint32_t)(uintptr_t)t->icon, 0 };

    b.color = fg;
    pip_blit_1bpp(&b);
}

static void draw_menu(void)
{
    char line[48];
    int i;

    pip_clear(0);
    pip_set_color(fg, PIP_COLOR_NONE);
    pip_draw_text(8, 4, "HELLO FROM THE SDK", 1);
    pip_set_color(dim, PIP_COLOR_NONE);
    pip_draw_text(8, 20, "Holotapes on the card", 1);
    pip_draw_line(0, 40, 319, 40);
    pip_draw_line(0, 204, 319, 204);

    if (count < 0) {
        pip_set_color(fg, PIP_COLOR_NONE);
        pip_draw_text(12, ROW_Y, "Card error, no list", 1);
    } else if (count == 0) {
        pip_set_color(fg, PIP_COLOR_NONE);
        pip_draw_text(12, ROW_Y, "No holotapes in /PIPBOY/HOLO", 1);
    } else {
        draw_icon(&tapes[sel]);
        for (i = top; i < count && i < top + ROWS; i++) {
            draw_row(i, ROW_Y + (i - top) * ROW_H);
        }
    }
    if (result[0] != '\0') {
        pip_snprintf(line, sizeof(line), "Last run: %s", result);
        pip_set_color(fg, PIP_COLOR_NONE);
        pip_draw_text(8, 184, line, 1);
    }
    pip_set_color(dim, PIP_COLOR_NONE);
    pip_draw_text(8, 208, "DIAL or UP/DOWN: pick a tape", 1);
    pip_draw_text(8, 224, "SELECT: run      LEFT: quit", 1);
    pip_present();
    pip_printf("hello: menu, %d tapes, tape %d selected\n", count, count > 0 ? sel + 1 : 0);
}

static void move(int by)
{
    if (count <= 0) {
        return;
    }
    sel = ((sel + by) % count + count) % count;
    if (sel < top) {
        top = sel;
    } else if (sel >= top + ROWS) {
        top = sel - ROWS + 1;
    }
}

static void run_selected(void)
{
    char args[PIP_ARGS_SIZE];
    int32_t rc;

    if (count <= 0) {
        return;
    }
    runs++;
    pip_snprintf(args, sizeof(args), "from hello, run %d", runs);
    pip_printf("hello: launching %s\n", tapes[sel].file);
    rc = pip_holo_launch(tapes[sel].file, args);
    describe(rc, result, sizeof(result));
    pip_printf("hello: %s: %s\n", tapes[sel].file, result);
}

int pip_main(const pipos_startup_t *startup)
{
    pip_gfx_info_t gi;
    pip_event_t ev;

    print_info(startup);

    fg = pip_gfx_info(&gi) == 0 ? gi.fg_default : 0x33e066;
    dim = (fg >> 1) & 0x7f7f7f;
    count = pip_holo_enumerate(tapes, MAX_TAPES);
    pip_printf("hello: %d holotapes on the card\n", count);
    draw_menu();

    for (;;) {
        if (pip_wait_event(&ev, PIP_WAIT_FOREVER) != 1) {
            continue;
        }
        if (ev.type == PIP_EV_DIAL) {
            move(ev.value);
        } else if (ev.type != PIP_EV_BUTTON || ev.value == PIP_EV_RELEASE) {
            continue;
        } else if (ev.code == PIP_BTN_UP) {
            move(-1);
        } else if (ev.code == PIP_BTN_DOWN) {
            move(1);
        } else if (ev.code == PIP_BTN_SELECT) {
            run_selected();
        } else if (ev.code == PIP_BTN_LEFT) {
            break;
        } else {
            continue;
        }
        draw_menu();
    }
    pip_printf("hello: bye\n");
    return 0;
}
