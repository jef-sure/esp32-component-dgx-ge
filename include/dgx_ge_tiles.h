#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dgx_ge_anim.h"
#include "dgx_ge_mask.h"
#include "dgx_ge_pack.h"
#include "dgx_ge_scene.h"

/*
 * Tiles: a scene described by a grid of cells. A cell holds the number of a
 * tile of a set of tiles (0 empty); the tile says how it is drawn and what is
 * solid in it. The game keeps the cells and changes them; the grid lays their
 * pieces into the set of a scene and tells where a change must be drawn anew.
 *
 * A tile is drawn in pieces: a picture of the pack by its origin at the
 * bottom-left point of the cell moved by dx, dy, in a slot (a layer: the
 * scene says which of its own) and with a key. So a cell is drawn partly
 * behind the people, partly before them, and may reach into its neighbours.
 *
 * What is solid in it stops: its bodies, boxes in the cell (from its
 * bottom-left point: dx, dy, w, h, numbers or expressions of mod and the
 * fields, and when), as a floor is a slab somewhere in its cell, a gate a
 * strip let down as far as its modifier says; without bodies, its solid
 * sides (top, left, right, bottom): a floor solid at its top, a wall all
 * round, a band of edge points along each. They give the terrain the engine
 * finds contacts in (dgx_ge_anim.h). The sides it closes end the open space an
 * actor is seen in (dgx_ge_grid_open_space): as solid unless the set says
 * otherwise, for what one goes through but does not see through, as a door
 * frame. Its opening, a box as a body is: what an actor that has gone into
 * it (up the stairs behind a door) is seen through.
 *
 * A piece may follow rules, so a tile can say by data what most games need:
 * - around: it is drawn only where the neighbours of the cell are so (empty,
 *   such tiles, solid on a side, or not): grass where above is empty, the
 *   side of a floor where no floor on the right hides it;
 * - by_mod: its picture is chosen by the modifier of the cell, a number the
 *   game keeps there (a door shut, half open, open); its dx, dy may be
 *   expressions of mod (a gate raised by mod / 4);
 * - cycle: its pictures go round by the time of the scene alone, every cell
 *   in step (water, lava); the game does nothing;
 * - when: an expression (of mod, variant and their fields): drawn only
 *   where it is not 0: "mod > 0 and variant == 0";
 * - variant: the decoration of the cell, a number of 32 bits made up on the
 *   host (patterns of bricks), stored with the scene. A piece may be drawn
 *   only in cells of one variant; or read fields of it, named by the set of
 *   tiles (bits of the variant or of the modifier), in its expressions:
 *   pick, its picture by an expression; dx, dy; the color of a card, a
 *   silhouette or a fill.
 *
 * A cell is laid in steps. A piece has its step, and may be laid with
 * another cell than its own: the right edge of a floor, laid when the grid
 * lays the cell on its right, between the pieces of that one, where it must
 * hide some of them and be hidden by others. Its place and rules are still
 * those of its own cell. A piece may be drawn as a card (on a rectangle of
 * its color, hiding what is behind it), as a silhouette of one color, or be
 * a fill: a rectangle of a color, no picture.
 * What data cannot say, the game draws itself: a hook of the grid, called at
 * every step of every cell.
 *
 * The set of tiles is a blob of the pack ("DGTS"), made by
 * dgx_ge_pack.py (Pack.add_tileset, or from the scenes of the sprite editor):
 *
 *   header    28 bytes: "DGTS", version, cell width, height, and the counts of tiles, slots,
 *             pieces, conditions, pictures, listed tiles, bytes of expressions, distinct pictures, bodies
 *   tiles     12 bytes each: kind (the game's number), solid sides, pieces, the first of them, the sides it
 *             closes (and 16: the record after its bodies is its opening, not counted among them),
 *             bodies, the first of them, its life (an animation of the pack: what a cell of it does by
 *             itself, dgx_ge_cells.h; 0xFFFF none)
 *   laid      36 bytes a tile: for its own cell and each side it is laid with, a bit for each
 *             step (0..31) it has pieces at; the grid looks through no others
 *   pieces    32 bytes each: its pictures (first, how many, picked or cycle), dx, dy and their
 *             expressions, slot, variant, key, its conditions (first, how many), the cell it is
 *             laid with, its step, how it is drawn (normal, card, silhouette, fill) and its color
 *             (set, n), the expressions of its pick and of its color, the size of a fill, the
 *             expression of when it is drawn
 *   distinct pictures 2 bytes each: a picture of the pack, each once
 *   pictures  4 bytes each: which of the distinct ones (0xFFFF none), ms (of a cycle)
 *   conditions 6 bytes each: side, what (empty, tiles, solid sides; not), value, how many
 *   tiles listed by conditions, 2 bytes each; expressions (dgx_ge_anim.py)
 *   bodies    20 bytes each: dx, dy, w, h (from the bottom-left point of the cell; dy up is
 *             negative, the body goes up from it and right), their expressions, when, for (the
 *             masks it holds, a bit a mask number; 0: every one); an opening is one of them (for 0)
 */
