#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The synthesizers of music by notes, and a song played alone.
 *
 * A song (DGMU, made by the packer from MML: tools/dgx_ge_music.py) says which synthesizer plays it:
 *   chip  a square of 12.5, 25 or 50%, a triangle, noise; each with an envelope (attack, decay, sustain, release);
 *   nes   as the NES's 2A03: a pulse of 12.5, 25, 50 or 75%, the triangle as its staircase of 32 steps, noise of its
 *         period table in its two modes (long, short); the envelope as the chip's, at the chip's 16 levels of volume;
 *   fm    two operators as the OPL2's (AdLib): a multiplier, a level, a key scale level, an envelope, one of four
 *         waves (sine, half-sine, abs-sine, quarter-sine); the modulator's feedback, a connection (FM or additive).
 * All in whole numbers, sample by sample: the same song at the same rate is the same samples everywhere.
 *
 * Which of them a build carries is the game's choice (dgx_ge_synths(chip fm) of project_include.cmake, or
 * DGX_GE_SYNTH_<NAME>=1 among the definitions of a build for the computer): DGX_GE_SYNTH_CHIP, _NES, _FM, 1 or 0. None
 * said: FM alone (the default synthesizer of a song too). A song of a synthesizer the build left out plays nothing (as if it had ended at once), and
 * the mixer says so once in the log.
 */
#if !defined(DGX_GE_SYNTH_CHIP) && !defined(DGX_GE_SYNTH_NES) && !defined(DGX_GE_SYNTH_FM)
#define DGX_GE_SYNTH_FM 1
#endif
#ifndef DGX_GE_SYNTH_CHIP
#define DGX_GE_SYNTH_CHIP 0
#endif
#ifndef DGX_GE_SYNTH_NES
#define DGX_GE_SYNTH_NES 0
#endif
#ifndef DGX_GE_SYNTH_FM
#define DGX_GE_SYNTH_FM 0
#endif

/* The synthesizers by their numbers in a song (its byte of the synthesizer). */
enum { DGX_GE_SYNTH_IS_CHIP = 0, DGX_GE_SYNTH_IS_NES = 1, DGX_GE_SYNTH_IS_FM = 2 };

/* Whether this build plays songs of a synthesizer (DGX_GE_SYNTH_IS_*). */
bool dgx_ge_synth_built(int synth);

/*
 * A song played alone: no mixer, no pack, no board. The samples are those the mixer adds of the same song at the same
 * rate, mono, signed 16 bits, at the full volume of its bus. For tools and for the editor (WebAssembly:
 * tools/synth/Makefile builds this and dgx_ge_music.c, nothing else).
 */
typedef struct dgx_ge_music dgx_ge_song_player_t;

/* What is wrong with a song: 0 nothing; 1 it is no song (or does not hold together); 2 its synthesizer is not in this build. */
int dgx_ge_song_check(const uint8_t *song, size_t size);

/* A player of a song (read where it lies, not copied: it stays while the player plays) at a rate; NULL when the song is wrong (dgx_ge_song_check) or there is no room. */
dgx_ge_song_player_t *dgx_ge_song_player_new(const uint8_t *song, size_t size, uint32_t rate);

/* The next frames of the song into out; how many of them it gave before it ended (the rest of out is 0). A song that goes round never ends. */
size_t dgx_ge_song_render(dgx_ge_song_player_t *player, int16_t *out, size_t frames);

/* Its state given back. */
void dgx_ge_song_player_free(dgx_ge_song_player_t *player);

/* A song from its start into out at once (a player made, rendered, given back): the frames it gave, 0 when it is wrong. */
size_t dgx_ge_music_render(const uint8_t *song, size_t size, uint32_t rate, int16_t *out, size_t frames);

#ifdef __cplusplus
}
#endif
