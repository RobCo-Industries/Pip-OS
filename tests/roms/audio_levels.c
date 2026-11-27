/*
 * The sound calls of ABI 1.6 from a ROM: the kind of a play, the levels,
 * the master, a start over a sound of another gain and a holotape's view.
 * The card sets volume 80, radio 40, sfx 100, recordings 50, games 30 and
 * beep = no, and holds the test holotape as TAPE.HTP.
 * tests/test_audio_levels.py lists the check names in order and reads the
 * trace. Each tone has its own pitch, so the test can find its line.
 */
#include "rt.h"

#define WAIT_MAX_MS 3000u

#define CK(name, expr, expected) rt_check_eq(name, (int32_t)(expr), (int32_t)(expected))

static uint8_t buf[1600];

static void fill(uint32_t seed, uint32_t len)
{
    uint32_t i;

    for (i = 0; i < len; i++) {
        buf[i] = (uint8_t)(i * 7u + seed * 31u);
    }
}

static void quiet(const char *name)
{
    uint32_t t0 = pip_ticks_ms();

    while (pip_audio_status() == 1) {
        if (pip_ticks_ms() - t0 > WAIT_MAX_MS) {
            rt_check(name, 0);
            return;
        }
        pip_sleep_ms(5);
    }
    rt_check(name, 1);
}

int pip_main(const pipos_startup_t *startup)
{
    static const pip_note_t tune[2] = { { 300, 40 }, { 301, 40 } };

    (void)startup;

    /* A play takes the kind in force at its start, then keeps it. */
    CK("kind_start_sfx", pip_sound_kind(PIP_SND_RADIO), PIP_SND_SFX);
    CK("seq_radio", pip_play_seq(tune, 2), 0);
    CK("kind_back", pip_sound_kind(PIP_SND_SFX), PIP_SND_RADIO);
    quiet("seq_wait");
    fill(1, 800);
    CK("pcm_sfx", pip_sample(buf, 800), 0);
    quiet("pcm_wait");
    CK("kind_bad_4", pip_sound_kind(4), -PIP_EINVAL);
    CK("kind_bad_max", pip_sound_kind(0xFFFFFFFFu), -PIP_EINVAL);
    CK("kind_bad_keeps", pip_sound_kind(PIP_SND_SFX), PIP_SND_SFX);

    /* A queued sample joins the play and keeps its kind. */
    fill(2, 1600);
    CK("q_first", pip_sample_queue(buf, 1600), 0);
    CK("q_to_radio", pip_sound_kind(PIP_SND_RADIO), PIP_SND_SFX);
    fill(3, 800);
    CK("q_joins", pip_sample_queue(buf, 800), 0);
    quiet("q_wait");

    /* A level change while a radio tone plays, for its kind and another. */
    CK("lv_tone", pip_tone(500, 2000), 0);
    pip_sleep_ms(100); /* the driver starts a request at its next tick */
    CK("lv_radio_20", pip_sound_level(PIP_SND_RADIO, 20), 40);
    CK("lv_games_20", pip_sound_level(PIP_SND_GAMES, 20), 30);
    CK("lv_stop", pip_audio_stop(), 0);
    CK("lv_idle_50", pip_sound_level(PIP_SND_RADIO, 50), 20);
    CK("lv_next", pip_tone(501, 40), 0);
    quiet("lv_wait");
    CK("lv_radio_back", pip_sound_level(PIP_SND_RADIO, 40), 50);
    CK("lv_games_back", pip_sound_level(PIP_SND_GAMES, 30), 20);
    CK("lv_which_bad", pip_sound_level(5, 50), -PIP_EINVAL);
    CK("lv_level_bad", pip_sound_level(PIP_SND_RADIO, 101), -PIP_EINVAL);
    CK("lv_bad_keeps", pip_sound_level(PIP_SND_RADIO, PIP_LEVEL_ASK), 40);

    /* The master is the level of volume. */
    CK("master_ask", pip_sound_level(PIP_LEVEL_MASTER, PIP_LEVEL_ASK), 80);
    CK("master_ask_keeps", pip_sound_level(PIP_LEVEL_MASTER, PIP_LEVEL_ASK), 80);
    CK("master_60", pip_sound_level(PIP_LEVEL_MASTER, 60), 80);
    CK("master_volume", pip_volume(80), 60);

    /* A start over a sound of another gain stops that sound first. */
    CK("over_kind_sfx", pip_sound_kind(PIP_SND_SFX), PIP_SND_RADIO);
    CK("over_sfx", pip_tone(600, 2000), 0);
    pip_sleep_ms(100);
    CK("over_kind_radio", pip_sound_kind(PIP_SND_RADIO), PIP_SND_SFX);
    CK("over_radio", pip_tone(601, 40), 0);
    quiet("over_wait");

    /* A holotape, launched with radio in force. */
    CK("tape_levels", pip_holo_launch("TAPE.HTP", "levels"), 0);
    CK("tape_kind_back", pip_sound_kind(PIP_SND_SFX), PIP_SND_RADIO);
    CK("tape_master", pip_sound_level(PIP_LEVEL_MASTER, PIP_LEVEL_ASK), 80);
    CK("tape_trim_again", pip_holo_launch("TAPE.HTP", "trim"), 100);
    rt_checks_done();
    return 0;
}