#define DGX_GE_TILES_MAGIC   0x53544744u /* "DGTS" */
#define DGX_GE_TILES_VERSION 9u

enum {
    DGX_GE_SOLID_TOP    = 1,
    DGX_GE_SOLID_LEFT   = 2,
    DGX_GE_SOLID_RIGHT  = 4,
    DGX_GE_SOLID_BOTTOM = 8,
    DGX_GE_SOLID_ALL    = 15,
};

/* Besides the sides of dgx_ge_grid_open_space: the actor has gone into an opening (DGX_GE_ITEM_INSIDE). */
#define DGX_GE_SPACE_INSIDE 16u

typedef struct {
    const dgx_ge_pack_t *pack;
    uint16_t             cell_w, cell_h, tiles, slots, pieces, conds, pictures, listed, expr_bytes, distinct, bodies;
    const uint8_t       *table, *laid_table, *piece_table, *image_table, *picture_table, *cond_table, *list_table, *exprs, *body_table;
    dgx_image_t         *images; /* the distinct pictures, taken out of the pack when opened: the RAM of the set, a dgx_image_t each */
    /* How far a change of the tile of a cell reaches, cells across and down: the sides its pieces are laid with and the rules of its neighbours (dgx_ge_grid_relay); found when opened. */
    uint8_t reach_x, reach_y;
} dgx_ge_tileset_t;

/* Take a set of tiles from its blob, its pictures from the pack; false when it is not one or a picture is missing. */
bool dgx_ge_tileset_open(dgx_ge_tileset_t *set, const dgx_ge_pack_t *pack, const void *data, size_t size);
void dgx_ge_tileset_close(dgx_ge_tileset_t *set);

/* A tile: the game's number for it and its solid sides; false when there is no such tile (0 is none). */
bool dgx_ge_tile_info(const dgx_ge_tileset_t *set, int tile, uint16_t *kind, uint8_t *solid);

/* How much a tile veils a look through it (sense ... seen): 0 not at all, 1..6 a point through it costs 2..7, 7 nothing seen through; 0 for none. */
uint8_t dgx_ge_tile_veil(const dgx_ge_tileset_t *set, int tile);

/* The sides a tile closes for sight (DGX_GE_SOLID_* bits); 0 for none or no such tile. */
uint8_t dgx_ge_tile_closes(const dgx_ge_tileset_t *set, int tile);

/* The life of a tile: an animation of the pack (dgx_ge_cells.h), 0xFFFF none. */
uint16_t dgx_ge_tile_life(const dgx_ge_tileset_t *set, int tile);

/* Whether the tiles draw with a set of the pack: pictures of it, or its colors for cards and
 * silhouettes. The grid needs the map of colors (maps) of every such set. */
bool dgx_ge_tileset_uses_set(const dgx_ge_tileset_t *set, int pack_set);

typedef struct dgx_ge_grid dgx_ge_grid_t;

