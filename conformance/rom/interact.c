/*
 * The interactive part: a person presses buttons, turns the dial and
 * judges sound and picture. Each prompt waits PROMPT_MS, then the check
 * is SKIP. A wrong answer is FAIL. In auto mode every check here is SKIP
 * without a prompt.
 *
 * Each prompt drops stale events, shows its screen, then prints
 * "@RC stage=conform ev=prompt name=<check>", so a test script knows when
 * to answer. Nothing drains after the marker: an answer is never lost.
 */
#include "conform.h"

#define PROMPT_MS   30000u
#define DIAL_QUIET  500u     /* the dial is still for this long: the turn is over */

static uint32_t fg = 0x33E066, dim;

void drain_events(void)
{
    pip_event_t ev;
    int i;

    for (i = 0; i < 256 && pip_poll_event(&ev) == 1; i++) {
    }
}

/* Waits for one event until the deadline. Returns 1 with *ev, or 0. */
static int next_event(pip_event_t *ev, int32_t start, uint32_t ms)
{
    int32_t left = (int32_t)ms - elapsed_ms(start, pip_ticks_ms());

    if (left <= 0) {
        return pip_wait_event(ev, 0) == 1;
    }
    return pip_wait_event(ev, (uint32_t)left) == 1;
}

int wait_press(uint32_t ms)
{
    pip_event_t ev;
    int32_t start = pip_ticks_ms();

    while (next_event(&ev, start, ms)) {
        if (ev.type == PIP_EV_BUTTON && ev.value == PIP_EV_PRESS) {
            return (int)ev.code;
        }
    }
    return -1;
}

/*
 * Drain, read the dial, then poll: input comes in at each call, so an empty
 * queue after the read means no turn is both in the result and queued. An
 * event means a turn came in around the read: start again.
 */
static int32_t dial_baseline(void)
{
    pip_event_t ev;
    int32_t d;
    int i;

    for (i = 0; i < 64; i++) {
        drain_events();
        d = pip_read_dial();
        if (pip_poll_event(&ev) != 1) {
            return d;
        }
    }
    return d;   /* the dial never stopped; the position check may be off */
}

/* Shows a prompt. With d0 not NULL, *d0 is the dial baseline taken with the drain. */
static void prompt_dial(const char *name, const char *l1, const char *l2, const char *hint,
                        int32_t *d0)
{
    int32_t w;

    pip_clear(0);
    pip_set_color(fg, PIP_COLOR_NONE);
    w = (int32_t)PIP_TM_WIDTH(pip_text_metrics("CHECK BY HAND", 2));
    pip_draw_text((PIP_SCREEN_W - w) / 2, 8, "CHECK BY HAND", 2);
    pip_draw_text(8, 80, l1, 1);
    if (l2 != NULL) {
        pip_draw_text(8, 100, l2, 1);
    }
    pip_set_color(dim, PIP_COLOR_NONE);
    pip_draw_text(8, 184, hint, 1);
    pip_draw_text(8, 204, "No answer in 30 s: SKIP", 1);
    if (d0 != NULL) {
        *d0 = dial_baseline();
    } else {
        drain_events();
    }
    pip_present();
    pip_printf("@RC stage=conform ev=prompt name=%s\n", name);
}

static void prompt(const char *name, const char *l1, const char *l2, const char *hint)
{
    prompt_dial(name, l1, l2, hint, NULL);
}

static void skip(const char *name)
{
    ck_res(name, R_SKIP, 0, 0, 0);
}

static const struct {
    const char *name;
    const char *label;
    int code;
} buttons[] = {
    { "btn_up", "Press UP", PIP_BTN_UP },
    { "btn_down", "Press DOWN", PIP_BTN_DOWN },
    { "btn_left", "Press LEFT", PIP_BTN_LEFT },
    { "btn_right", "Press RIGHT", PIP_BTN_RIGHT },
    { "btn_select", "Press SELECT", PIP_BTN_SELECT },
    { "btn_dial_press", "Press the dial in", PIP_BTN_DIAL },
};

/* PASS when the asked button comes, its stamp not after ticks_ms. Other buttons are ignored. */
static void ask_button(int i)
{
    pip_event_t ev;
    int32_t start;

    prompt(buttons[i].name, buttons[i].label, NULL, "Other buttons are ignored.");
    start = pip_ticks_ms();
    while (next_event(&ev, start, PROMPT_MS)) {
        if (ev.type == PIP_EV_BUTTON && ev.value == PIP_EV_PRESS && (int)ev.code == buttons[i].code) {
            int32_t late = elapsed_ms((int32_t)ev.time_ms, pip_ticks_ms());

            /* The stamp is never after the ticks_ms read after the event. */
            ck_res(buttons[i].name, late < 0x40000000 ? R_PASS : R_FAIL, 1, late, 0);
            return;
        }
    }
    skip(buttons[i].name);
}

/* The dial position pos moved by detents, 0 to 359. */
static int32_t dial_add(int32_t pos, int32_t detents)
{
    return ((pos + detents % 24 * PIP_DIAL_DETENT_DEG) % 360 + 360) % 360;
}

/*
 * One turn of the dial in direction dir (1 or -1). Sums the detents until
 * the dial is still, then compares the change of read_dial.
 */
