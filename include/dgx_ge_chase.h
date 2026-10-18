#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Chasing: an enemy that goes after the hero out of its scene. What a kind
 * does is data, its properties:
 * - its sense: how it knows of the hero — by sight (the game says if it sees
 *   him: a ray, a row), by hearing (so near, through walls), always, never;
 * - the sides of a scene it follows by (left, right, up, down; doors);
 * - how near a side it must be to follow by it (the game's units: points, a
 *   row; below 0: past it already);
 * - how far from home it goes (scenes of the graph; 0 no end);
 * - how long out of its senses it stays after him before it calms down (0
 *   never).
 * The game says whether it is ready by its own state (PoP: alive, its sword
 * drawn), moves the one that follows into the hero's scene (an actor all the
 * while: ~176 bytes) and gives the one that calms down back to the world (a
 * record where it stands).
 *
 * The blob "DGCH", little-endian: "DGCH", version u16, count u16; 18 bytes a
 * kind: kind u16 (an animation of the pack), sense u8, sides u8 (bit a
 * DGX_GE_SIDE_*, bit 4 doors), hearing i16, near i16 × 4 (by side), steps
 * u8, 0, calm u16 ms.
 */
#define DGX_GE_CHASE_MAGIC 0x48434744u /* "DGCH" */
#define DGX_GE_CHASE_DOORS 0x10

enum { DGX_GE_SENSE_NONE, DGX_GE_SENSE_SIGHT, DGX_GE_SENSE_HEARING, DGX_GE_SENSE_ALWAYS };

typedef struct {
    uint8_t  sense;   /* DGX_GE_SENSE_* */
    uint8_t  sides;   /* bit a side it follows by, DGX_GE_CHASE_DOORS by doors */
    int16_t  hearing; /* how near the hero it hears him */
    int16_t  near[4]; /* how near a side it must be to follow by it, by side */
    uint8_t  steps;   /* scenes from its home at most, 0 no end */
    uint16_t calm_ms; /* out of its senses this long, it calms down; 0 never */
} dgx_ge_chase_t;

/* What chasing a kind does, from the blob of the pack; false when the kind is not there (it never follows). */
bool dgx_ge_chase_of(const void *blob, size_t size, int kind, dgx_ge_chase_t *chase);

/* Does it sense the hero: so far from him, seeing him or not (the game's sight). */
bool dgx_ge_chase_senses(const dgx_ge_chase_t *chase, int to_hero, bool sees);

/*
 * Does it follow the hero out by a side (DGX_GE_SIDE_*, or 4: a door): ready
 * by the game's own state, so far from that side, from the hero, seeing him or
 * not, so many scenes from its home already.
 */
bool dgx_ge_chase_follows(const dgx_ge_chase_t *chase, bool ready, int side, int to_side, int to_hero, bool sees, int steps);

/* Out of its senses since a moment (0: it senses him): has it calmed down by now? */
bool dgx_ge_chase_calm(const dgx_ge_chase_t *chase, uint32_t lost_ms, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