struct dgx_ge_grid {
    const dgx_ge_tileset_t *tileset; /* NULL: every cell is drawn by the game */
    const uint8_t *const   *maps;  /* by set of the pack: the indices of the frame for its colors (dgx_ge_frame_add_colors) */
    uint16_t               *cells; /* rows x cols, the game's: 0 empty, n the tile n */
    uint16_t                cols, rows;
    int16_t                 x, y;  /* the top-left of the grid, points of the scene */
    /*
     * A ring: the grid is a window of cols x rows cells onto a world without
     * end. Its top-left is the cell first_col, first_row of the world: numbers
     * that go round past 2^32, so they never run out and only their
     * differences mean anything (a world made by them repeats after 2^32
     * cells). In the arrays the top-left lies at ring_col, ring_row and the
     * window goes round them, so moving it by a row (dgx_ge_grid_scroll)
     * leaves every cell where it is and frees the row that went out for the
     * one that comes in. The game writes that one (dgx_ge_grid_index) and lays
     * the set anew. Cells are still given to every call by their place in the
     * window, 0.. cols-1.
     */
    bool                    ring;
    uint32_t                first_col, first_row;
    uint16_t                ring_col, ring_row;
    uint8_t                 layer_of[DGX_GE_SCENE_MAX_LAYERS]; /* the layer of the scene of each slot */
    uint8_t                 edge;  /* how thick a solid side is, points: a floor is a slab of it at the top of its cell */
    bool                    bottom_up; /* rows laid from the bottom up: pieces of a row above overlap those below */
    uint8_t                 steps;     /* the steps of laying a cell, 0 one */
    uint32_t                skip;      /* steps not laid, a bit each (the hook is not called at them either): a strip of a neighbour shown in part */
    const uint8_t          *mods;     /* rows x cols, the modifiers of the cells, the game's; NULL: all 0 */
    const uint32_t         *variants; /* rows x cols, the variants of decoration of the cells, made on the host; NULL: all 0 */
    uint32_t                now_ms;   /* the time of the scene: a cycle shows its picture of this moment */
    /*
     * Where the pieces of each cell begin in the set of the scene, in the order they are laid (row by row, from the
     * top or the bottom), and one more: where the last ends; cols x rows + 1 numbers, the game's, written by
     * dgx_ge_grid_add. With them dgx_ge_grid_relay lays again only what a change reaches. NULL: not kept.
     */
    uint16_t               *laid;
    /* The tile beyond the grid, its modifier and variant (0 both when not set), for the neighbours of its edge cells (rooms around); NULL: empty there. */
    uint16_t (*outside)(const dgx_ge_grid_t *g, int col, int row, uint8_t *mod, uint32_t *variant, void *ctx);
    /*
     * What the game draws itself, for what its tiles cannot say by data: at
     * every step of every cell, the empty ones too, before the pieces of data
     * of that step; it adds its pieces to the scene. NULL: none.
     */
    void (*draw)(const dgx_ge_grid_t *g, int col, int row, int step, dgx_ge_scene_t *scene, void *ctx);
    void *ctx;
};

/* Where the cell col, row of the window lies in the arrays of the grid (cells, mods, variants); -1 outside the window. */
static inline int dgx_ge_grid_index(const dgx_ge_grid_t *g, int col, int row)
{
    if (col < 0 || row < 0 || col >= g->cols || row >= g->rows) return -1;
    if (!g->ring) return row * g->cols + col;
    return (g->ring_row + row) % g->rows * g->cols + (g->ring_col + col) % g->cols;
}

static inline uint16_t dgx_ge_grid_cell(const dgx_ge_grid_t *g, int col, int row)
{
    const int i = dgx_ge_grid_index(g, col, row);
    return g->cells && i >= 0 ? g->cells[i] : 0;
}

/*
 * Move the window of a ring by cols, rows cells of the world (forward: more);
 * x, y follow, so every cell stays at its place in the scene. The cells that
 * came into view keep what went out of it until the game writes them: after
 * a scroll by +1 row, row rows - 1 of the window is new. Then the set is laid
 * anew (dgx_ge_scene_set_begin, dgx_ge_grid_add, dgx_ge_scene_set_end).
 */
