/* Sounds by number: to the output the platform gives; with none, nothing (no crash); its music, which plays, its level. */
#include "check.h"
#include "dgx_ge_sound.h"

static int      played[4], stopped, music = -1, level = -1;
static uint32_t fade, level_ms;

static void play(void *ctx, int sound)
{
    (void)ctx;
    if (sound < 4) ++played[sound];
}

static void stop(void *ctx, int sound)
{
    (void)ctx;
    stopped = sound;
}

static void play_music(void *ctx, int m, uint32_t fade_ms)
{
    (void)ctx;
    music = m, fade = fade_ms;
}

static int music_now(void *ctx)
{
    (void)ctx;
    return music;
}

static void music_level(void *ctx, int percent, uint32_t ms)
{
    (void)ctx;
    level = percent, level_ms = ms;
}

int main(void)
{
    dgx_ge_sound_play(NULL, 1);               /* no output: nothing */
    dgx_ge_sound_play(&(dgx_ge_sound_t){0}, 1); /* an output that plays nothing: nothing */
    const dgx_ge_sound_t out = {.play = play, .stop = stop};
    dgx_ge_sound_play(&out, 1);
    dgx_ge_sound_play(&out, 1);
    dgx_ge_sound_play(&out, -1); /* no such sound: nothing */
    dgx_ge_sound_stop(&out, -1);
    CHECK(played[1] == 2 && played[0] == 0 && stopped == -1);
    /* an output without music: none plays, asking for one does nothing */
    dgx_ge_sound_music(&out, 2, 0), dgx_ge_sound_music_level(&out, 50, 0), dgx_ge_sound_music(NULL, 2, 0);
    CHECK(dgx_ge_sound_music_now(&out) == -1 && dgx_ge_sound_music_now(NULL) == -1 && music == -1 && level == -1);
    /* one with music: the music and its fade, which plays, the level and its time */
    const dgx_ge_sound_t full = {play, stop, NULL, play_music, music_now, music_level};
    dgx_ge_sound_music(&full, 2, 500);
    dgx_ge_sound_music_level(&full, 30, 1000);
    CHECK(dgx_ge_sound_music_now(&full) == 2 && fade == 500 && level == 30 && level_ms == 1000);
    CHECK_DONE();
}
