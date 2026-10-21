#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dgx_ge_anim.h"
#include "dgx_ge_pack.h"
#include "dgx_ge_pockets.h"
#include "dgx_ge_scene.h"
#include "dgx_ge_tiles.h"

/*
 * A scene as the sprite editor laid it (its "Сцены"; the blob "DGSC", made by Pack.add_sprites as SCENE_<NAME>): its
 * layers, the pictures on them, the zones and the entries the game knows by name, the actors that begin there, its
 * grids of tiles. Data, read where it lies (flash); a scene of the editor is nothing special to the engine: the game
 * lays it into a scene of its own (dgx_ge_layout_scene), starts its actors (dgx_ge_layout_actor_start), takes its
 * zones and entries by their numbers (the header names them: <PREFIX>_SCENE_<SCENE>_ZONE_<NAME>, _ENTRY_, _ACTOR_,
 * _GRID_, _PICTURE_, _LAYER_) and sets up its grids (dgx_ge_layout_grid_set).
 *
 * Its links: the links of a map (dgx_ge_links.h) are one blob, <GRAPH>_<MAP>_LINKS, and name their cells by numbers
 * counted through the map: the cells of every grid of its scenes one after another (its scenes in the order of the
 * editor's file, a scene's grids in order, a grid row by row). A grid knows the number of its first cell (first), the
 * scene the first link of each of its sources (a cell, an actor, a zone): the game asks it as one is trodden on, pulled
 * or entered (dgx_ge_layout_links_of) and fires them (dgx_ge_links_fire). The cells of the map that change are the
 * game's to keep, a scene not shown included: their lives go on.
 *
 * The blob, little-endian: "DGSC", version u16, layers u8, grids u8, width, height i16, pictures, actors, zones,
 * entries, sources u16, 0 u16, where the grids begin u32; the layers (factor i16, order u8, flags u8); the pictures
 * (picture u16, x, y, key i16, layer u8, flags u8 — bit 0 mirrored, clip x, y, w, h i16 — w 0: none); the zones
 * (kind u16, x, y, w, h i16); the entries (x, y i16, flags u8 — bit 0 facing left, 0); the sources of links (kind u8
 * — DGX_GE_LAYOUT_CELL, _ACTOR, _ZONE, 0, which u16 — a cell's number in the map, an actor's or a zone's in the scene,
 * its first link u16); the actors by offsets u16 (kind — the blob of its animation u16, script u8, layer u8, x, y i16,
 * flags u8, params u8, each param: number u8, 0, value i16); the grids, each from a multiple of 4 (tiles — the blob of
 * its set u16, x, y i16, cols, rows u16, first — the number of its first cell in the map u16, flags u8 — bit 0
 * modifiers, bit 1 variants, slots u8, the layer of the scene of each slot u8 — 0xFF none, the cells u16 from an even
 * place, the modifiers u8, the variants u32 from a multiple of 4).
 */
#define DGX_GE_LAYOUT_MAGIC   0x43534744u /* "DGSC" */
#define DGX_GE_LAYOUT_VERSION 2u

/* What fires links in a scene: a cell (a plate trodden on), an actor (a lever pulled), a zone (one come into). */
enum { DGX_GE_LAYOUT_CELL, DGX_GE_LAYOUT_ACTOR, DGX_GE_LAYOUT_ZONE };

typedef struct {
    const uint8_t *data;
    uint32_t       size;
    uint8_t        layers, grids;
    int16_t        width, height;
    uint16_t       pictures, actors, zones, entries, sources;
    uint32_t       grids_at;
} dgx_ge_layout_t;

/* Take the blob of a scene laid in the editor; false when it is not one, or not whole. */
bool dgx_ge_layout_open(dgx_ge_layout_t *layout, const void *data, size_t size);