static inline void dgx_ge_grid_scroll(dgx_ge_grid_t *g, int cols, int rows)
{
    g->first_col += (uint32_t)cols, g->first_row += (uint32_t)rows;
    if (g->cols) g->ring_col = (uint16_t)((g->ring_col + cols % g->cols + g->cols) % g->cols);
    if (g->rows) g->ring_row = (uint16_t)((g->ring_row + rows % g->rows + g->rows) % g->rows);
    g->x = (int16_t)(g->x + cols * (g->tileset ? g->tileset->cell_w : 0));
    g->y = (int16_t)(g->y + rows * (g->tileset ? g->tileset->cell_h : 0));
}

/*
 * Add the pieces of every cell to the set of the scene (a scene reset, or
 * between dgx_ge_scene_set_begin/end), row by row, from the left: from the top
 * down or from the bottom up. False when it had no room.
 */
bool dgx_ge_grid_add(const dgx_ge_grid_t *g, dgx_ge_scene_t *scene);

/*
 * A cell has changed (the game has written it): lay again the cells it reaches (of its modifier or variant alone,
 * kind false: its own pieces, where its tile lays them; of its tile, kind true: the rules of its neighbours too, as far
 * as dgx_ge_tileset_t says),
 * the set given by dgx_ge_grid_add with laid kept. col, row may lie beyond the grid (a cell of the room around, as
 * outside gives it): what of the grid it reaches is laid. Each cell is laid at the end of the set, beyond what is
 * there, and compared with what it was: a piece shown otherwise changes in its place (dgx_ge_scene_change marks where
 * it was and where it is), so the order of drawing is that of the whole laid anew, and only that is drawn. False
 * when a cell lays more or fewer pieces than before (a rule now holds, a tile of other pieces), or the set has no
 * room beyond it, or nothing was kept: its places are marked all the same (all the frame when it had no room), and
 * the game lays the set anew (dgx_ge_scene_set_begin, dgx_ge_grid_add of each grid, dgx_ge_scene_set_end): the
 * numbers of the pieces of the grids after it would move. The hook (draw) is called again for the cells laid; what
 * it draws from cells farther away it does not see changed.
 */
bool dgx_ge_grid_relay(const dgx_ge_grid_t *g, dgx_ge_scene_t *scene, int col, int row, bool kind);

/*
 * Lay one step of a cell again, its pieces of data only: for a hook that
 * draws over some of them and must draw them once more above what it drew.
 * False when the scene had no room.
 */
bool dgx_ge_grid_lay(const dgx_ge_grid_t *g, int col, int row, int step, dgx_ge_scene_t *scene);

/*
 * A figure: a tile of the grid's set drawn for an actor, not in a cell. A
 * thing that looks like several pieces in their layers (a pot before the
 * people, its shadow behind them) is drawn by the same data as a tile, by
 * the rules of its pieces: mod and variant are the actor's (say, its
 * params), neighbours there are none, a cycle goes by the grid's time. Its
 * pieces go into the scene as actors, step by step, all of them whatever
 * side they are laid with, from x, y, the bottom-left point a cell would
 * have. False when the scene had no room.
 */
bool dgx_ge_grid_figure(const dgx_ge_grid_t *g, int tile, uint8_t mod, uint32_t variant, int x, int y, dgx_ge_scene_t *scene);

/*
 * Where the pieces of a tile take place when it is in a cell, as the cell is
 * now (its modifier, its neighbours, the time), with whichever cell they are
 * laid.
 */
dgx_ge_rect_t dgx_ge_grid_cell_box(const dgx_ge_grid_t *g, int col, int row, int tile);

/*
 * What a change of a cell may draw anew: its pieces and those of its
 * neighbours, whose rules may look at it. Taken before and after the change,
 * both are marked.
 */
dgx_ge_rect_t dgx_ge_grid_change_box(const dgx_ge_grid_t *g, int col, int row);

/*
 * Does a cell look the same in two states (tile, modifier)? Its own pieces in
 * both: shown or not, the same pictures at the same places, drawn alike. A
 * change that looks the same need not be drawn anew (spikes out counting
 * down, a gate up and higher still).
 */
bool dgx_ge_grid_same_look(const dgx_ge_grid_t *g, int col, int row, int tile_a, uint8_t mod_a, int tile_b, uint8_t mod_b);

