#pragma once

/*
 * Music by notes inside the mixer: its state, made by dgx_ge_mixer_init when the sounds have music, and the sample
 * the mixer adds of it each frame of its stream. Inside the engine only.
 */
#include <stdint.h>

#include "dgx_ge_mixer.h"

/* The state of the music of a mixer, for as many channels as the most a music of its sounds has (NULL: they have none); false when there is no room. */
bool dgx_ge_music_new(const dgx_ge_sounds_t *sounds, uint32_t rate, struct dgx_ge_music **music);

/* The next sample of the music (its volume in it), on from it by one; 0 when none plays. Under the mixer's lock. */
int32_t dgx_ge_music_next(struct dgx_ge_music *music);

/* Whether a blob of sounds holds together as music: its musics' entries and their songs' heads (dgx_ge_sounds_open). */
bool dgx_ge_music_check(const uint8_t *blob, size_t size, size_t at, uint16_t musics);
