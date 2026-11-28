/*
 * sound_store from a ROM: it sets the master and the four levels, then
 * stores them twice. tests/test_audio_levels.py reads PIPOS.SET from the
 * card image after the run and boots the image again.
 */
#include "rt.h"

int pip_main(const pipos_startup_t *startup)
{
    (void)startup;
    rt_check_eq("store_master", pip_sound_level(PIP_LEVEL_MASTER, 70), 80);
    rt_check_eq("store_radio", pip_sound_level(PIP_SND_RADIO, 45), 40);
    rt_check_eq("store_sfx", pip_sound_level(PIP_SND_SFX, 90), 100);
    rt_check_eq("store_recordings", pip_sound_level(PIP_SND_RECORDINGS, 60), 50);
    rt_check_eq("store_games", pip_sound_level(PIP_SND_GAMES, 55), 30);
    rt_check_eq("store_ok", pip_sound_store(), 0);
    rt_check_eq("store_again", pip_sound_store(), 0);
    rt_check_eq("store_keeps", pip_sound_level(PIP_SND_SFX, PIP_LEVEL_ASK), 90);
    return rt_checks_done();
}
