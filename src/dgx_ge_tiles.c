#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "dgx_ge_expr.h"
#include "dgx_ge_image.h"
#include "dgx_ge_tiles.h"

#define HEADER_BYTES  28u
#define TILE_BYTES    12u
#define BODY_BYTES    20u
#define CLOSES_OPENING 16u /* of the sides a tile closes: the record after its bodies is its opening */
#define LAID_BYTES    36u /* of a tile: for each side it is laid with (its own cell, up, ...), a bit for each step it has pieces at */
#define PIECE_BYTES   32u
#define IMAGE_BYTES   2u
#define PICTURE_BYTES 4u
#define COND_BYTES    6u

enum { MODE_FIXED, MODE_PICK, MODE_CYCLE }; /* MODE_PICK: by an expression, or by the modifier without one */
enum { TEST_EMPTY, TEST_TILES, TEST_SOLID, TEST_NOT = 0x80 };
enum { DRAW_NORMAL, DRAW_CARD, DRAW_SILHOUETTE, DRAW_FILL };
#define OWN_SET 0xFF
#define ANY_VARIANT 0xFF
#define NO_EXPR     0xFFFF
#define NO_PICTURE  0xFFFF /* by_mod: no piece in this state */

static inline uint16_t u16_at(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static inline uint32_t u32_at(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* The neighbours of a cell, by the number of a side: up, down, left, right, up-left, up-right, down-left, down-right. */
static const int8_t side_step[8][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}, {-1, -1}, {1, -1}, {-1, 1}, {1, 1}};

/*
 * How far a change of the tile of a cell reaches: a piece is laid with its own cell or a neighbour (its side), and
 * its rules look at the neighbours of its own cell; so a piece laid in a cell depends on the cells as far as its side
 * and a rule's side together. (A modifier or a variant shows only in the pieces of its own cell, where its laid
 * table says they are laid.)
 */
static void find_reach(dgx_ge_tileset_t *set)
{
    for (unsigned i = 0; i < set->pieces; ++i) {
        const uint8_t *r  = set->piece_table + i * PIECE_BYTES;
        const int      sx = r[19] ? abs(side_step[r[19] - 1][0]) : 0, sy = r[19] ? abs(side_step[r[19] - 1][1]) : 0;
        int            cx = 0, cy = 0;
        for (unsigned k = 0; k < r[18]; ++k) {
            const uint8_t *c = set->cond_table + (u16_at(r + 16) + k) * COND_BYTES;
            if (abs(side_step[c[0]][0]) > cx) cx = abs(side_step[c[0]][0]);
            if (abs(side_step[c[0]][1]) > cy) cy = abs(side_step[c[0]][1]);
        }
        if (sx + cx > set->reach_x) set->reach_x = (uint8_t)(sx + cx);
        if (sy + cy > set->reach_y) set->reach_y = (uint8_t)(sy + cy);
    }
}

bool dgx_ge_tileset_open(dgx_ge_tileset_t *set, const dgx_ge_pack_t *pack, const void *data, size_t size)
{
    memset(set, 0, sizeof(*set));
    const uint8_t *p = data;
    if (!p || size < HEADER_BYTES || u32_at(p) != DGX_GE_TILES_MAGIC || u16_at(p + 4) != DGX_GE_TILES_VERSION) return false;
    set->pack       = pack;
    set->cell_w     = u16_at(p + 6);
    set->cell_h     = u16_at(p + 8);
    set->tiles      = u16_at(p + 10);
    set->slots      = u16_at(p + 12);
    set->pieces     = u16_at(p + 14);
    set->conds      = u16_at(p + 16);
    set->pictures   = u16_at(p + 18);
    set->listed     = u16_at(p + 20);
    set->expr_bytes = u16_at(p + 22);
    set->distinct   = u16_at(p + 24);
    set->bodies     = u16_at(p + 26);
    const size_t need = HEADER_BYTES + (size_t)set->tiles * (TILE_BYTES + LAID_BYTES) + (size_t)set->pieces * PIECE_BYTES + (size_t)set->distinct * IMAGE_BYTES +
                        (size_t)set->pictures * PICTURE_BYTES + (size_t)set->conds * COND_BYTES + (size_t)set->listed * 2 + set->expr_bytes +
                        (size_t)set->bodies * BODY_BYTES;
    if (need > size || set->slots > DGX_GE_SCENE_MAX_LAYERS || !set->cell_w || !set->cell_h) return memset(set, 0, sizeof(*set)), false;
    set->table         = p + HEADER_BYTES;
    set->laid_table    = set->table + (size_t)set->tiles * TILE_BYTES;
    set->piece_table   = set->laid_table + (size_t)set->tiles * LAID_BYTES;
    set->image_table   = set->piece_table + (size_t)set->pieces * PIECE_BYTES;
    set->picture_table = set->image_table + (size_t)set->distinct * IMAGE_BYTES;
    set->cond_table    = set->picture_table + (size_t)set->pictures * PICTURE_BYTES;
    set->list_table    = set->cond_table + (size_t)set->conds * COND_BYTES;
    set->exprs         = set->list_table + (size_t)set->listed * 2;
    set->body_table    = set->exprs + set->expr_bytes;
    /* everything a table points at lies in the blob */
    bool ok = true;
    for (unsigned t = 0; ok && t < set->tiles; ++t) {
        const uint8_t *r = set->table + t * TILE_BYTES;
        ok               = (uint32_t)u16_at(r + 4) + r[3] <= set->pieces && (uint32_t)u16_at(r + 8) + r[7] + (r[6] & CLOSES_OPENING ? 1u : 0u) <= set->bodies;
    }
    for (unsigned i = 0; ok && i < set->pieces; ++i) {
        const uint8_t *r = set->piece_table + i * PIECE_BYTES;
        ok = r[2] && (uint32_t)u16_at(r) + r[2] <= set->pictures && r[3] <= MODE_CYCLE && r[12] < set->slots &&
             (uint32_t)u16_at(r + 16) + r[18] <= set->conds && (u16_at(r + 8) == NO_EXPR || u16_at(r + 8) < set->expr_bytes) &&
             (u16_at(r + 10) == NO_EXPR || u16_at(r + 10) < set->expr_bytes) && r[19] <= 8 && r[21] <= DRAW_FILL &&
             (r[22] == OWN_SET || r[22] < pack->sets) && (u16_at(r + 24) == NO_EXPR || u16_at(r + 24) < set->expr_bytes) &&
             (u16_at(r + 26) == NO_EXPR || u16_at(r + 26) < set->expr_bytes) && (u16_at(r + 30) == NO_EXPR || u16_at(r + 30) < set->expr_bytes);
    }
    for (unsigned i = 0; ok && i < set->bodies; ++i)
        for (unsigned k = 0; ok && k < 5; ++k) {
            const uint16_t e = u16_at(set->body_table + i * BODY_BYTES + 8 + 2 * k);
            ok               = e == NO_EXPR || e < set->expr_bytes;
        }
    for (unsigned i = 0; ok && i < set->conds; ++i) {
        const uint8_t *r = set->cond_table + i * COND_BYTES;
        ok               = r[0] < 8 && ((r[1] & 0x7F) != TEST_TILES || (uint32_t)u16_at(r + 2) + u16_at(r + 4) <= set->listed);
    }
    for (unsigned i = 0; ok && i < set->pictures; ++i) {
        const uint16_t k = u16_at(set->picture_table + i * PICTURE_BYTES);
        ok               = k == NO_PICTURE || k < set->distinct;
    }
    if (ok) find_reach(set);
    /* the pictures, each once (a long by_mod names the same few again and again): a dgx_image_t of RAM each */
    set->images = ok ? calloc(set->distinct ? set->distinct : 1, sizeof(dgx_image_t)) : NULL;
    ok          = ok && set->images;
    for (unsigned k = 0; ok && k < set->distinct; ++k) ok = dgx_ge_pack_image(pack, u16_at(set->image_table + k * IMAGE_BYTES), &set->images[k]);
    if (!ok) dgx_ge_tileset_close(set);
    return ok;
}

void dgx_ge_tileset_close(dgx_ge_tileset_t *set)
{
    free(set->images);
    memset(set, 0, sizeof(*set));
}

bool dgx_ge_tile_info(const dgx_ge_tileset_t *set, int tile, uint16_t *kind, uint8_t *solid)
{
    if (!set || tile < 1 || tile > set->tiles) return false;
    const uint8_t *r = set->table + (tile - 1) * TILE_BYTES;
    if (kind) *kind = u16_at(r);
    if (solid) *solid = r[2];
    return true;
}

bool dgx_ge_tileset_uses_set(const dgx_ge_tileset_t *set, int pack_set)
{
    if (!set || pack_set < 0) return false;
    for (unsigned k = 0; k < set->distinct; ++k)
        if (dgx_ge_pack_image_set(set->pack, u16_at(set->image_table + k * IMAGE_BYTES)) == pack_set) return true;
    for (unsigned i = 0; i < set->pieces; ++i) {
        const uint8_t *r = set->piece_table + i * PIECE_BYTES;
        if (r[21] != DRAW_NORMAL && r[22] == pack_set) return true;
    }
    return false;
}

uint16_t dgx_ge_tile_life(const dgx_ge_tileset_t *set, int tile)
{
    return set && tile >= 1 && tile <= set->tiles ? u16_at(set->table + (tile - 1) * TILE_BYTES + 10) : 0xFFFF;
}

uint8_t dgx_ge_tile_veil(const dgx_ge_tileset_t *set, int tile)
{
    return set && tile >= 1 && tile <= set->tiles ? set->table[(tile - 1) * TILE_BYTES + 6] >> 5 : 0;
}

uint8_t dgx_ge_tile_closes(const dgx_ge_tileset_t *set, int tile)
{
    return set && tile >= 1 && tile <= set->tiles ? set->table[(tile - 1) * TILE_BYTES + 6] & DGX_GE_SOLID_ALL : 0;
}

/* ---- the rules of a piece ---- */

/* A cell: its tile, modifier and variant; beyond the grid as the game says, of no variant. */
typedef struct {
    int      tile;
    uint8_t  mod;
    uint32_t variant;
} cell_t;

static cell_t cell_of(const dgx_ge_grid_t *g, int col, int row)
{
    cell_t c = {0, 0, 0};
    if (col >= 0 && row >= 0 && col < g->cols && row < g->rows) {
        const int i = dgx_ge_grid_index(g, col, row);
        c.tile      = g->cells ? g->cells[i] : 0;
        c.mod       = g->mods ? g->mods[i] : 0;
        c.variant   = g->variants ? g->variants[i] : 0;
    } else if (g->outside) {
        c.tile = g->outside(g, col, row, &c.mod, &c.variant, g->ctx);
    }
    return c;
}

static int tile_at(const dgx_ge_grid_t *g, int col, int row)
{
    return cell_of(g, col, row).tile;
}

/* Are the neighbours of the cell as the piece asks? */
static bool around_holds(const dgx_ge_grid_t *g, int col, int row, const uint8_t *piece)
{
    const dgx_ge_tileset_t *set = g->tileset;
    for (unsigned i = 0; i < piece[18]; ++i) {
        const uint8_t *c    = set->cond_table + (u16_at(piece + 16) + i) * COND_BYTES;
        const int      tile = tile_at(g, col + side_step[c[0]][0], row + side_step[c[0]][1]);
        bool           yes;
        switch (c[1] & 0x7F) {
        case TEST_EMPTY: yes = tile == 0; break;
        case TEST_SOLID: {
            uint8_t solid = 0;
            dgx_ge_tile_info(set, tile, NULL, &solid);
            yes = (solid & u16_at(c + 2)) != 0;
            break;
        }
        default:
            yes = false;
            for (unsigned k = 0; !yes && k < u16_at(c + 4); ++k) yes = u16_at(set->list_table + (u16_at(c + 2) + k) * 2) == tile;
            break;
        }
        if (yes == ((c[1] & TEST_NOT) != 0)) return false;
    }
    return true;
}

/* What the expressions of a cell read: its modifier and variant (and their fields); made once for all of them. */
typedef struct {
    uint32_t          words[2];
    dgx_ge_expr_ctx_t c;
} cell_expr_t;

static void cell_expr(cell_expr_t *e, cell_t cell)
{
    e->words[0] = cell.mod, e->words[1] = cell.variant;
    e->c        = (dgx_ge_expr_ctx_t){.words = e->words, .word_count = 2};
}

/* An expression of a piece in a cell, rounded; or 0 without one. */
static int value_of(const dgx_ge_tileset_t *set, uint16_t expr, const cell_expr_t *e)
{
    if (expr == NO_EXPR) return 0;
    return (int)lroundf(dgx_ge_expr_eval(set->exprs + expr, &e->c));
}

/* The picture a piece shows in a cell: fixed, picked (by an expression, or by the modifier), or of the cycle at the time of the grid. */
static unsigned picture_of(const dgx_ge_grid_t *g, const uint8_t *piece, cell_t owner, const cell_expr_t *e, uint32_t now_ms)
{
    const dgx_ge_tileset_t *set = g->tileset;
    const unsigned          first = u16_at(piece), count = piece[2];
    if (piece[3] == MODE_PICK) {
        const int k = u16_at(piece + 24) == NO_EXPR ? owner.mod : value_of(set, u16_at(piece + 24), e);
        return first + (unsigned)(k < 0 ? 0 : k < (int)count ? k : (int)count - 1);
    }
    if (piece[3] != MODE_CYCLE) return first;
    uint32_t round = 0;
    for (unsigned i = 0; i < count; ++i) round += u16_at(set->picture_table + (first + i) * PICTURE_BYTES + 2);
    if (!round) return first;
    uint32_t at = now_ms % round;
    for (unsigned i = 0; i < count; ++i) {
        const uint32_t ms = u16_at(set->picture_table + (first + i) * PICTURE_BYTES + 2);
        if (at < ms) return first + i;
        at -= ms;
    }
    return first + count - 1;
}

/* The item of a piece of the tile of a cell (its owner, which may lie beyond the grid) at a time; false when its rules leave it out there. */
static bool piece_item(const dgx_ge_grid_t *g, int col, int row, cell_t owner, unsigned piece, uint32_t now_ms, dgx_ge_item_t *item)
{
    const dgx_ge_tileset_t *set = g->tileset;
    const uint8_t          *r   = set->piece_table + piece * PIECE_BYTES;
    if (r[13] != ANY_VARIANT && r[13] != owner.variant) return false;
    if (r[18] && !around_holds(g, col, row, r)) return false;
    cell_expr_t e;
    cell_expr(&e, owner);
    if (u16_at(r + 30) != NO_EXPR && !value_of(set, u16_at(r + 30), &e)) return false;
    const unsigned picture = picture_of(g, r, owner, &e, now_ms);
    const uint16_t image   = u16_at(set->picture_table + picture * PICTURE_BYTES);
    if (image == NO_PICTURE && r[21] != DRAW_FILL) return false;
    const int set_of = image == NO_PICTURE ? -1 : dgx_ge_pack_image_set(set->pack, u16_at(set->image_table + image * IMAGE_BYTES));
    /* the bottom-left point of the cell */
    const int x0 = g->x + col * set->cell_w, y0 = g->y + (row + 1) * set->cell_h - 1;
    *item        = (dgx_ge_item_t){
               .image = image == NO_PICTURE ? NULL : &set->images[image],
               .map   = g->maps && set_of >= 0 ? g->maps[set_of] : NULL,
               .x     = (int16_t)(x0 + (int16_t)u16_at(r + 4) + value_of(set, u16_at(r + 8), &e)),
               .y     = (int16_t)(y0 + (int16_t)u16_at(r + 6) + value_of(set, u16_at(r + 10), &e)),
               .key   = (int16_t)u16_at(r + 14),
               .layer = g->layer_of[r[12]],
    };
    if (r[21] != DRAW_NORMAL) {
        /* the color of a card, a silhouette or a fill: 0 the paper, n the color n of a set (a number, or an expression) */
        const int      color_set = r[22] == OWN_SET ? set_of : r[22];
        const uint8_t *map       = g->maps && color_set >= 0 ? g->maps[color_set] : NULL;
        const int      n         = u16_at(r + 26) == NO_EXPR ? r[23] : value_of(set, u16_at(r + 26), &e);
        item->color              = n > 0 && n <= 255 && map ? map[n - 1] : 0;
        item->flags              = r[21] == DRAW_CARD ? DGX_GE_ITEM_CARD : r[21] == DRAW_FILL ? DGX_GE_ITEM_FILL : DGX_GE_BLIT_SILHOUETTE;
    }
    if (r[21] == DRAW_FILL) {
        /* a rectangle of its color, its bottom-left at the place of the piece */
        item->clip = dgx_ge_rect(item->x, item->y - r[29] + 1, r[28], r[29]);
        item->x = item->y = 0;
    }
    return true;
}

/* ---- the grid ---- */

/* The sides a piece may be laid with, by the number it has for them: 0 its own cell, 1.. up, down, left, right, the corners. */
static int with_col(int side)
{
    return side ? side_step[side - 1][0] : 0;
}

static int with_row(int side)
{
    return side ? side_step[side - 1][1] : 0;
}

/*
 * The owners of the pieces laid with a cell from each side: the cells the
 * sides lead to from it, looked at backwards; and the steps (0..31) any of them
 * has pieces at with it.
 */
static uint32_t owners_of(const dgx_ge_grid_t *g, int col, int row, cell_t owners[9])
{
    const dgx_ge_tileset_t *set   = g->tileset;
    uint32_t                steps = 0;
    for (int side = 0; side <= 8; ++side) {
        owners[side] = cell_of(g, col - with_col(side), row - with_row(side));
        if (owners[side].tile >= 1 && owners[side].tile <= set->tiles) steps |= u32_at(set->laid_table + (owners[side].tile - 1) * LAID_BYTES + side * 4);
    }
    return steps;
}

/* A cell laid at a step: its own pieces of the step and those of its neighbours laid with it, in this order. */
static bool lay_with(const dgx_ge_grid_t *g, int col, int row, int step, const cell_t owners[9], dgx_ge_scene_t *scene)
{
    const dgx_ge_tileset_t *set = g->tileset;
    if (!set) return true;
    for (int side = 0; side <= 8; ++side) {
        const int    oc = col - with_col(side), orow = row - with_row(side);
        const cell_t owner = owners[side];
        if (owner.tile < 1 || owner.tile > set->tiles) continue;
        /* none of its pieces at this step from this side: nothing to look through */
        if (step < 32 && !(u32_at(set->laid_table + (owner.tile - 1) * LAID_BYTES + side * 4) & 1u << step)) continue;
        const uint8_t *t = set->table + (owner.tile - 1) * TILE_BYTES;
        for (unsigned i = 0; i < t[3]; ++i) {
            const unsigned piece = u16_at(t + 4) + i;
            const uint8_t *r     = set->piece_table + piece * PIECE_BYTES;
            if (r[19] != side || r[20] != step) continue;
            dgx_ge_item_t item;
            if (piece_item(g, oc, orow, owner, piece, g->now_ms, &item) && dgx_ge_scene_add(scene, &item) < 0) return false;
        }
    }
    return true;
}

bool dgx_ge_grid_lay(const dgx_ge_grid_t *g, int col, int row, int step, dgx_ge_scene_t *scene)
{
    cell_t owners[9];
    owners_of(g, col, row, owners);
    return lay_with(g, col, row, step, owners, scene);
}

/* Every step of a cell: the hook's pieces and those of data; false when the set had no room. */
static bool lay_cell(const dgx_ge_grid_t *g, int col, int row, dgx_ge_scene_t *scene)
{
    const int      steps = g->steps ? g->steps : 1;
    cell_t         owners[9];
    const uint32_t laid = g->tileset ? owners_of(g, col, row, owners) : 0;
    for (int step = 0; step < steps; ++step) {
        if (step < 32 && (g->skip & 1u << step)) continue;
        if (g->draw) g->draw(g, col, row, step, scene, g->ctx);
        /* nothing of data at this step: no side to look at */
        if (step < 32 && !(laid & 1u << step)) continue;
        if (!lay_with(g, col, row, step, owners, scene)) return false;
    }
    return true;
}

bool dgx_ge_grid_add(const dgx_ge_grid_t *g, dgx_ge_scene_t *scene)
{
    const int cells = g->cols * g->rows;
    for (int r = 0; r < g->rows; ++r)
        for (int col = 0; col < g->cols; ++col) {
            if (g->laid) g->laid[r * g->cols + col] = scene->set_count;
            if (!lay_cell(g, col, g->bottom_up ? g->rows - 1 - r : r, scene)) {
                if (g->laid) g->laid[cells] = 0xFFFF; /* laid in part: no more than the set holds, so never taken for whole */
                return false;
            }
        }
    if (g->laid) g->laid[cells] = scene->set_count;
    return true;
}

/* A cell laid again at the end of the set and compared with its pieces there before; false when their number differs or there was no room. */
static bool relay_cell(const dgx_ge_grid_t *g, dgx_ge_scene_t *scene, int col, int row)
{
    const int      k = (g->bottom_up ? g->rows - 1 - row : row) * g->cols + col;
    const uint16_t was = g->laid[k], was_end = g->laid[k + 1], end = scene->set_count;
    const bool     quiet = scene->quiet;
    scene->quiet         = true; /* what is laid beyond the set to be compared marks nothing */
    const bool room      = lay_cell(g, col, row, scene);
    scene->quiet         = quiet;
    const int n          = scene->set_count - end;
    bool      same       = room && n == was_end - was;
    /* a piece shown otherwise changes in its place; when they do not match, the places of both are marked */
    for (int i = 0; same && i < n; ++i)
        if (!dgx_ge_item_same(&scene->set[was + i], &scene->set[end + i])) dgx_ge_scene_change(scene, was + i, &scene->set[end + i]);
    for (int i = 0; !same && room && i < was_end - was + n; ++i) {
        const int h = i < was_end - was ? was + i : end + i - (was_end - was);
        dgx_ge_scene_change(scene, h, &scene->set[h]);
    }
    if (!room) dgx_ge_frame_invalidate_all(scene->frame);
    scene->set_count = end;
    return same;
}

bool dgx_ge_grid_relay(const dgx_ge_grid_t *g, dgx_ge_scene_t *scene, int col, int row, bool kind)
{
    const dgx_ge_tileset_t *set   = g->tileset;
    const int               cells = g->cols * g->rows;
    if (!g->laid || g->laid[cells] > scene->set_count) {
        dgx_ge_frame_invalidate_all(scene->frame);
        return false;
    }
    bool      same = true;
    const int tile = tile_at(g, col, row);
    if (!kind) {
        /* its modifier or variant: the tile is the one it was, only its own pieces show it, laid where its laid table says (and the cell itself, for the hook) */
        for (int side = 0; side <= 8; ++side) {
            const int c = col + with_col(side), r = row + with_row(side);
            const bool here = set && tile >= 1 && tile <= set->tiles && u32_at(set->laid_table + (tile - 1) * LAID_BYTES + side * 4);
            if ((here || side == 0) && c >= 0 && r >= 0 && c < g->cols && r < g->rows) same &= relay_cell(g, scene, c, r);
        }
        return same;
    }
    /* its tile: the rules of the pieces of its neighbours may look at it too, as far as the set says */
    const int rx = set ? set->reach_x : 0, ry = set ? set->reach_y : 0;
    for (int r = row - ry; r <= row + ry; ++r)
        for (int c = col - rx; c <= col + rx; ++c)
            if (c >= 0 && r >= 0 && c < g->cols && r < g->rows) same &= relay_cell(g, scene, c, r);
    return same;
}

bool dgx_ge_grid_figure(const dgx_ge_grid_t *g, int tile, uint8_t mod, uint32_t variant, int x, int y, dgx_ge_scene_t *scene)
{
    const dgx_ge_tileset_t *set = g->tileset;
    if (!set || tile < 1 || tile > set->tiles) return true;
    /* a grid of one cell where the figure stands, its own and nothing around */
    uint16_t      cell = (uint16_t)tile;
    dgx_ge_grid_t one  = *g;
    one.cells = &cell, one.mods = &mod, one.variants = &variant, one.laid = NULL;
    one.cols = one.rows = 1, one.ring = false, one.ring_col = one.ring_row = 0, one.outside = NULL;
    one.x = (int16_t)x, one.y = (int16_t)(y - set->cell_h + 1);
    const cell_t   owner = {tile, mod, variant};
    const uint8_t *t     = set->table + (tile - 1) * TILE_BYTES;
    const int      steps = g->steps ? g->steps : 1;
    for (int step = 0; step < steps; ++step)
        for (unsigned i = 0; i < t[3]; ++i) {
            const unsigned piece = u16_at(t + 4) + i;
            dgx_ge_item_t  item;
            if (set->piece_table[piece * PIECE_BYTES + 20] != step || !piece_item(&one, 0, 0, owner, piece, g->now_ms, &item)) continue;
            if (!dgx_ge_scene_actor(scene, &item)) return false;
        }
    return true;
}

/* The place of the pieces of a tile in a cell at a time, wherever they are laid. */
static dgx_ge_rect_t box_at(const dgx_ge_grid_t *g, int col, int row, int tile, uint32_t now_ms)
{
    const dgx_ge_tileset_t *set = g->tileset;
    dgx_ge_rect_t           box = dgx_ge_rect(0, 0, 0, 0);
    if (!set || tile < 1 || tile > set->tiles) return box;
    cell_t owner = cell_of(g, col, row);
    owner.tile   = tile;
    const uint8_t *t = set->table + (tile - 1) * TILE_BYTES;
    for (unsigned i = 0; i < t[3]; ++i) {
        dgx_ge_item_t item;
        if (!piece_item(g, col, row, owner, u16_at(t + 4) + i, now_ms, &item)) continue;
        const dgx_ge_rect_t r = item.image ? dgx_ge_image_box(item.image, item.x, item.y, false) : item.clip;
        box                   = dgx_ge_rect_empty(box) ? r : dgx_ge_rect_union(box, r);
    }
    return box;
}

dgx_ge_rect_t dgx_ge_grid_cell_box(const dgx_ge_grid_t *g, int col, int row, int tile)
{
    return box_at(g, col, row, tile, g->now_ms);
}

bool dgx_ge_grid_same_look(const dgx_ge_grid_t *g, int col, int row, int tile_a, uint8_t mod_a, int tile_b, uint8_t mod_b)
{
    const dgx_ge_tileset_t *set = g->tileset;
    if (tile_a != tile_b) return false;
    if (mod_a == mod_b) return true;
    if (!set || tile_a < 1 || tile_a > set->tiles) return true;
    /* its own pieces, the one state against the other: shown or not, the same picture at the same place, drawn the same */
    const cell_t   here = cell_of(g, col, row);
    const cell_t   a = {tile_a, mod_a, here.variant}, b = {tile_b, mod_b, here.variant};
    const uint8_t *t = set->table + (tile_a - 1) * TILE_BYTES;
    for (unsigned i = 0; i < t[3]; ++i) {
        const unsigned piece = u16_at(t + 4) + i;
        dgx_ge_item_t  ia, ib;
        const bool     sa = piece_item(g, col, row, a, piece, g->now_ms, &ia), sb = piece_item(g, col, row, b, piece, g->now_ms, &ib);
        if (sa != sb) return false;
        if (sa && (ia.image != ib.image || ia.map != ib.map || ia.x != ib.x || ia.y != ib.y || ia.flags != ib.flags || ia.color != ib.color ||
                   !dgx_ge_rect_equal(ia.clip, ib.clip)))
            return false;
    }
    return true;
}

dgx_ge_rect_t dgx_ge_grid_change_box(const dgx_ge_grid_t *g, int col, int row)
{
    dgx_ge_rect_t box = dgx_ge_rect(0, 0, 0, 0);
    for (int dr = -1; dr <= 1; ++dr)
        for (int dc = -1; dc <= 1; ++dc) {
            const dgx_ge_rect_t r = box_at(g, col + dc, row + dr, tile_at(g, col + dc, row + dr), g->now_ms);
            if (!dgx_ge_rect_empty(r)) box = dgx_ge_rect_empty(box) ? r : dgx_ge_rect_union(box, r);
        }
    return box;
}

int dgx_ge_grid_animate(dgx_ge_grid_t *g, uint32_t now_ms, dgx_ge_rect_t *places, int max)
{
    const dgx_ge_tileset_t *set = g->tileset;
    int                     n   = 0;
    for (int row = 0; set && row < g->rows; ++row)
        for (int col = 0; col < g->cols; ++col) {
            const int tile = dgx_ge_grid_cell(g, col, row);
            if (tile < 1 || tile > set->tiles) continue;
            /* a cycle of this tile showing another picture now */
            const uint8_t *t       = set->table + (tile - 1) * TILE_BYTES;
            const cell_t   owner   = cell_of(g, col, row);
            bool           changed = false;
            for (unsigned i = 0; !changed && i < t[3]; ++i) {
                const uint8_t *r = set->piece_table + (u16_at(t + 4) + i) * PIECE_BYTES;
                changed          = r[3] == MODE_CYCLE && picture_of(g, r, owner, NULL, g->now_ms) != picture_of(g, r, owner, NULL, now_ms); /* a cycle reads no expression */
            }
            if (!changed) continue;
            if (n < max) {
                const dgx_ge_rect_t was = box_at(g, col, row, tile, g->now_ms), is = box_at(g, col, row, tile, now_ms);
                places[n] = dgx_ge_rect_empty(was) ? is : dgx_ge_rect_empty(is) ? was : dgx_ge_rect_union(was, is);
            }
            ++n;
        }
    g->now_ms = now_ms;
    return n;
}

static int floor_div(int a, int b)
{
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

bool dgx_ge_grid_cell_at(const dgx_ge_grid_t *g, int x, int y, int *col, int *row)
{
    if (!g->tileset) return false;
    *col = floor_div(x - g->x, g->tileset->cell_w);
    *row = floor_div(y - g->y, g->tileset->cell_h);
    return *col >= 0 && *row >= 0 && *col < g->cols && *row < g->rows;
}

/* A number of a body in a cell: its value, or its expression worked out. */
static int body_value(const dgx_ge_tileset_t *set, const uint8_t *body, int k, const cell_expr_t *e)
{
    const uint16_t x = u16_at(body + 8 + 2 * k);
    return x == NO_EXPR ? (int16_t)u16_at(body + 2 * k) : value_of(set, x, e);
}

/* Does a body of a tile hold one of the masks asking (its for: the masks it holds; 0, every one)? */
static bool holds(const uint8_t *body, unsigned asking)
{
    const uint16_t f = u16_at(body + 18);
    return !f || (f & asking);
}

/* Is a point solid at all (no normal)? */
static bool solid_point(const dgx_ge_grid_t *g, int x, int y, unsigned asking)
{
    return dgx_ge_grid_solid_for(g, x, y, asking, NULL);
}

bool dgx_ge_grid_solid_at(const dgx_ge_grid_t *g, int x, int y, dgx_ge_contact_t *c)
{
    return dgx_ge_grid_solid_for(g, x, y, 0xFFFFu, c);
}

bool dgx_ge_grid_solid_for(const dgx_ge_grid_t *g, int x, int y, unsigned asking, dgx_ge_contact_t *c)
{
    if (!g->tileset) return false;
    const dgx_ge_tileset_t *set = g->tileset;
    const int w = set->cell_w, h = set->cell_h, col = floor_div(x - g->x, w), row = floor_div(y - g->y, h);
    int       nx = 0, ny = 0, depth = 0x7fff, surface = 0;
    /*
     * The bodies of the cell the point is in and of the cells around it: a
     * body may reach into the next cell (a floor from the middle of its cell
     * to the middle of the next). The nearest side of the one the point is
     * in that is a surface: one with nothing solid just beyond it (where a
     * floor goes on into the next cell, its side there is no surface); all
     * inside: the nearest side at all.
     */
    for (int dr = -1; dr <= 1; ++dr)
        for (int dc = -1; dc <= 1; ++dc) {
            const int  cc = col + dc, rr = row + dr;
            const bool in = cc >= 0 && rr >= 0 && cc < g->cols && rr < g->rows;
            if (!in && !g->outside) continue;
            const cell_t cell = cell_of(g, cc, rr);
            if (cell.tile < 1 || cell.tile > set->tiles) continue;
            const uint8_t *t = set->table + (cell.tile - 1) * TILE_BYTES;
            const int      from_left = x - (g->x + cc * w), from_bottom = h - 1 - (y - (g->y + rr * h));
            cell_expr_t    e;
            if (t[7]) cell_expr(&e, cell);
            for (unsigned i = 0; i < t[7]; ++i) {
                const uint8_t *b = set->body_table + (u16_at(t + 8) + i) * BODY_BYTES;
                if (!holds(b, asking) || (u16_at(b + 16) != NO_EXPR && !value_of(set, u16_at(b + 16), &e))) continue;
                const int bx = body_value(set, b, 0, &e), by = body_value(set, b, 1, &e), bw = body_value(set, b, 2, &e), bh = body_value(set, b, 3, &e);
                const int fx = from_left - bx, fy = -from_bottom - (by - bh + 1); /* from its left; from its top, down (y from the bottom row: 0, up negative) */
                if (bw <= 0 || bh <= 0 || fx < 0 || fx >= bw || fy < 0 || fy >= bh) continue;
                if (!c) return true; /* solid; which side does not matter */
                const int faces[4][4] = {{fy, 0, -1, 0}, {fx, -1, 0, 0}, {bw - 1 - fx, 1, 0, 0}, {bh - 1 - fy, 0, 1, 0}};
                int       best = -1, any = -1;
                for (int f = 0; f < 4; ++f) {
                    const int d = faces[f][0];
                    if (any < 0 || d < faces[any][0]) any = f;
                    if ((best < 0 || d < faces[best][0]) && !solid_point(g, x + faces[f][1] * (d + 1), y + faces[f][2] * (d + 1), asking)) best = f;
                }
                if (best < 0) best = any;
                if (faces[best][0] < depth) depth = faces[best][0], nx = faces[best][1], ny = faces[best][2], surface = cell.tile;
            }
        }
    if (depth != 0x7fff) {
        c->x = x * 256, c->y = y * 256;
        c->nx = (float)nx, c->ny = (float)ny;
        c->surface = surface;
        return true;
    }
    /* no body here: the solid sides of the tile of its own cell, a band of edge points along each */
    const bool inside = col >= 0 && row >= 0 && col < g->cols && row < g->rows;
    if (!inside && !g->outside) return false;
    const cell_t cell = cell_of(g, col, row);
    uint8_t      solid;
    if (!dgx_ge_tile_info(set, cell.tile, NULL, &solid)) return false;
    if (set->table[(cell.tile - 1) * TILE_BYTES + 7]) return false; /* a tile of bodies, the point in none of them */
    const int from_left = x - (g->x + col * w), from_top = y - (g->y + row * h), from_right = w - 1 - from_left, from_bottom = h - 1 - from_top;
    if (!solid) return false;
    if (solid == DGX_GE_SOLID_ALL) {
        /* solid all through: the normal of the nearest side */
        if (from_top < depth) depth = from_top, nx = 0, ny = -1;
        if (from_left < depth) depth = from_left, nx = -1, ny = 0;
        if (from_right < depth) depth = from_right, nx = 1, ny = 0;
        if (from_bottom < depth) depth = from_bottom, nx = 0, ny = 1;
    } else {
        /* the bands along the solid sides; the shallowest one is the side touched */
        const int e = g->edge ? g->edge : 1;
        if ((solid & DGX_GE_SOLID_TOP) && from_top < e && from_top < depth) depth = from_top, nx = 0, ny = -1;
        if ((solid & DGX_GE_SOLID_LEFT) && from_left < e && from_left < depth) depth = from_left, nx = -1, ny = 0;
        if ((solid & DGX_GE_SOLID_RIGHT) && from_right < e && from_right < depth) depth = from_right, nx = 1, ny = 0;
        if ((solid & DGX_GE_SOLID_BOTTOM) && from_bottom < e && from_bottom < depth) depth = from_bottom, nx = 0, ny = 1;
        if (depth == 0x7fff) return false;
    }
    if (c) {
        c->x = x * 256, c->y = y * 256;
        c->nx = (float)nx, c->ny = (float)ny;
        c->surface = cell.tile;
    }
    return true;
}

/* The box of the active masks of a body (or of one of them, mask >= 0) in the scene; empty: none. */
static dgx_ge_rect_t masks_box(const dgx_ge_body_t *body, const dgx_ge_mask_t *masks, int n, int mask)
{
    dgx_ge_rect_t all = dgx_ge_rect(0, 0, 0, 0);
    for (int i = 0; i < n; ++i)
        if ((body->active >> masks[i].index & 1) && (mask < 0 || masks[i].index == mask)) {
            const dgx_ge_rect_t r = dgx_ge_mask_place(&masks[i], body->x, body->y, body->flip);
            all                   = dgx_ge_rect_empty(all) ? r : dgx_ge_rect_union(all, r);
        }
    return all;
}

/*
 * Every body of a tile that may reach into a rectangle of the scene, as the cells are now, holding one of the masks
 * asking: fn with its cell, tile, rectangle and the masks it holds (every one: 0xFFFF).
 */
typedef void (*solid_fn)(int col, int row, int tile, dgx_ge_rect_t solid, unsigned held, void *ctx);

static void each_solid(const dgx_ge_grid_t *g, dgx_ge_rect_t all, unsigned asking, solid_fn fn, void *ctx)
{
    const dgx_ge_tileset_t *set = g->tileset;
    const int               w = set->cell_w, h = set->cell_h;
    /* the cells the rectangle lies over and those around them: a body may reach into the next cell (a floor from the middle of its cell to the middle of the next) */
    for (int row = floor_div(all.y - g->y, h) - 1; row <= floor_div(all.y + all.h - 1 - g->y, h) + 1; ++row)
        for (int col = floor_div(all.x - g->x, w) - 1; col <= floor_div(all.x + all.w - 1 - g->x, w) + 1; ++col) {
            const bool inside = col >= 0 && row >= 0 && col < g->cols && row < g->rows;
            if (!inside && !g->outside) continue;
            const cell_t cell = cell_of(g, col, row);
            if (cell.tile < 1 || cell.tile > set->tiles) continue;
            const uint8_t *t = set->table + (cell.tile - 1) * TILE_BYTES;
            cell_expr_t    e;
            if (t[7]) cell_expr(&e, cell);
            for (unsigned b = 0; b < t[7]; ++b) {
                const uint8_t *r = set->body_table + (u16_at(t + 8) + b) * BODY_BYTES;
                if (!holds(r, asking) || (u16_at(r + 16) != NO_EXPR && !value_of(set, u16_at(r + 16), &e))) continue;
                const int bx = body_value(set, r, 0, &e), by = body_value(set, r, 1, &e), bw = body_value(set, r, 2, &e), bh = body_value(set, r, 3, &e);
                if (bw <= 0 || bh <= 0) continue;
                const int bottom = g->y + (row + 1) * h - 1 + by;
                fn(col, row, cell.tile, dgx_ge_rect(g->x + col * w + bx, bottom - bh + 1, bw, bh), u16_at(r + 18) ? u16_at(r + 18) : 0xFFFFu, ctx);
            }
        }
}

/* The box of the points of the active masks of a body inside a rectangle; empty when none. */
static dgx_ge_rect_t inside_box(const dgx_ge_body_t *body, const dgx_ge_mask_t *masks, int n, dgx_ge_rect_t solid, unsigned held)
{
    int x0 = 0x7fff, y0 = 0x7fff, x1 = -0x7fff, y1 = -0x7fff;
    for (int i = 0; i < n; ++i) {
        if (!(body->active >> masks[i].index & 1) || !(held >> masks[i].index & 1)) continue;
        const dgx_ge_rect_t place = dgx_ge_mask_place(&masks[i], body->x, body->y, body->flip), both = dgx_ge_rect_intersect(place, solid);
        for (int sy = both.y; sy < both.y + both.h; ++sy)
            for (int sx = both.x; sx < both.x + both.w; ++sx)
                if (dgx_ge_mask_has(&masks[i], place, body->flip, sx, sy)) {
                    if (sx < x0) x0 = sx;
                    if (sx > x1) x1 = sx;
                    if (sy < y0) y0 = sy;
                    if (sy > y1) y1 = sy;
                }
    }
    return x1 < x0 ? dgx_ge_rect(0, 0, 0, 0) : dgx_ge_rect(x0, y0, x1 - x0 + 1, y1 - y0 + 1);
}

typedef struct {
    const dgx_ge_body_t   *body;
    const dgx_ge_mask_t   *masks;
    int                    n, mask, reach, found;
    dgx_ge_grid_overlap_fn overlap;
    dgx_ge_touch_fn        touch;
    void                  *ctx;
    const dgx_ge_grid_t   *g;
    int                    way_x, way_y; /* the push out: the way it came, and how far back along it, a scalar of the way */
    float                  t;
    const dgx_ge_rect_t   *solids; /* every solid about the body, for touches among them (NULL: each on its own) */
    int                    solid_count;
} walk_t;

static void overlap_solid(int col, int row, int tile, dgx_ge_rect_t solid, unsigned held, void *ctx)
{
    walk_t             *w  = ctx;
    const dgx_ge_rect_t in = inside_box(w->body, w->masks, w->n, solid, held);
    if (dgx_ge_rect_empty(in)) return;
    ++w->found;
    if (w->overlap) w->overlap(col, row, tile, in, w->ctx);
}

int dgx_ge_grid_overlap(const dgx_ge_grid_t *g, const dgx_ge_body_t *body, dgx_ge_grid_overlap_fn fn, void *ctx)
{
    if (!g->tileset || !body->pack) return 0;
    dgx_ge_mask_t masks[16];
    const int     n   = dgx_ge_pack_masks(body->pack, body->image, masks, 16);
    walk_t        w   = {.body = body, .masks = masks, .n = n, .mask = -1, .overlap = fn, .ctx = ctx, .g = g};
    dgx_ge_rect_t all = masks_box(body, masks, n, -1);
    if (dgx_ge_rect_empty(all)) return 0;
    each_solid(g, all, body->active, overlap_solid, &w);
    return w.found;
}

static void touch_solid(int col, int row, int tile, dgx_ge_rect_t solid, unsigned held, void *ctx)
{
    walk_t *w = ctx;
    for (int i = 0; i < w->n; ++i) {
        if (!(w->body->active >> w->masks[i].index & 1) || (w->mask >= 0 && w->masks[i].index != w->mask) || !(held >> w->masks[i].index & 1)) continue;
        dgx_ge_area_t  areas[DGX_GE_TOUCH_SIDES];
        const unsigned bits = dgx_ge_mask_touch_rect_among(&w->masks[i], dgx_ge_mask_place(&w->masks[i], w->body->x, w->body->y, w->body->flip),
                                                           w->body->flip, solid, w->reach, w->solids, w->solid_count, areas);
        if (!bits) continue;
        uint16_t kind = 0;
        uint8_t  sides;
        dgx_ge_tile_info(w->g->tileset, tile, &kind, &sides);
        for (int s = 0; s < DGX_GE_TOUCH_SIDES; ++s) {
            if (!(bits >> s & 1)) continue;
            ++w->found;
            if (w->touch)
                w->touch(&(dgx_ge_touch_t){.mask = w->masks[i].index, .from = (uint8_t)(1u << s), .area = areas[s], .handle = -1, .index = -1,
                                           .col = (int16_t)col, .row = (int16_t)row, .tile = (uint16_t)tile, .kind = kind, .solid = solid},
                         w->ctx);
        }
    }
}

/* The solids about a body, gathered first: a touch is then found among them all (a point inside one is beside no other). */
#define SOLIDS_ABOUT 32
typedef struct {
    dgx_ge_rect_t rects[SOLIDS_ABOUT];
    int16_t       col[SOLIDS_ABOUT], row[SOLIDS_ABOUT];
    uint16_t      tile[SOLIDS_ABOUT];
    int           count, over;
} solids_t;

static void gather_solid(int col, int row, int tile, dgx_ge_rect_t solid, unsigned held, void *ctx)
{
    solids_t *s = ctx;
    (void)held; /* gathered for one mask: every body here holds it */
    if (s->count >= SOLIDS_ABOUT) {
        ++s->over;
        return;
    }
    s->col[s->count] = (int16_t)col, s->row[s->count] = (int16_t)row, s->tile[s->count] = (uint16_t)tile;
    s->rects[s->count++] = solid;
}

int dgx_ge_grid_touching(const dgx_ge_grid_t *g, const dgx_ge_body_t *body, int mask, int reach, dgx_ge_touch_fn fn, void *ctx)
{
    if (!g->tileset || !body->pack) return 0;
    if (reach < 0) reach = 0;
    if (mask < 0) {
        /* every active mask: one at a time, each among the bodies that hold it */
        int found = 0;
        for (int m = 0; m < 16; ++m)
            if (body->active >> m & 1) found += dgx_ge_grid_touching(g, body, m, reach, fn, ctx);
        return found;
    }
    dgx_ge_mask_t masks[16];
    const int     n   = dgx_ge_pack_masks(body->pack, body->image, masks, 16);
    walk_t        w   = {.body = body, .masks = masks, .n = n, .mask = mask, .reach = reach, .touch = fn, .ctx = ctx, .g = g};
    dgx_ge_rect_t all = masks_box(body, masks, n, mask);
    if (dgx_ge_rect_empty(all)) return 0;
    const dgx_ge_rect_t about = dgx_ge_rect(all.x - reach, all.y - reach, all.w + 2 * reach, all.h + 2 * reach);
    solids_t            s     = {.count = 0, .over = 0};
    each_solid(g, about, 1u << mask, gather_solid, &s);
    if (s.over) {
        /* more solids about than are kept at once: each on its own, as a crowd of small bodies is no slab */
        each_solid(g, about, 1u << mask, touch_solid, &w);
        return w.found;
    }
    w.solids = s.rects, w.solid_count = s.count;
    for (int i = 0; i < s.count; ++i) touch_solid(s.col[i], s.row[i], s.tile[i], s.rects[i], 1u << mask, &w);
    return w.found;
}

static void touch_sides_of(const dgx_ge_touch_t *t, void *ctx)
{
    *(unsigned *)ctx |= t->from;
}

unsigned dgx_ge_grid_touch_sides(const dgx_ge_grid_t *g, const dgx_ge_body_t *body, int mask, int reach)
{
    unsigned bits = 0;
    dgx_ge_grid_touching(g, body, mask, reach, touch_sides_of, &bits);
    return bits;
}

/* How far back along the way (a scalar of it) the box inside this body must go to be out of it: by the nearest of its faces the way leads out of. */
static void push_solid(int col, int row, int tile, dgx_ge_rect_t solid, unsigned held, void *ctx)
{
    (void)col, (void)row, (void)tile;
    walk_t             *w  = ctx;
    const dgx_ge_rect_t in = inside_box(w->body, w->masks, w->n, solid, held);
    if (dgx_ge_rect_empty(in)) return;
    ++w->found;
    float t = 1e30f;
    if (w->way_x > 0) t = (float)(in.x + in.w - solid.x) / (float)w->way_x;
    if (w->way_x < 0) t = (float)(solid.x + solid.w - in.x) / (float)-w->way_x;
    if (w->way_y > 0) t = fminf(t, (float)(in.y + in.h - solid.y) / (float)w->way_y);
    if (w->way_y < 0) t = fminf(t, (float)(solid.y + solid.h - in.y) / (float)-w->way_y);
    if (t > w->t) w->t = t;
}

bool dgx_ge_grid_push_out(const dgx_ge_grid_t *g, const dgx_ge_body_t *body, int way_x, int way_y, int *dx, int *dy)
{
    *dx = *dy = 0;
    if (!g->tileset || !body->pack || (!way_x && !way_y)) return false;
    dgx_ge_mask_t masks[16];
    const int     n   = dgx_ge_pack_masks(body->pack, body->image, masks, 16);
    walk_t        w   = {.body = body, .masks = masks, .n = n, .mask = -1, .g = g, .way_x = way_x, .way_y = way_y};
    dgx_ge_rect_t all = masks_box(body, masks, n, -1);
    if (dgx_ge_rect_empty(all)) return false;
    each_solid(g, all, body->active, push_solid, &w);
    if (!w.found) return false;
    /* back along the way by the farthest any body asks, whole points, never short of it; a step more where the rounding left a point in */
    dgx_ge_body_t moved = *body;
    for (int more = 0; more < 4; ++more) {
        const float t = w.t + (float)more * 0.5f;
        *dx = -(int)ceilf((float)way_x * t - 1e-4f), *dy = -(int)ceilf((float)way_y * t - 1e-4f);
        if (way_x < 0) *dx = (int)ceilf((float)-way_x * t - 1e-4f);
        if (way_y < 0) *dy = (int)ceilf((float)-way_y * t - 1e-4f);
        moved.x = body->x + *dx, moved.y = body->y + *dy;
        walk_t again = {.body = &moved, .masks = masks, .n = n, .mask = -1, .g = g, .way_x = way_x, .way_y = way_y};
        each_solid(g, masks_box(&moved, masks, n, -1), body->active, push_solid, &again);
        if (!again.found) break;
    }
    return true;
}

bool dgx_ge_grid_touches(const dgx_ge_actor_t *actor, int32_t x, int32_t y, dgx_ge_contact_t *contact, void *ctx)
{
    /* asked by the actor's active masks (none: as every one) */
    return dgx_ge_grid_solid_for(ctx, x >> 8, y >> 8, actor && actor->masks ? actor->masks : 0xFFFFu, contact);
}

/* ---- the open space an actor is seen in ---- */

static uint8_t closes_at(const dgx_ge_grid_t *g, int col, int row)
{
    return dgx_ge_tile_closes(g->tileset, tile_at(g, col, row));
}

static bool in_grid(const dgx_ge_grid_t *g, int col, int row)
{
    return col >= 0 && row >= 0 && col < g->cols && row < g->rows;
}

/* Does every cell of a column from row top to row bottom close this side? Only cells of the grid close a side: what is beside it is not drawn here. */
static bool column_closes(const dgx_ge_grid_t *g, int col, int top, int bottom, uint8_t side)
{
    for (int row = top; row <= bottom; ++row)
        if (!in_grid(g, col, row) || !(closes_at(g, col, row) & side)) return false;
    return true;
}

/* Does every cell of a row from column left to column right close this side? */
static bool row_closes(const dgx_ge_grid_t *g, int row, int left, int right, uint8_t side)
{
    for (int col = left; col <= right; ++col)
        if (!(closes_at(g, col, row) & side)) return false;
    return true;
}

/* The opening an actor gone in is seen through: of the cell of its feet, else of a neighbour in its row whose opening reaches into that cell. */
static bool opening_at(const dgx_ge_grid_t *g, int feet_x, int feet_y, dgx_ge_rect_t *hole)
{
    const dgx_ge_tileset_t *set = g->tileset;
    const int               w = set->cell_w, h = set->cell_h;
    int                     fc, fr;
    dgx_ge_grid_cell_at(g, feet_x, feet_y, &fc, &fr);
    for (int k = 0; k < 3; ++k) {
        const int col = fc + (k == 1 ? -1 : k == 2 ? 1 : 0);
        if (!in_grid(g, col, fr)) continue;
        const cell_t cell = cell_of(g, col, fr);
        if (cell.tile < 1 || cell.tile > set->tiles) continue;
        const uint8_t *t = set->table + (cell.tile - 1) * TILE_BYTES;
        if (!(t[6] & CLOSES_OPENING)) continue;
        const uint8_t *r = set->body_table + (u16_at(t + 8) + t[7]) * BODY_BYTES;
        cell_expr_t    e;
        cell_expr(&e, cell);
        if (u16_at(r + 16) != NO_EXPR && !value_of(set, u16_at(r + 16), &e)) continue;
        const int bx = body_value(set, r, 0, &e), by = body_value(set, r, 1, &e), bw = body_value(set, r, 2, &e), bh = body_value(set, r, 3, &e);
        const int left = g->x + col * w + bx, own = g->x + fc * w;
        if (bw <= 0 || bh <= 0 || left >= own + w || left + bw <= own) continue;
        *hole = dgx_ge_rect(left, g->y + (fr + 1) * h - 1 + by - bh + 1, bw, bh);
        return true;
    }
    return false;
}

dgx_ge_rect_t dgx_ge_grid_open_space(const dgx_ge_grid_t *g, dgx_ge_rect_t body, int feet_x, int feet_y, int depth, unsigned sides)
{
    int left = -16384, top = -16384, right = 16382, bottom = 16382; /* the last points seen: a width of 32767 at most */
    if (g->tileset && !dgx_ge_rect_empty(body)) {
        const int w = g->tileset->cell_w, h = g->tileset->cell_h;
        int       fc, fr, bl, bt, br, bb;
        dgx_ge_grid_cell_at(g, feet_x, feet_y, &fc, &fr);
        dgx_ge_grid_cell_at(g, body.x, body.y, &bl, &bt);
        dgx_ge_grid_cell_at(g, body.x + body.w - 1, body.y + body.h - 1, &br, &bb);
        if ((sides & DGX_GE_SOLID_TOP) && bt < fr && row_closes(g, fr - 1, bl, br, DGX_GE_SOLID_BOTTOM)) {
            const int edge = g->y + fr * h;
            if (fr - 1 < 0 || body.y > edge - depth) top = edge;
        }
        if ((sides & DGX_GE_SOLID_BOTTOM) && bb > fr && row_closes(g, fr + 1, bl, br, DGX_GE_SOLID_TOP)) {
            const int edge = g->y + (fr + 1) * h - 1;
            if (fr + 1 >= g->rows || body.y + body.h - 1 < edge + depth) bottom = edge;
        }
        /* across the body from its far side: a frame it is under; else a wall its near side reaches into, past its feet */
        if (sides & DGX_GE_SOLID_RIGHT) {
            int col = fc < bl ? fc : bl; /* a frame it stands in or is under; a wall past the feet ends them there: what it does is the wall's */
            while (col <= br && !(col > fc && in_grid(g, col, fr) && (closes_at(g, col, fr) & DGX_GE_SOLID_LEFT)) &&
                   !(in_grid(g, col, fr) && (closes_at(g, col, fr) & DGX_GE_SOLID_RIGHT)))
                ++col;
            if (col <= br && !(col > fc && (closes_at(g, col, fr) & DGX_GE_SOLID_LEFT))) right = g->x + (col + 1) * w - 1;
            else if (br > fc && column_closes(g, br, bt, fr, DGX_GE_SOLID_LEFT)) right = g->x + br * w - 1;
        }
        if (sides & DGX_GE_SOLID_LEFT) {
            int col = fc > br ? fc : br;
            while (col >= bl && !(col < fc && in_grid(g, col, fr) && (closes_at(g, col, fr) & DGX_GE_SOLID_RIGHT)) &&
                   !(in_grid(g, col, fr) && (closes_at(g, col, fr) & DGX_GE_SOLID_LEFT)))
                --col;
            if (col >= bl && !(col < fc && (closes_at(g, col, fr) & DGX_GE_SOLID_RIGHT))) left = g->x + col * w;
            else if (bl < fc && column_closes(g, bl, bt, fr, DGX_GE_SOLID_RIGHT)) left = g->x + (bl + 1) * w;
        }
    }
    const dgx_ge_rect_t seen = dgx_ge_rect(left, top, right - left + 1, bottom - top + 1);
    dgx_ge_rect_t       hole;
    if (!(sides & DGX_GE_SPACE_INSIDE) || !g->tileset || !opening_at(g, feet_x, feet_y, &hole)) return seen;
    const dgx_ge_rect_t both = dgx_ge_rect_intersect(seen, hole);
    return !dgx_ge_rect_empty(both) ? both : dgx_ge_rect(-16384, -16384, 1, 1); /* nothing of it: a point far off (an empty clip is none) */
}

dgx_ge_rect_t dgx_ge_space_clip(const void *space, dgx_ge_rect_t body, int feet_x, int feet_y, bool inside)
{
    const dgx_ge_space_t *s = space;
    return dgx_ge_grid_open_space(s->grid, body, feet_x, feet_y, s->depth, s->sides | (inside ? DGX_GE_SPACE_INSIDE : 0u));
}

bool dgx_ge_grid_nearest(const dgx_ge_grid_t *g, int x, int y, int radius, dgx_ge_cell_fn fn, void *ctx, int *col, int *row, int *dx, int *dy)
{
    if (!g->tileset || radius < 0) return false;
    const int w = g->tileset->cell_w, h = g->tileset->cell_h;
    int       c0 = floor_div(x - radius - g->x, w), c1 = floor_div(x + radius - g->x, w);
    int       r0 = floor_div(y - radius - g->y, h), r1 = floor_div(y + radius - g->y, h);
    if (c0 < 0) c0 = 0;
    if (r0 < 0) r0 = 0;
    if (c1 >= g->cols) c1 = g->cols - 1;
    if (r1 >= g->rows) r1 = g->rows - 1;
    bool    found   = false;
    int64_t best_d2 = (int64_t)radius * radius;
    for (int r = r0; r <= r1; ++r)
        for (int c = c0; c <= c1; ++c) {
            const int     mx = g->x + c * w + w / 2 - x, my = g->y + r * h + h / 2 - y;
            const int64_t d2 = (int64_t)mx * mx + (int64_t)my * my;
            if (d2 > best_d2 || (found && d2 == best_d2)) continue; /* the first of equal ones: upper, then left */
            const int tile = dgx_ge_grid_cell(g, c, r);
            if (fn ? !fn(tile, dgx_ge_grid_mod(g, c, r), c, r, ctx) : !tile) continue;
            found = true, best_d2 = d2;
            if (col) *col = c;
            if (row) *row = r;
            if (dx) *dx = mx;
            if (dy) *dy = my;
        }
    return found;
}

/* What a look costs through the cells, a point at a time (the cell skip_col, skip_row not counted: one looked at); -1 more than radius or a veil 7. */
static int sight_cost(const dgx_ge_grid_t *g, int x0, int y0, int x1, int y1, int radius, int skip_col, int skip_row)
{
    const dgx_ge_tileset_t *set = g->tileset;
    const int               dx = x1 - x0, dy = y1 - y0, n = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
    if (!n) return 0;
    const float step = sqrtf((float)dx * dx + (float)dy * dy) / (float)n;
    float       cost = 0;
    for (int i = 0; i < n; ++i) {
        /* the middle of the i-th step of the way */
        const int x = x0 + (int)lroundf(((float)i + 0.5f) * (float)dx / (float)n), y = y0 + (int)lroundf(((float)i + 0.5f) * (float)dy / (float)n);
        const int col = floor_div(x - g->x, set->cell_w), row = floor_div(y - g->y, set->cell_h);
        unsigned  veil = 0;
        if (!(col == skip_col && row == skip_row) && (in_grid(g, col, row) || g->outside)) veil = dgx_ge_tile_veil(set, tile_at(g, col, row));
        if (veil >= 7) return -1;
        cost += step * (float)(1 + veil);
        if (cost > (float)radius) return -1;
    }
    return (int)lroundf(cost);
}

int dgx_ge_grid_sight(const dgx_ge_grid_t *g, int x0, int y0, int x1, int y1, int radius)
{
    if (!g->tileset) {
        const float d = sqrtf((float)(x1 - x0) * (x1 - x0) + (float)(y1 - y0) * (y1 - y0));
        return d > (float)radius ? -1 : (int)lroundf(d);
    }
    return sight_cost(g, x0, y0, x1, y1, radius, INT32_MIN, INT32_MIN);
}

int dgx_ge_grid_sight_of(int x0, int y0, int x1, int y1, int radius, void *ctx)
{
    return dgx_ge_grid_sight(ctx, x0, y0, x1, y1, radius);
}

typedef struct {
    const dgx_ge_grid_t  *g;
    const dgx_ge_actor_t *looking;
    const dgx_ge_sense_t *query;
} cell_query_t;

static bool cell_counts(int tile, uint8_t mod, int col, int row, void *ctx)
{
    const cell_query_t   *q = ctx;
    const dgx_ge_sensed_t f = {.col = col, .row = row, .tile = tile, .mod = mod, .cell = row * q->g->cols + col};
    if (!tile) return false;
    /* seen: the look to its middle within the radius, its own veil not counted (the cell itself is what is seen) */
    if (q->query->seen && q->g->tileset) {
        const int w = q->g->tileset->cell_w, h = q->g->tileset->cell_h;
        if (sight_cost(q->g, q->query->x, q->query->y, q->g->x + col * w + w / 2, q->g->y + row * h + h / 2, q->query->radius, col, row) < 0) return false;
    }
    return dgx_ge_actor_sees(q->looking, q->query->where, &f, true);
}

bool dgx_ge_sense_cells(const dgx_ge_grid_t *g, const dgx_ge_actor_t *actor, const dgx_ge_sense_t *query, dgx_ge_sensed_t *found)
{
    cell_query_t q = {g, actor, query};
    int          col, row, dx, dy;
    if (!dgx_ge_grid_nearest(g, query->x, query->y, query->radius, cell_counts, &q, &col, &row, &dx, &dy)) return false;
    memset(found, 0, sizeof(*found));
    found->dx = dx, found->dy = dy, found->col = col, found->row = row;
    found->tile = dgx_ge_grid_cell(g, col, row), found->mod = dgx_ge_grid_mod(g, col, row), found->cell = row * g->cols + col;
    return true;
}