static inline uint8_t dgx_ge_grid_mod(const dgx_ge_grid_t *g, int col, int row)
{
    const int i = dgx_ge_grid_index(g, col, row);
    return g->mods && i >= 0 ? g->mods[i] : 0;
}

/*
 * The time of the scene moves to now: the places of the cells whose cycles
 * show another picture now, up to max of them; the game lays the set anew and
 * marks them. Returns how many there are (more than max: mark them all).
 */
int dgx_ge_grid_animate(dgx_ge_grid_t *g, uint32_t now_ms, dgx_ge_rect_t *places, int max);

/* The cell a point of the scene is in; false outside the grid. */
bool dgx_ge_grid_cell_at(const dgx_ge_grid_t *g, int x, int y, int *col, int *row);

/* Does a cell count (its tile, its modifier, where it is in the window)? */
typedef bool (*dgx_ge_cell_fn)(int tile, uint8_t mod, int col, int row, void *ctx);

/*
 * The cell nearest to x, y (points of the scene) by its middle, within
 * radius points, of those fn lets (NULL: any not empty): grass to eat, water
 * to drink. False when there is none; col, row in the window, dx, dy from x,
 * y to its middle. Of equally near ones the upper, then the left. Only the
 * cells of the square around the radius are looked at.
 */
bool dgx_ge_grid_nearest(const dgx_ge_grid_t *g, int x, int y, int radius, dgx_ge_cell_fn fn, void *ctx, int *col, int *row, int *dx, int *dy);

/* A sense of cells answered by a grid: the nearest cell not empty that makes where not 0 (its.tile, its.mod, its.cell); its number row * cols + col. */
bool dgx_ge_sense_cells(const dgx_ge_grid_t *g, const dgx_ge_actor_t *actor, const dgx_ge_sense_t *query, dgx_ge_sensed_t *found);

/*
 * What a look from x0, y0 to x1, y1 (points of the scene) costs through the cells it crosses, a point at a time: a
 * point in a cell of a veil v costs 1 + v (0 in the open); -1 when the cost passes radius or a cell of veil 7 is on
 * the way. Beyond the grid as the game says (outside). The terrain's sight of a grid: dgx_ge_grid_sight_of, ctx the grid.
 */
int dgx_ge_grid_sight(const dgx_ge_grid_t *g, int x0, int y0, int x1, int y1, int radius);
int dgx_ge_grid_sight_of(int x0, int y0, int x1, int y1, int radius, void *ctx);

/*
 * Is a point of the scene solid? In a body of the tile of its cell (as the
 * cell is now: its modifier, its fields); the normal is that of the side of
 * the body nearest the point (into a floor from above: (0, -1)). A tile
 * without bodies: solid all round, solid all through; a solid side alone, a
 * band of edge points along it. The contact gets the point, the normal, the
 * tile as the surface. Beyond the grid, the tile the game says is there
 * (outside), if it says. Every body counts, whatever masks it holds.
 */
bool dgx_ge_grid_solid_at(const dgx_ge_grid_t *g, int x, int y, dgx_ge_contact_t *contact);

/*
 * The same asked by masks (a bit a mask number): only the bodies that hold one
 * of them count (a person's floor as its rules say, a thing's as drawn).
 */
bool dgx_ge_grid_solid_for(const dgx_ge_grid_t *g, int x, int y, unsigned asking, dgx_ge_contact_t *contact);

/*
 * Where a body (the active masks of its picture) is inside the bodies of the
 * tiles: for every body of a tile it reaches into, fn gets the cell, its tile
 * and the box of the points of the masks inside it (points of the scene), so
 * the game may push it out, or let it be (it decides what stops what). The
 * bodies of a tile are worked out once a cell, the masks looked at only where
 * they and a body cross. Cells beyond the grid as the game says (outside).
 * Returns how many boxes.
 */
typedef void (*dgx_ge_grid_overlap_fn)(int col, int row, int tile, dgx_ge_rect_t inside, void *ctx);
int dgx_ge_grid_overlap(const dgx_ge_grid_t *g, const dgx_ge_body_t *body, dgx_ge_grid_overlap_fn fn, void *ctx);

