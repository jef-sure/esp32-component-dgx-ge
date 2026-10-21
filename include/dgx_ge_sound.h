#pragma once
#ifdef __cplusplus
extern "C" {
#endif

/*
 * The output of sounds: a sound is a number of the pack (<PREFIX>_SOUND_<name>,
 * Pack.add_sound), played or stopped by the output the game gives; with none,
 * nothing. The steps "sound N" of scripts reach the game (DGX_GE_ANIM_SOUND),
 * which hands them on here. Mostly the output is the engine's mixer
 * (dgx_ge_mixer_output), whose samples a board sends by I2S (boards/); a game
 * with hardware of its own for sound (a buzzer, a DAC) gives its own play and
 * stop.
 */
typedef struct {
    void (*play)(void *ctx, int sound); /* from its start (playing already: again) */
    void (*stop)(void *ctx, int sound); /* -1: all of them */
    void *ctx;
} dgx_ge_sound_t;

static inline void dgx_ge_sound_play(const dgx_ge_sound_t *out, int sound)
{
    if (out && out->play && sound >= 0) out->play(out->ctx, sound);
}

static inline void dgx_ge_sound_stop(const dgx_ge_sound_t *out, int sound)
{
    if (out && out->stop) out->stop(out->ctx, sound);
}

#ifdef __cplusplus
}
#endif
