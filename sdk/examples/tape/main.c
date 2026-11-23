/*
 * Beacon: a sample holotape. It shows its title and argument string,
 * grows a few rings, plays a short tune, then waits. UP and DOWN pick
 * the exit status, SELECT leaves with it and the ROM shows it.
 */
#include <pipos/pip.h>

#define TITLE      "BEACON"
#define RINGS      4
#define RING_STEP  12
#define CX         70
#define CY         150

static uint32_t fg, dim;

static const pip_note_t tune[] = {
    { 523, 120 }, { 659, 120 }, { 784, 120 }, { PIP_NOTE_REST, 60 }, { 1047, 240 },
};

/* The tower under the rings: a closed triangle with a mast. */
static void draw_tower(void)
{
    static const int16_t tower[] = { CX - 16, CY + 60, CX + 16, CY + 60, CX, CY + 8 };

    pip_set_color(fg, PIP_COLOR_NONE);
    pip_polyline(tower, 3, 1);
    pip_draw_line(CX, CY + 8, CX, CY);
    pip_rect_xywh(CX - 3, CY - 3, 7, 7, PIP_RECT_FILLED);
}

/* Rings 1 to n around the top of the mast, the last one bright. */
static void draw_rings(int n)
{
    pip_arc_t a = { CX, CY, 0, 0, 0, PIP_ARC_OUTLINE };
    int i;

    for (i = 1; i <= n; i++) {
        a.radius = (uint16_t)(i * RING_STEP);
        a.start_deg = 300;   /* the upper right quarter and a bit */
        a.end_deg = 360;
        pip_set_color(i == n ? fg : dim, PIP_COLOR_NONE);
        pip_arc(&a);
        a.start_deg = 180;
        a.end_deg = 240;
        pip_arc(&a);
    }
}

static void draw_status(int status)
{
    char line[40];

    pip_set_color(fg, 0);
    pip_snprintf(line, sizeof(line), "Exit status: %d ", status);
    pip_draw_text(150, 140, line, 1);
}

static void draw_screen(const char *args, int rings, int status)
{
    char line[39]; /* 38 characters fit inside the frame */

    pip_clear(0);
    pip_set_color(fg, PIP_COLOR_NONE);
    pip_rect_xywh(0, 0, 320, 240, PIP_RECT_OUTLINE);
    pip_draw_text(160 - (int32_t)PIP_TM_WIDTH(pip_text_metrics(TITLE, 3)) / 2, 10, TITLE, 3);
    pip_set_color(dim, PIP_COLOR_NONE);
    pip_draw_text(84, 60, "a sample holotape", 1);
    pip_snprintf(line, sizeof(line), "Args: %s", args[0] != '\0' ? args : "(none)");
    pip_draw_text(8, 84, line, 1);
    draw_tower();
    draw_rings(rings);
    if (status >= 0) {
        draw_status(status);
        pip_set_color(dim, PIP_COLOR_NONE);
        pip_draw_text(150, 164, "UP/DOWN: change", 1);
        pip_draw_text(150, 180, "SELECT: back to ROM", 1);
    }
    pip_present();
}

int pip_main(const pipos_startup_t *startup)
{
    pip_gfx_info_t gi;
    pip_event_t ev;
    int status = 3;
    int i;

    fg = pip_gfx_info(&gi) == 0 ? gi.fg_default : 0x33e066;
    dim = (fg >> 1) & 0x7f7f7f;
    pip_printf("beacon: started, args \"%s\"\n", startup->args);

    /* A short animation, then a tune that plays while the tape waits. */
    for (i = 0; i <= RINGS; i++) {
        draw_screen(startup->args, i, -1);
        pip_sleep_ms(150);
    }
    pip_play_seq(tune, sizeof(tune) / sizeof(tune[0]));
    draw_screen(startup->args, RINGS, status);
    pip_printf("beacon: ready\n");

    for (;;) {
        if (pip_wait_event(&ev, PIP_WAIT_FOREVER) != 1 || ev.type != PIP_EV_BUTTON ||
            ev.value == PIP_EV_RELEASE) {
            continue;
        }
        if (ev.code == PIP_BTN_UP && status < 255) {
            status++;
        } else if (ev.code == PIP_BTN_DOWN && status > 0) {
            status--;
        } else if (ev.code == PIP_BTN_SELECT) {
            break;
        }
        draw_status(status);
        pip_present();
    }
    pip_audio_stop();
    pip_printf("beacon: exit status %d\n", status);
    return status;
}
