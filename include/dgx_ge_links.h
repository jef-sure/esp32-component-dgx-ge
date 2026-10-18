#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dgx_ge_cells.h"

/*
 * Links: who sets what going in a map. A source (a plate trodden on, a lever
 * pulled, a lock opened by its code, a step of a script or a dialog) fires
 * its links: each tells the life of its cell an event (dgx_ge_cells.h), the
 * link's own, or the one the source fires (an opener: open; a closer: shut),
 * an own param of the life given the link's value first (how long a gate
 * stays up by this plate). What follows is the target's: its matrix.
 *
 * The links of a map are data (the blob "DGLK"), in flash: "DGLK", version
 * u16, count u16, then the links, 6 bytes each: the cell u16 (the game's
 * number), the event u8 (0xFF: the one fired), flags u8 (bit 0: the last of
 * its source), the value i16 (into own param 0 of the target's life;
 * -32768: none). A source names its first link (a plate: by its modifier);
 * its links go on to the last.
 */
#define DGX_GE_LINKS_MAGIC   0x4B4C4744u /* "DGLK" */
#define DGX_GE_LINKS_VERSION 1u
#define DGX_GE_LINK_ANY      0xFF   /* the event the source fires */
#define DGX_GE_LINK_NO_VALUE INT16_MIN

typedef struct {
    const uint8_t *links;
    uint16_t       count;
} dgx_ge_links_t;

/* Take the blob of links; false when it is not one (no links at all: count 0). */
bool dgx_ge_links_open(dgx_ge_links_t *links, const void *data, size_t size);

/* The life a cell has (its kind: the animation its tile names), as the game finds it. */
typedef uint16_t (*dgx_ge_life_of_fn)(uint16_t cell, void *ctx);

/*
 * A source fires its links from the first, an event (for those that take the
 * source's), at a moment: the lives of their cells told. How many were told.
 */
int dgx_ge_links_fire(const dgx_ge_links_t *links, int first, int event, dgx_ge_cells_t *cells, dgx_ge_life_of_fn life_of, void *ctx, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