static void ask_dial(const char *name, const char *pos_name, const char *label, int dir)
{
    pip_event_t ev;
    int32_t start, d0, d1, sum = 0, want;
    int got = 0;

    /* The baseline comes before the screen: a turn after it is a queued event. */
    prompt_dial(name, label, "by a few clicks.", "Turning the other way is ignored.", &d0);
    start = pip_ticks_ms();
    while (!got && next_event(&ev, start, PROMPT_MS)) {
        if (ev.type == PIP_EV_DIAL && ev.value * dir > 0) {
            got = 1;
            sum = ev.value;
        } else if (ev.type == PIP_EV_DIAL) {
            d0 = dial_add(d0, ev.value);   /* the wrong way: ignored, but read_dial counts it */
        }
    }
    if (!got) {
        skip(name);
        skip(pos_name);
        return;
    }
    ck(name, 1);
    while (pip_wait_event(&ev, DIAL_QUIET) == 1) {
        if (ev.type == PIP_EV_DIAL) {
            sum += ev.value;
        }
    }
    d1 = pip_read_dial();
    want = dial_add(d0, sum);
    ck_eq(pos_name, d1, want);
}

/* A yes or no question. SELECT is yes, LEFT is no. */
static void ask_yes_no(const char *name, const char *l1, const char *l2)
{
    int32_t start;
    pip_event_t ev;

    prompt(name, l1, l2, "SELECT = yes    LEFT = no");
    start = pip_ticks_ms();
    while (next_event(&ev, start, PROMPT_MS)) {
        if (ev.type != PIP_EV_BUTTON || ev.value != PIP_EV_PRESS) {
            continue;
        }
        if (ev.code == PIP_BTN_SELECT) {
            ck(name, 1);
            pip_audio_stop();
            return;
        }
        if (ev.code == PIP_BTN_LEFT) {
            ck(name, 0);
            pip_audio_stop();
            return;
        }
    }
    pip_audio_stop();
    skip(name);
}

void draw_scene(void)
{
    static const int16_t tri[] = { 230, 150, 290, 150, 260, 100 };
    pip_arc_t circle = { 80, 120, 40, 0, 360, PIP_ARC_OUTLINE };

    pip_clear(0);
    pip_set_color(fg, PIP_COLOR_NONE);
    pip_rect_xywh(0, 0, PIP_SCREEN_W, PIP_SCREEN_H, PIP_RECT_OUTLINE);
    pip_arc(&circle);
    pip_rect_xywh(140, 100, 50, 50, PIP_RECT_FILLED);
    pip_polyline(tri, 3, 1);
    pip_draw_line(10, 170, 309, 170);
    pip_draw_text(8, 8, "Is the picture right?", 1);
    pip_set_color(dim, PIP_COLOR_NONE);
    pip_draw_text(8, 180, "A frame, a circle, a filled square", 1);
    pip_draw_text(8, 196, "and a triangle above one line.", 1);
    pip_draw_text(8, 216, "SELECT = yes    LEFT = no", 1);
}

static void ask_picture(void)
{
    int b;

    draw_scene();
    drain_events();
    pip_present();
    pip_printf("@RC stage=conform ev=prompt name=picture_right\n");
    for (;;) {
        b = wait_press(PROMPT_MS);
        if (b == PIP_BTN_SELECT || b == PIP_BTN_LEFT || b < 0) {
            break;
        }
    }
    if (b < 0) {
        skip("picture_right");
    } else {
        ck("picture_right", b == PIP_BTN_SELECT);
    }
}

void run_interactive(void)
{
    static const pip_note_t tune[] = {
        { 392, 200 }, { 523, 200 }, { 659, 200 }, { 784, 400 },
        { PIP_NOTE_REST, 200 }, { 392, 200 }, { 523, 200 }, { 659, 200 }, { 784, 400 },
    };
    static uint8_t pcm[8000];
    static const char *const names[] = {
        "btn_up", "btn_down", "btn_left", "btn_right", "btn_select", "btn_dial_press",
        "dial_clockwise", "dial_cw_position", "dial_counter", "dial_ccw_position",
        "heard_tone", "heard_tune", "heard_sample", "picture_right",
    };
    pip_gfx_info_t gi;
    unsigned i;

    if (auto_mode) {
        for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
            cur_call = i < 6 ? "wait_event" : i < 10 ? "read_dial" : i == 10 ? "tone" :
                       i == 11 ? "play_seq" : i == 12 ? "sample" : "present";
            skip(names[i]);
        }
        return;
    }
    if (pip_gfx_info(&gi) == 0) {
        fg = gi.fg_default;
    }
    dim = (fg >> 1) & 0x7F7F7Fu;
    cur_call = "wait_event";
    for (i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
        ask_button((int)i);
    }
    cur_call = "read_dial";
    ask_dial("dial_clockwise", "dial_cw_position", "Turn the dial clockwise", 1);
    ask_dial("dial_counter", "dial_ccw_position", "Turn the dial counter-clockwise", -1);

    cur_call = "tone";
    pip_tone(440, 3000);
    ask_yes_no("heard_tone", "A 440 Hz tone plays for 3 s.", "Did you hear it?");
    cur_call = "play_seq";
    pip_play_seq(tune, sizeof(tune) / sizeof(tune[0]));
    ask_yes_no("heard_tune", "A short rising tune plays twice.", "Did you hear it?");
    cur_call = "sample";
    for (i = 0; i < sizeof(pcm); i++) {
        /* A 250 Hz square wave for 1 s, then the same at 500 Hz below. */
        pcm[i] = (uint8_t)(((i / (i < 4000 ? 16u : 8u)) & 1) ? 176 : 80);
    }
    pip_sample(pcm, sizeof(pcm));
    ask_yes_no("heard_sample", "A low buzz, then a higher one, plays.", "Did you hear it?");
    cur_call = "present";
    ask_picture();
}
