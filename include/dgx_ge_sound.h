#pragma once
#ifdef __cplusplus
extern "C" {
#endif

/*
 * Sounds, for now only their names: a sound is a number of the pack
 * (<PREFIX>_SOUND_<name>, Pack.add_sound), played or stopped by the output
 * the platform gives (a DAC, I2S, a pin of PWM: a board's own, as its
 * display is); with none, nothing. The steps "sound N" of scripts reach the
 * game (DGX_GE_ANIM_SOUND), which hands them on here. The sounds themselves,
 * their data in the pack and a mixer of the engine, come later; the output
 * will take samples then.
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
