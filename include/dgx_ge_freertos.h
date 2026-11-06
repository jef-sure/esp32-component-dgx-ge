#pragma once
#ifdef __cplusplus
extern "C" {
#endif

/*
 * What a game does alike on FreeRTOS, whatever its board: the end of a pass of its loop, and its sound filled by a task
 * of the board while the game plays (on the computer, the linux target, a task too). The board itself is the hardware:
 * jef-sure/boards (boards.h), or the game's own.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dgx_ge_mixer.h"
#include "dgx_ge_sound.h"

/*
 * The end of a pass of the game's loop that began at t0_us (esp_timer_get_time): the rest of period_us waited, and the
 * processor given up for a tick at least however long the pass took, so that the board's other tasks run (and its
 * watchdog is fed) and, on the computer, the window is shown and the input read (DGX's window: in vTaskDelay). A pass
 * that took longer goes on after that one tick: the game moves by time, a step late is not made up.
 */
void dgx_ge_pace(int64_t t0_us, uint32_t period_us);

/* Where a board's task takes its next samples: count of them (16 bits, mono) into samples; ctx as the task was given it. */
typedef void (*dgx_ge_sound_source_t)(void *ctx, int16_t *samples, size_t count);

/*
 * What starts the board's task over a source, as jef-sure/boards' board_sound_start is: false when the board has no
 * sound or it could not start; its rate into rate_out, when that is not NULL (the engine gives NULL: it was told the rate).
 */
typedef bool (*dgx_ge_sound_start_t)(dgx_ge_sound_source_t source, void *ctx, uint32_t *rate_out);

/*
 * The game's sound, as a game starts it: a mixer of the blob SOUNDS (blob, size: the pack's, or samples in the board's
 * flash) opened into sounds, of voices voices (a constant of the game, as it names one), at the board's rate, given a
 * lock of FreeRTOS (the mixer's lock: the game calls the mixer, and its output, as it likes) and started as the source
 * of the board's task (start), at volume (of the mixer, 1..255, with the game's sound on: the board's speaker, a room;
 * jef-sure/boards gives the level in percent of full scale, mixer_volume, 0 when it knows none); its output, what the
 * game plays through (dgx_ge_sound_play). It goes on until the firmware stops: the mixer lives as long. None, play
 * NULL, so that nothing sounds and nothing is kept, on a board without sound (a rate of 0, or start false), with no
 * blob or one that is not SOUNDS, or no room for the voices. One a game: the lock is one. On jef-sure/boards:
 *     board_sound_t speaker;
 *     if (board_sound_init(&speaker))
 *         sound = dgx_ge_sound_open(&mixer, &sounds, blob, size, voices, speaker.rate,
 *                                   speaker.mixer_volume ? speaker.mixer_volume * 255 / 100 : 32, board_sound_start);
 */
dgx_ge_sound_t dgx_ge_sound_open(dgx_ge_mixer_t *mixer, dgx_ge_sounds_t *sounds, const void *blob, size_t size, int voices, uint32_t rate,
                                 uint8_t volume, dgx_ge_sound_start_t start);

#ifdef __cplusplus
}
#endif
