/* Sounds by number: to the output the platform gives; with none, nothing (no crash). */
#include "check.h"
#include "dgx_ge_sound.h"

static int played[4], stopped;

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

int main(void)
{
    dgx_ge_sound_play(NULL, 1);               /* no output: nothing */
    dgx_ge_sound_play(&(dgx_ge_sound_t){0}, 1); /* an output that plays nothing: nothing */
    const dgx_ge_sound_t out = {play, stop, NULL};
    dgx_ge_sound_play(&out, 1);
    dgx_ge_sound_play(&out, 1);
    dgx_ge_sound_play(&out, -1); /* no such sound: nothing */
    dgx_ge_sound_stop(&out, -1);
    CHECK(played[1] == 2 && played[0] == 0 && stopped == -1);
    CHECK_DONE();
}