/*
 * The scene's layers (their factor and order) and its pictures, into a scene of the game, between its
 * dgx_ge_scene_set_begin and _end: each picture a piece of its set, its dgx_image_t kept in images (layout->pictures
 * of them, the game's: the scene points at them), maps: by set of the pack the indices of the frame for its colors
 * (NULL: as they are). false: the scene has fewer layers, a picture is not in the pack, its set is full.
 */
bool dgx_ge_layout_scene(const dgx_ge_layout_t *layout, dgx_ge_scene_t *scene, const dgx_ge_pack_t *pack, const uint8_t *const *maps,
                         dgx_image_t *images);

/* Zone n: its place in the scene and its kind (a word of the game: <PREFIX>_ZONE_<WORD>); false: no such zone. */
bool dgx_ge_layout_zone(const dgx_ge_layout_t *layout, int n, dgx_ge_rect_t *place, int *kind);

/* Entry n: where one who comes stands, and whether facing left; false: no such entry. */
bool dgx_ge_layout_entry(const dgx_ge_layout_t *layout, int n, int *x, int *y, bool *left);

/* An actor as laid: its kind (the blob of its animation), the script it begins with, its layer, place, facing; the params given. */
typedef struct {
    int            kind, script, layer;
    int16_t        x, y;
    bool           flip;
    int            params;
    const uint8_t *given; /* params of them: number u8, 0, value i16 */
} dgx_ge_layout_actor_t;

/* Actor n as laid; false: no such actor. */
bool dgx_ge_layout_actor(const dgx_ge_layout_t *layout, int n, dgx_ge_layout_actor_t *actor);

/* Actor n started as laid: its kind's animation (of kinds), its script, place and facing, the params given over its own. false: none, or no such kind. */
bool dgx_ge_layout_actor_start(const dgx_ge_layout_t *layout, int n, dgx_ge_actor_t *actor, dgx_ge_kinds_t *kinds, uint32_t now_ms);

/* A grid as laid: its set of tiles (a blob), place, size, the layer of each slot of the set, its cells, modifiers and variants in the blob. */
typedef struct {
    int             tiles;
    int16_t         x, y;
    uint16_t        cols, rows;
    uint16_t        first; /* the number of its first cell in the map (links and lives name cells so): row r, col c is first + r * cols + c */
    int             slots;
    const uint8_t  *layer_of; /* the scene's layer of each slot of the set, 0xFF: not in the scene */
    const uint16_t *cells;    /* rows x cols, in the blob: read only (little-endian, as the chips are); copy them to change them */
    const uint8_t  *mods;     /* NULL: all 0 */
    const uint32_t *variants; /* NULL: all 0 */
} dgx_ge_layout_grid_t;

/* Grid n as laid; false: no such grid. */
bool dgx_ge_layout_grid(const dgx_ge_layout_t *layout, int n, dgx_ge_layout_grid_t *grid);

/* The cell numbered so in the map, if it is one of this scene's: its grid, column and row; false: it is not. */
bool dgx_ge_layout_cell_at(const dgx_ge_layout_t *layout, int number, int *grid, int *col, int *row);

/*
 * The first link of a source of this scene in its map's links (to fire them: dgx_ge_links_fire): kind
 * DGX_GE_LAYOUT_CELL, which the cell's number in the map; _ACTOR or _ZONE, which its number in the scene. -1: it fires
 * nothing.
 */
int dgx_ge_layout_links_of(const dgx_ge_layout_t *layout, int kind, int which);

/*
 * A grid of the game set up as laid: its set of tiles (opened by the game from the blob grid->tiles), cells (the
 * game's: a copy of grid->cells it may change, or those very ones cast, if it never does), place, size, the layers
 * of its slots, modifiers and variants; the rest of it left as the game has it.
 */
void dgx_ge_layout_grid_set(const dgx_ge_layout_grid_t *grid, dgx_ge_grid_t *g, const dgx_ge_tileset_t *tiles, uint16_t *cells);

#ifdef __cplusplus
}
#endif