/*
 * What a body touches among the bodies of the tiles (dgx_ge_mask.h: where
 * the solid is as to the points of its masks, within reach points): fn for
 * every mask (the one given, -1 every active one) on every side it touches a
 * body of a tile on, with the cell, the tile and the game's number for it.
 * How many. A floor under the feet, a wall before the body, a ceiling over
 * the head, nothing below (an edge), water around: one question. The bodies
 * are one solid: a point inside one of them is beside no other, so the seam
 * of two slabs is no edge (feet on it touch the floor below, nothing on the
 * left or the right). A mask meets only the bodies that hold it (their for);
 * every active mask (-1): each in turn, among its own bodies.
 */
int dgx_ge_grid_touching(const dgx_ge_grid_t *g, const dgx_ge_body_t *body, int mask, int reach, dgx_ge_touch_fn fn, void *ctx);

/* The sides (DGX_GE_TOUCH_* bits) a mask of a body (-1: every active one) touches the bodies of tiles on. */
unsigned dgx_ge_grid_touch_sides(const dgx_ge_grid_t *g, const dgx_ge_body_t *body, int mask, int reach);

/*
 * A hint: the shortest way back along the line it came by (way_x, way_y: its
 * motion, any scale, not both 0) that takes its active masks out of every
 * body of the tiles: a vector, whole points, into dx, dy. The game applies
 * it, or not: nothing here moves anything. False when it is in none, no way
 * is given, or no way out is found within 16 bodies one behind another (dx,
 * dy 0).
 */
bool dgx_ge_grid_push_out(const dgx_ge_grid_t *g, const dgx_ge_body_t *body, int way_x, int way_y, int *dx, int *dy);

/*
 * What of a body is seen: the open space of the grid it is in, a clip in
 * points of the scene (large where nothing ends it). body: its outline;
 * feet: the point it stands at, whose cell it is in. sides: which sides may
 * cut (DGX_GE_SOLID_*), as the game draws its scene: what is drawn after the
 * actors needs no cut.
 * - Above: if the body reaches into the row over its feet and every cell
 *   there, over the width of the body, closes its bottom, the body is cut at
 *   the top of its own row; only when it reaches in less than depth points
 *   (a head in the slab of a ceiling; deeper it is going through), or the
 *   row lies beyond the grid. Below: the same downwards.
 * - Right: from its feet or the left of its body, whichever is further, the
 *   first cell that closes its right (a door frame it stands in or is under)
 *   cuts at its right edge; else the cell
 *   its right edge reaches into, past the cell of the feet, if it closes its
 *   left over the height of the body (a wall), at its left edge. Left: the same the other
 *   way. Only cells of the grid cut at the sides: what lies beside it is not
 *   drawn in this scene, there is nothing to hide behind; above and below,
 *   cells beyond it count (a strip of the room above shown along the top).
 * - Inside (DGX_GE_SPACE_INSIDE in sides): gone into an opening, it is seen
 *   only through it as well: the opening of the cell of its feet, else of a
 *   neighbour in that row whose opening reaches into the cell (a door whose
 *   left half holds the opening over both). None there: as without.
 */
dgx_ge_rect_t dgx_ge_grid_open_space(const dgx_ge_grid_t *g, dgx_ge_rect_t body, int feet_x, int feet_y, int depth, unsigned sides);

/* The open space of a grid for a scene (dgx_ge_scene_space): the grid, the depth and sides of dgx_ge_grid_open_space. */
typedef struct {
    const dgx_ge_grid_t *grid;
    int16_t              depth;
    uint8_t              sides;
} dgx_ge_space_t;

/* dgx_ge_grid_open_space of a dgx_ge_space_t, as a scene asks it (dgx_ge_space_fn). */
dgx_ge_rect_t dgx_ge_space_clip(const void *space, dgx_ge_rect_t body, int feet_x, int feet_y, bool inside);

/*
 * The terrain of a grid for points: an actor is taken as its origin. A game
 * whose actors have bodies asks dgx_ge_grid_solid_at() at their edges itself.
 * ctx is the grid.
 */
bool dgx_ge_grid_touches(const dgx_ge_actor_t *actor, int32_t x, int32_t y, dgx_ge_contact_t *contact, void *ctx);

#ifdef __cplusplus
}
#endif
