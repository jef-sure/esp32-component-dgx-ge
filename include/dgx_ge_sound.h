#pragma once
#include <stdint.h>
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
 *
 * A music (<PREFIX>_MUSIC_<name>, Pack.add_music) plays one at a time, apart
 * from the sounds: "music <name>", "music stop" and "music volume" of scripts
 * reach the game (DGX_GE_ANIM_MUSIC, DGX_GE_ANIM_MUSIC_LEVEL), which hands
 * them on here (dgx_ge_anim_said_sound does it all). Which one plays now
 * (music_now) is what the expression "music" of scripts reads
 * (dgx_ge_expr_sound), so a script waits for a music to end with no C. An
 * output without them (NULL) plays no music, and none plays.
 */
typedef struct {
    void (*play)(void *ctx, int sound); /* from its start (playing already: again) */
    void (*stop)(void *ctx, int sound); /* -1: all of them */
    void *ctx;
    void (*music)(void *ctx, int music, uint32_t fade_ms); /* from its start (playing already: it goes on); a number of none (-1, 255): stop */
    int (*music_now)(void *ctx);                           /* the music playing, -1 none */
    void (*music_level)(void *ctx, int percent, uint32_t ms); /* the music's level by design, 0..100, over a time */
} dgx_ge_sound_t;

static inline void dgx_ge_sound_play(const dgx_ge_sound_t *out, int sound)
{
    if (out && out->play && sound >= 0) out->play(out->ctx, sound);
}

static inline void dgx_ge_sound_stop(const dgx_ge_sound_t *out, int sound)
{
    if (out && out->stop) out->stop(out->ctx, sound);
}

/* A music played, or stopped by a number of none (-1; 255, as a script's music stop says it); at once, or by a fade of fade_ms. */
static inline void dgx_ge_sound_music(const dgx_ge_sound_t *out, int music, uint32_t fade_ms)
{
    if (out && out->music) out->music(out->ctx, music, fade_ms);
}

/* The music's level by design, 0..100 percent of its volume, reached over ms. */
static inline void dgx_ge_sound_music_level(const dgx_ge_sound_t *out, int percent, uint32_t ms)
{
    if (out && out->music_level) out->music_level(out->ctx, percent, ms);
}

/* The music playing, -1 none (no output, or one without music). */
static inline int dgx_ge_sound_music_now(const dgx_ge_sound_t *out)
{
    return out && out->music_now ? out->music_now(out->ctx) : -1;
}

#ifdef __cplusplus
}
#endif
