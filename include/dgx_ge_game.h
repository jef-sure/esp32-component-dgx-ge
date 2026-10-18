#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * The constants of a game: numbers its author writes and the engine takes as
 * they are, so much and no more — how many kinds of things it opens, lives of
 * cells at once, records of the world, boxes and their places, shots,
 * timers; and numbers of its own (an hour to rescue the princess). The game
 * reads them and gives the engine's parts their sizes.
 *
 * The blob "GAME" (dgx_ge_pack.py, Pack.add_game): "DGGM", version u16,
 * count u16, then the values, i32 each, in the order the author wrote them;
 * the header names their places: <PREFIX>_GAME_<name>.
 */
#define DGX_GE_GAME_MAGIC   0x4D474744u /* "DGGM" */
#define DGX_GE_GAME_VERSION 1u

typedef struct {
    const uint8_t *values;
    uint16_t       count;
} dgx_ge_game_t;

/* Take the blob of the constants; false when it is not one (no constants: count 0). */
bool dgx_ge_game_open(dgx_ge_game_t *game, const void *data, size_t size);

/* A constant by its place; otherwise when the game wrote none there. */
int32_t dgx_ge_game_value(const dgx_ge_game_t *game, int index, int32_t otherwise);

#ifdef __cplusplus
}
#endif
