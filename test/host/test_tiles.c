/* Tiles: a grid of cells laid into the set of a scene, the place of a cell, what is solid, the terrain of a grid. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "dgx_ge_tiles.h"
#include "drivers/vscreen.h"
#include "test_pack.h"

static int at(const dgx_ge_frame_t *f, int x, int y)
{
    return f->canvas.pixels[y * f->canvas.stride + x];
}

static int drawn, first_row = -1;

/* The pieces the grid lays for the cell 1, 1 of a 3 x 3 grid of the tile with rules: which pictures, where. */
static int rules_pieces(const dgx_ge_tileset_t *tiles, dgx_ge_grid_t *g, dgx_ge_scene_t *scene, const dgx_image_t **pics, int *ys)
{
    dgx_ge_scene_reset(scene, 0);
    dgx_ge_grid_add(g, scene);
    for (int i = 0; i < scene->set_count; ++i) {
        pics[i] = scene->set[i].image;
        ys[i]   = scene->set[i].y;
    }
    (void)tiles;
    return scene->set_count;
}

/* the game draws a piece of its own in cell 0, 0; it sees every cell at every step */
static void draw_cell(const dgx_ge_grid_t *g, int col, int row, int step, dgx_ge_scene_t *scene, void *ctx)
{
    (void)ctx, (void)step;
    if (!drawn++) first_row = row;
    if (col || row) return;
    dgx_ge_scene_add(scene, &(dgx_ge_item_t){.image = &g->tileset->images[0], .x = 0, .y = 0, .layer = 1});
}

/* above the grid: slabs, a ceiling of another scene */
static uint16_t slabs_above(const dgx_ge_grid_t *g, int col, int row, uint8_t *mod, uint32_t *variant, void *ctx)
{
    (void)g, (void)col, (void)ctx, (void)mod, (void)variant;
    return row < 0 ? TEST_TILES_TILE_SLAB : 0;
}

static uint16_t above_block(const dgx_ge_grid_t *g, int col, int row, uint8_t *mod, uint32_t *variant, void *ctx)
{
    (void)g, (void)col, (void)ctx, (void)mod, (void)variant;
    return row < 0 ? TEST_TILES_TILE_BLOCK : 0;
}

/* What dgx_ge_grid_touching found. */
static int            touch_count;
static unsigned       touch_bits;
static dgx_ge_touch_t touch_last;
static void           touched(const dgx_ge_touch_t *t, void *ctx)
{
    (void)ctx;
    ++touch_count;
    touch_bits |= t->from;
    touch_last = *t;
}

/* What dgx_ge_grid_overlap found. */
static int           in_count;
static int           in_tile[4];
static dgx_ge_rect_t in_box[4];
static void          inside_of(int col, int row, int tile, dgx_ge_rect_t inside, void *ctx)
{
    (void)col, (void)row, (void)ctx;
    if (in_count < 4) in_tile[in_count] = tile, in_box[in_count] = inside;
    ++in_count;
}

/* Beyond the grid: plates all round. */
static uint16_t plate_outside(const dgx_ge_grid_t *g, int col, int row, uint8_t *mod, uint32_t *variant, void *ctx)
{
    (void)g, (void)col, (void)row, (void)ctx;
    *mod = 0, *variant = 0;
    return TEST_TILES_TILE_PLATE;
}

/* Only blocks count. */
static bool blocks_only(int tile, uint8_t mod, int col, int row, void *ctx)
{
    return tile == TEST_TILES_TILE_BLOCK;
}

/* A sense of cells answered by the grid of ctx. */
static bool sense_grass(const dgx_ge_actor_t *actor, const dgx_ge_sense_t *query, dgx_ge_sensed_t *found, void *ctx)
{
    return query->cells && dgx_ge_sense_cells(ctx, actor, query, found);
}

/* Is the set of a scene the one the grid lays anew (into whole)? */
static bool same_as_laid(const dgx_ge_scene_t *s, const dgx_ge_grid_t *g, dgx_ge_scene_t *whole)
{
    dgx_ge_scene_reset(whole, 0);
    if (!dgx_ge_grid_add(g, whole) || whole->set_count != s->set_count) return false;
    for (int i = 0; i < s->set_count; ++i)
        if (!dgx_ge_item_same(&s->set[i], &whole->set[i])) return false;
    return true;
}

/* What a body touches, by cell and side: how many points, their middle (dgx_ge_grid_touching, or worked out by hand). */
#define TOUCH_COLS 16
#define TOUCH_ROWS 10
typedef struct {
    int points[TOUCH_ROWS][TOUCH_COLS][DGX_GE_TOUCH_SIDES], cx[TOUCH_ROWS][TOUCH_COLS][DGX_GE_TOUCH_SIDES], cy[TOUCH_ROWS][TOUCH_COLS][DGX_GE_TOUCH_SIDES];
    int count;
} touches_t;

static void touch_into(const dgx_ge_touch_t *t, void *ctx)
{
    touches_t *k = ctx;
    int        s = 0;
    while (!(t->from >> s & 1)) ++s;
    k->points[t->row][t->col][s] += t->area.points, k->cx[t->row][t->col][s] = t->area.cx, k->cy[t->row][t->col][s] = t->area.cy;
    ++k->count;
}

/* By hand: every plate of the grid against every other, a mask at a time (the plates of cells are x 0..7, y 4..5 of each). */
static void touches_by_hand(const dgx_ge_grid_t *g, const dgx_ge_body_t *body, int reach, touches_t *k)
{
    dgx_ge_mask_t masks[16];
    dgx_ge_rect_t plates[TOUCH_COLS * TOUCH_ROWS];
    int           n = 0, at[TOUCH_COLS * TOUCH_ROWS];
    for (int row = 0; row < g->rows; ++row)
        for (int col = 0; col < g->cols; ++col)
            if (g->cells[row * g->cols + col]) at[n] = row * TOUCH_COLS + col, plates[n++] = dgx_ge_rect(col * 8, row * 8 + 4, 8, 2);
    const int count = dgx_ge_pack_masks(body->pack, body->image, masks, 16);
    for (int m = 0; m < count; ++m) {
        if (!(body->active >> masks[m].index & 1)) continue;
        const dgx_ge_rect_t place = dgx_ge_mask_place(&masks[m], body->x, body->y, body->flip);
        for (int i = 0; i < n; ++i) {
            dgx_ge_area_t  areas[DGX_GE_TOUCH_SIDES];
            const unsigned bits = dgx_ge_mask_touch_rect_among(&masks[m], place, body->flip, plates[i], reach, plates, n, areas);
            for (int s = 0; s < DGX_GE_TOUCH_SIDES; ++s)
                if (bits >> s & 1) {
                    const int row = at[i] / TOUCH_COLS, col = at[i] % TOUCH_COLS;
                    k->points[row][col][s] += areas[s].points, k->cx[row][col][s] = areas[s].cx, k->cy[row][col][s] = areas[s].cy;
                    ++k->count;
                }
        }
    }
}

int main(void)
{
    size_t   size = 0;
    FILE    *fp   = fopen("build/test.pack", "rb");
    uint8_t *data = NULL;
    if (fp) {
        fseek(fp, 0, SEEK_END);
        size = (size_t)ftell(fp);
        fseek(fp, 0, SEEK_SET);
        data = malloc(size);
        if (fread(data, 1, size, fp) != size) size = 0;
        fclose(fp);
    }
    dgx_ge_pack_t pack;
    CHECK(data && dgx_ge_pack_open(&pack, data, size));
    size_t            n;
    const uint8_t    *blob = dgx_ge_pack_blob(&pack, TEST_BLOB_TILES, &n);
    dgx_ge_tileset_t  tiles;
    CHECK(blob && dgx_ge_tileset_open(&tiles, &pack, blob, n));
    CHECK(tiles.cell_w == 8 && tiles.cell_h == 8 && tiles.tiles == 14 && tiles.slots == 2 && tiles.bodies == 6 && tiles.pieces == 13);
    uint16_t kind;
    uint8_t  solid;
    CHECK(dgx_ge_tile_info(&tiles, TEST_TILES_TILE_FLOOR, &kind, &solid) && kind == 6 && solid == DGX_GE_SOLID_TOP);
    CHECK(!dgx_ge_tile_info(&tiles, 0, &kind, &solid) && !dgx_ge_tile_info(&tiles, 15, &kind, &solid)
);
    /* the tiles draw with pictures of both sets, and there are no others */
    CHECK(dgx_ge_tileset_uses_set(&tiles, TEST_SET_HERO) && dgx_ge_tileset_uses_set(&tiles, TEST_SET_WIDE));
    CHECK(!dgx_ge_tileset_uses_set(&tiles, TEST_SET_COUNT) && !dgx_ge_tileset_uses_set(&tiles, -1));

    /* a frame and the colors of the set of the pictures */
    dgx_screen_t   *display = dgx_vscreen_init(64, 48, 16, DgxScreenRGB);
    dgx_ge_frame_t *frame   = dgx_ge_frame_create(display, 0, 0, 64, 48);
    uint8_t         hero_map[15];
    const uint8_t (*rgb)[3];
    const int       colors = dgx_ge_pack_set_colors(&pack, TEST_SET_HERO, &rgb);
    CHECK(dgx_ge_frame_add_colors(frame, rgb, colors, hero_map));
    const uint8_t *maps[TEST_SET_COUNT] = {[TEST_SET_HERO] = hero_map};

    /* a grid of 4 x 3 at 16, 8: a block, a floor, three floors and a block below */
    uint16_t cells[12] = {
        TEST_TILES_TILE_BLOCK, 0, 0, TEST_TILES_TILE_FLOOR,
        0, 0, 0, 0,
        TEST_TILES_TILE_FLOOR, TEST_TILES_TILE_FLOOR, TEST_TILES_TILE_BLOCK, 0,
    };
    dgx_ge_grid_t grid = {.tileset = &tiles, .maps = maps, .cells = cells, .cols = 4, .rows = 3, .x = 16, .y = 8, .layer_of = {1, 2}, .edge = 2};
    dgx_ge_scene_t *scene = dgx_ge_scene_create(frame, 3, 32, 4);
    CHECK(scene && dgx_ge_grid_add(&grid, scene));
    CHECK(scene->set_count == 1 + 2 + 2 + 2 + 1); /* a block one piece, a floor two */
    dgx_ge_scene_render(scene);
    /* the block of cell 0, 0: FOUR by its origin (0, 1) at the bottom-left point of the cell, 16, 15 */
    CHECK(at(frame, 16, 14) == hero_map[0] && at(frame, 18, 14) == hero_map[2] && at(frame, 16, 15) == hero_map[1] && at(frame, 17, 14) == 0);
    /* the place of a cell: what its pieces cover */
    dgx_ge_rect_t box = dgx_ge_grid_cell_box(&grid, 0, 0, TEST_TILES_TILE_BLOCK);
    CHECK(box.x == 16 && box.y == 14 && box.w == 3 && box.h == 2);
    box = dgx_ge_grid_cell_box(&grid, 3, 0, TEST_TILES_TILE_FLOOR); /* 40, 15: one piece at 42, 9, one at 44, 15 */
    CHECK(box.x == 42 && box.y == 8 && box.w == 5 && box.h == 8);
    CHECK(dgx_ge_grid_cell_box(&grid, 1, 1, 0).w == 0);
    /* a figure: the floor drawn for an actor at 100, 50, its two pieces as actors in their layers, no cells */
    dgx_ge_scene_t *fig = dgx_ge_scene_create(frame, 3, 4, 8);
    dgx_ge_scene_actors_begin(fig);
    CHECK(fig && dgx_ge_grid_figure(&grid, TEST_TILES_TILE_FLOOR, 0, 0, 100, 50, fig) && fig->actor_count == 2 && fig->set_count == 0);
    CHECK(fig->actors[0].x == 102 && fig->actors[0].y == 44 && fig->actors[0].layer == 2); /* step by step, pieces as they are listed */
    CHECK(fig->actors[1].x == 104 && fig->actors[1].y == 50 && fig->actors[1].layer == 1);
    /* by the modifier the actor gives: 1 the second picture; 3 none of the by_mod piece; nothing around, so the lip is drawn */
    dgx_ge_scene_actors_begin(fig);
    CHECK(dgx_ge_grid_figure(&grid, TEST_TILES_TILE_RULES, 1, 0, 100, 50, fig));
    int with_four = 0;
    for (int i = 0; i < fig->actor_count; ++i) with_four += fig->actors[i].image == &tiles.images[1] || fig->actors[i].image->width == 3;
    const int count_mod1 = fig->actor_count;
    dgx_ge_scene_actors_begin(fig);
    CHECK(dgx_ge_grid_figure(&grid, TEST_TILES_TILE_RULES, 3, 0, 100, 50, fig) && fig->actor_count == count_mod1 - 1 && with_four >= 1);
    CHECK(!dgx_ge_grid_figure(&grid, TEST_TILES_TILE_FLOOR, 0, 0, 100, 50, fig) || fig->actor_count <= 4); /* no room past 4 actors */
    dgx_ge_scene_destroy(&fig);

    /* rows from the bottom up, a cell drawn by the game: its pieces instead of those of the tile */
    dgx_ge_scene_reset(scene, 0);
    grid.bottom_up = true;
    grid.draw      = draw_cell;
    drawn          = 0;
    CHECK(dgx_ge_grid_add(&grid, scene) && drawn == 12 && first_row == 2);
    CHECK(scene->set_count == 8 + 1); /* the pieces of the tiles, and the one of the game */
    grid.draw      = NULL;
    grid.bottom_up = false;

    /* cells of points */
    int col, row;
    CHECK(dgx_ge_grid_cell_at(&grid, 47, 15, &col, &row) && col == 3 && row == 0);
    CHECK(!dgx_ge_grid_cell_at(&grid, 15, 10, &col, &row) && col == -1);

    /* the nearest cell by its middle: a floor 2 left 8 down; of blocks the lower one; none within 5; equally near: the upper */
    int ndx, ndy;
    CHECK(dgx_ge_grid_nearest(&grid, 30, 20, 20, NULL, NULL, &col, &row, &ndx, &ndy) && col == 1 && row == 2 && ndx == -2 && ndy == 8);
    CHECK(dgx_ge_grid_nearest(&grid, 30, 20, 20, blocks_only, NULL, &col, &row, &ndx, &ndy) && col == 2 && row == 2 && ndx == 6 && ndy == 8);
    CHECK(!dgx_ge_grid_nearest(&grid, 30, 20, 5, NULL, NULL, &col, &row, NULL, NULL));
    CHECK(dgx_ge_grid_nearest(&grid, 20, 20, 8, NULL, NULL, &col, &row, NULL, NULL) && col == 0 && row == 0);

    /* solid: the block all through, the normal of its nearest side; a floor a band of 2 at its top */
    dgx_ge_contact_t c;
    CHECK(dgx_ge_grid_solid_at(&grid, 17, 8, &c) && c.ny == -1 && c.surface == TEST_TILES_TILE_BLOCK);
    CHECK(dgx_ge_grid_solid_at(&grid, 23, 12, &c) && c.nx == 1 && c.ny == 0);
    CHECK(dgx_ge_grid_solid_at(&grid, 42, 9, &c) && c.ny == -1 && c.surface == TEST_TILES_TILE_FLOOR);
    CHECK(!dgx_ge_grid_solid_at(&grid, 42, 10, &c));  /* below the band of the floor */
    CHECK(!dgx_ge_grid_solid_at(&grid, 30, 10, &c));  /* an empty cell */
    CHECK(!dgx_ge_grid_solid_at(&grid, 100, 10, &c)); /* off the grid */

    /* bodies: a plate in its cell, the side nearest the point; a gate as its modifier lets it down; the cell beyond the grid the game's */
    uint16_t      bcells[2] = {TEST_TILES_TILE_PLATE, TEST_TILES_TILE_GATE};
    uint8_t       bmods[2]  = {0, 0};
    dgx_ge_grid_t bodies    = {.tileset = &tiles, .cells = bcells, .mods = bmods, .cols = 2, .rows = 1};
    CHECK(dgx_ge_grid_solid_at(&bodies, 3, 4, &c) && c.nx == 0 && c.ny == -1 && c.surface == TEST_TILES_TILE_PLATE);
    CHECK(dgx_ge_grid_solid_at(&bodies, 0, 4, &c) && c.ny == -1);                /* the top before the left at a corner */
    CHECK(dgx_ge_grid_solid_at(&bodies, 3, 5, &c) && c.nx == 0 && c.ny == 1);    /* its bottom row: up from below */
    CHECK(!dgx_ge_grid_solid_at(&bodies, 3, 3, &c) && !dgx_ge_grid_solid_at(&bodies, 3, 6, &c)); /* above it, under it */
    CHECK(dgx_ge_grid_solid_at(&bodies, 14, 0, &c) && c.ny == -1 && c.surface == TEST_TILES_TILE_GATE);
    CHECK(dgx_ge_grid_solid_at(&bodies, 14, 4, &c) && c.nx == -1 && c.ny == 0);  /* into it from the left */
    CHECK(dgx_ge_grid_solid_at(&bodies, 15, 4, &c) && c.nx == 1);                /* from the right */
    CHECK(!dgx_ge_grid_solid_at(&bodies, 13, 4, &c));
    bmods[1] = 3; /* raised by 3: 5 high, rows 3..7 */
    CHECK(!dgx_ge_grid_solid_at(&bodies, 14, 2, &c) && dgx_ge_grid_solid_at(&bodies, 14, 3, &c) && c.ny == -1);
    bmods[1] = 6; /* 2 high still, but up enough: none (when) */
    CHECK(!dgx_ge_grid_solid_at(&bodies, 14, 7, &c) && !dgx_ge_grid_solid_at(&bodies, 15, 6, &c));
    bmods[1] = 5;
    CHECK(dgx_ge_grid_solid_at(&bodies, 14, 7, &c));
    CHECK(!dgx_ge_grid_solid_at(&bodies, -5, 4, &c)); /* beyond the grid, nobody says */
    /* plates side by side: at their joint the floor goes on, its top is the surface, not the side of a cell */
    uint16_t      two[2]  = {TEST_TILES_TILE_PLATE, TEST_TILES_TILE_PLATE};
    dgx_ge_grid_t floor2  = {.tileset = &tiles, .cells = two, .cols = 2, .rows = 1};
    CHECK(dgx_ge_grid_solid_at(&floor2, 7, 5, &c) && c.nx == 0 && c.ny == 1);   /* bottom row at the joint: from below */
    CHECK(dgx_ge_grid_solid_at(&floor2, 8, 4, &c) && c.nx == 0 && c.ny == -1);  /* top row at the joint: from above */
    CHECK(dgx_ge_grid_solid_at(&floor2, 15, 4, &c) && c.ny == -1);              /* the end: its top (as near as its side) */
    CHECK(dgx_ge_grid_solid_at(&bodies, 7, 5, &c) && c.nx == 1 && c.ny == 0);   /* plate, then a gate cell without a floor: its end */

    bodies.outside = plate_outside;
    CHECK(dgx_ge_grid_solid_at(&bodies, -5, 4, &c) && c.surface == TEST_TILES_TILE_PLATE && !dgx_ge_grid_solid_at(&bodies, -5, 2, &c));

    /* a body of masks in the bodies of tiles: the box of its points inside each; only its active masks */
    bmods[1]           = 0;
    bodies.outside     = NULL;
    dgx_ge_body_t four = {&pack, TEST_IMG_FOUR, 2, 5, false, 1u << 1}; /* its body mask: points (2,4) (4,4) (2,5) (3,5) */
    in_count = 0;
    CHECK(dgx_ge_grid_overlap(&bodies, &four, inside_of, NULL) == 1 && in_count == 1 && in_tile[0] == TEST_TILES_TILE_PLATE);
    CHECK(in_box[0].x == 2 && in_box[0].y == 4 && in_box[0].w == 3 && in_box[0].h == 2);
    four.y = 4; /* one row higher: only its lower row in the plate */
    in_count = 0;
    CHECK(dgx_ge_grid_overlap(&bodies, &four, inside_of, NULL) == 1 && in_box[0].y == 4 && in_box[0].h == 1 && in_box[0].x == 2 && in_box[0].w == 2);
    four.active = 1u << 2; /* the blade's mask: a point at (3..4, ...) beyond the picture */
    four.y      = 5;
    in_count    = 0;
    const int blade_in = dgx_ge_grid_overlap(&bodies, &four, inside_of, NULL);
    four.active = 0;
    CHECK(dgx_ge_grid_overlap(&bodies, &four, inside_of, NULL) == 0 && blade_in >= 0);
    four = (dgx_ge_body_t){&pack, TEST_IMG_FOUR, 12, 3, false, 1u << 1}; /* points (12,2) (14,2) (12,3) (13,3): the gate strip is x 14..15 */
    in_count = 0;
    CHECK(dgx_ge_grid_overlap(&bodies, &four, inside_of, NULL) == 1 && in_tile[0] == TEST_TILES_TILE_GATE && in_box[0].x == 14 && in_box[0].w == 1);
    four.flip = true; /* mirrored about its origin: points (10..12): nothing in the strip */
    CHECK(dgx_ge_grid_overlap(&bodies, &four, inside_of, NULL) == 0);
    four = (dgx_ge_body_t){&pack, TEST_IMG_FOUR, 14, 7, false, 1u << 1}; /* in the lowest rows of the strip */
    bmods[1] = 6; /* 2 high, but up enough: no body (when) */
    CHECK(dgx_ge_grid_overlap(&bodies, &four, inside_of, NULL) == 0);
    bmods[1] = 5;
    CHECK(dgx_ge_grid_overlap(&bodies, &four, inside_of, NULL) == 1);
    bmods[1] = 0;

    /* a body reaching into the next cell: the shelf of cell 0 (x 4..11, y 0..1) is solid in cell 1 too, for a point and for a body */
    uint16_t      scells[3] = {TEST_TILES_TILE_SHELF, 0, 0};
    dgx_ge_grid_t shelf     = {.tileset = &tiles, .cells = scells, .cols = 3, .rows = 1};
    CHECK(dgx_ge_grid_solid_at(&shelf, 10, 0, &c) && c.surface == TEST_TILES_TILE_SHELF && c.ny == -1); /* in cell 1, on the shelf reaching there */
    CHECK(!dgx_ge_grid_solid_at(&shelf, 12, 1, &c) && !dgx_ge_grid_solid_at(&shelf, 3, 1, &c) && !dgx_ge_grid_solid_at(&shelf, 10, 2, &c));
    four = (dgx_ge_body_t){&pack, TEST_IMG_FOUR, 10, 0, false, 1u << 1}; /* points (10,-1) (12,-1) (10,0) (11,0): the lower row in the shelf, in cell 1 */
    CHECK(dgx_ge_grid_touch_sides(&shelf, &four, 1, 0) == DGX_GE_TOUCH_INSIDE);
    in_count = 0;
    CHECK(dgx_ge_grid_overlap(&shelf, &four, inside_of, NULL) == 1 && in_tile[0] == TEST_TILES_TILE_SHELF && in_box[0].x == 10 && in_box[0].w == 2);

    /* what a body touches: standing on the plate (its top row y 4): the lower row of the mask (y 3) touches it from below */
    four = (dgx_ge_body_t){&pack, TEST_IMG_FOUR, 2, 3, false, 1u << 1}; /* points (2,2) (4,2) (2,3) (3,3) */
    touch_count = 0, touch_bits = 0;
    CHECK(dgx_ge_grid_touching(&bodies, &four, -1, 1, touched, NULL) == 1 && touch_count == 1 && touch_bits == DGX_GE_TOUCH_BELOW);
    CHECK(touch_last.mask == 1 && touch_last.area.points == 2 && touch_last.area.box.x == 2 && touch_last.area.box.y == 3 && touch_last.area.box.w == 2);
    CHECK(touch_last.handle == -1 && touch_last.col == 0 && touch_last.row == 0 && touch_last.tile == TEST_TILES_TILE_PLATE && touch_last.kind == 12);
    CHECK(dgx_ge_grid_touch_sides(&bodies, &four, 1, 1) == DGX_GE_TOUCH_BELOW && dgx_ge_grid_touch_sides(&bodies, &four, 2, 1) == 0);
    four.y = 2; /* a point higher: out of reach 1, within 2 */
    CHECK(dgx_ge_grid_touch_sides(&bodies, &four, 1, 1) == 0 && dgx_ge_grid_touch_sides(&bodies, &four, 1, 2) == DGX_GE_TOUCH_BELOW);
    four.y = 4; /* its lower row in the plate, its upper row over it */
    CHECK(dgx_ge_grid_touch_sides(&bodies, &four, -1, 1) == (DGX_GE_TOUCH_INSIDE | DGX_GE_TOUCH_BELOW));
    four.active = 0;
    CHECK(dgx_ge_grid_touch_sides(&bodies, &four, -1, 1) == 0); /* no active mask: nothing */

    /* bodies for some masks: the plate of the ward holds mask 2 only, its left half every mask */
    uint16_t      wcells[1] = {TEST_TILES_TILE_WARD};
    dgx_ge_grid_t ward      = {.tileset = &tiles, .cells = wcells, .cols = 1, .rows = 1};
    CHECK(dgx_ge_grid_solid_at(&ward, 6, 4, &c) && c.ny == -1);                      /* asked by no mask: every body */
    CHECK(dgx_ge_grid_solid_for(&ward, 6, 4, 1u << 2, &c) && c.ny == -1);            /* mask 2: the plate */
    CHECK(!dgx_ge_grid_solid_for(&ward, 6, 4, 1u << 1, &c));                          /* mask 1: not its */
    CHECK(dgx_ge_grid_solid_for(&ward, 2, 4, 1u << 1, &c) && c.ny == -1);            /* the left half holds every mask */
    four = (dgx_ge_body_t){&pack, TEST_IMG_FOUR, 5, 3, false, 1u << 1};               /* points (5,2) (7,2) (5,3) (6,3): over the right half */
    CHECK(dgx_ge_grid_touch_sides(&ward, &four, 1, 1) == 0 && dgx_ge_grid_touch_sides(&ward, &four, -1, 1) == 0);
    four.x = 1;                                                                       /* over the left half: mask 1 stands on it */
    CHECK(dgx_ge_grid_touch_sides(&ward, &four, 1, 1) == DGX_GE_TOUCH_BELOW);
    /* two plates side by side: their seam (x 7 | 8) is no edge; feet in both, over both, touch inside and below, no side */
    uint16_t      seam_cells[2] = {TEST_TILES_TILE_PLATE, TEST_TILES_TILE_PLATE};
    dgx_ge_grid_t seam          = {.tileset = &tiles, .cells = seam_cells, .cols = 2, .rows = 1};
    four = (dgx_ge_body_t){&pack, TEST_IMG_FOUR, 7, 4, false, 1u << 1}; /* points (7,3) (9,3) over them, (7,4) in the left one, (8,4) in the right */
    CHECK(dgx_ge_grid_touch_sides(&seam, &four, 1, 1) == (DGX_GE_TOUCH_INSIDE | DGX_GE_TOUCH_BELOW));
    four.y = 3; /* a row up: over the seam, touching below only */
    CHECK(dgx_ge_grid_touch_sides(&seam, &four, 1, 1) == DGX_GE_TOUCH_BELOW);
    /* before the strip of the gate (x 14..15): the point (13,2) has it on its right */
    four = (dgx_ge_body_t){&pack, TEST_IMG_FOUR, 11, 3, false, 1u << 1}; /* points (11,2) (13,2) (11,3) (12,3) */
    CHECK(dgx_ge_grid_touch_sides(&bodies, &four, 1, 1) == DGX_GE_TOUCH_RIGHT && dgx_ge_grid_touch_sides(&bodies, &four, -1, 1) == DGX_GE_TOUCH_RIGHT);
    int px, py;
    CHECK(!dgx_ge_grid_push_out(&bodies, &four, 1, 0, &px, &py) && px == 0 && py == 0);
    four.x = 13; /* its points (15,2) (14,3) in the strip: 2 back to the left going right; going down-left, back up-right 3: at 2 its (13,3) is at (15,1), in the strip still */
    CHECK(dgx_ge_grid_push_out(&bodies, &four, 1, 0, &px, &py) && px == -2 && py == 0);
    CHECK(dgx_ge_grid_push_out(&bodies, &four, -1, 1, &px, &py) && px == 3 && py == -3);
    CHECK(dgx_ge_grid_push_out(&bodies, &four, 0, -1, &px, &py) && px == 0 && py == 8 - 2); /* up out of the strip 8 high from its row 2 */
    CHECK(dgx_ge_grid_push_out(&bodies, &four, 3, 1, &px, &py) && px == -2 && py == -1);  /* a slant: 2 back along x, the y with it, whole points */
    CHECK(!dgx_ge_grid_push_out(&bodies, &four, 0, 0, &px, &py) && px == 0 && py == 0);  /* no way given: no hint */
    /* out of one body into another: on, out of every one; points (8,4) (10,4) (8,5) (9,5) in the right plate going right, 3 back are in the left one */
    four = (dgx_ge_body_t){&pack, TEST_IMG_FOUR, 8, 5, false, 1u << 1};
    CHECK(dgx_ge_grid_push_out(&seam, &four, 1, 0, &px, &py) && px == -11 && py == 0);
    dgx_ge_body_t out = four;
    out.x += px;
    in_count = 0;
    CHECK(dgx_ge_grid_overlap(&seam, &out, inside_of, NULL) == 0);
    /* plates on and on beyond the grid: no way out, no hint */
    seam.outside = plate_outside;
    CHECK(!dgx_ge_grid_push_out(&seam, &four, 1, 0, &px, &py) && px == 0 && py == 0);
    seam.outside = NULL;

    /* the terrain of the grid: a ray down onto the floor of cell 3, 0 */
    const dgx_ge_terrain_t terrain = {.touches = dgx_ge_grid_touches, .ctx = &grid};
    CHECK(dgx_ge_terrain_ray(&terrain, 44 * 256, 0, 44 * 256, 40 * 256, 4, &c) && c.y == 8 * 256 && c.ny == -1);
    /* across the empty middle row nothing */
    CHECK(!dgx_ge_terrain_ray(&terrain, 16 * 256, 20 * 256, 60 * 256, 20 * 256, 2, &c));

    /*
     * The rules: the tile at 1, 1 of a grid 3 x 3, its pieces in order: a lip
     * where above is empty, a picture by the modifier, water going round, a
     * decoration of variant 1, a gate raised by mod / 4 where the left is not
     * a block and below is a block or a floor.
     */
    uint16_t       rcells[9] = {0, 0, 0, 0, TEST_TILES_TILE_RULES, 0, 0, TEST_TILES_TILE_FLOOR, 0};
    uint8_t        rmods[9]  = {0};
    uint32_t       rvariants[9] = {0};
    dgx_ge_grid_t  rg = {.tileset = &tiles, .cells = rcells, .mods = rmods, .variants = rvariants, .cols = 3, .rows = 3, .layer_of = {1, 2}};
    const dgx_image_t *pics[16];
    int                ys[16];
    /* the pictures of the tiles each once: ONE, FOUR, EIGHT (none of by_mod is no picture) */
    CHECK(tiles.distinct == 3 && tiles.pictures > 3);
    /* pictures of the pack by number, to tell them */
    dgx_image_t img_one, img_four, img_eight;
    dgx_ge_pack_image(&pack, TEST_IMG_ONE, &img_one);
    dgx_ge_pack_image(&pack, TEST_IMG_FOUR, &img_four);
    dgx_ge_pack_image(&pack, TEST_IMG_EIGHT, &img_eight);
#define SAME(p, im) ((p)->data == (im).data)
    /* above empty, mod 0, time 0, variant 0, below a floor: lip, by_mod ONE, cycle ONE, the gate at dy 0; then the two of the floor */
    CHECK(rules_pieces(&tiles, &rg, scene, pics, ys) == 4 + 2);
    CHECK(SAME(pics[0], img_eight) && ys[0] == 15 - 7 && SAME(pics[1], img_one) && SAME(pics[2], img_one) && SAME(pics[3], img_four) && ys[3] == 15);
    /* mod 8: by_mod past its end takes its last, none here: no piece; the gate 2 up; time 150: the cycle shows FOUR; variant 1: the decoration */
    rmods[4] = 8, rvariants[4] = 1, rg.now_ms = 150;
    CHECK(rules_pieces(&tiles, &rg, scene, pics, ys) == 4 + 2);
    CHECK(SAME(pics[1], img_four) && SAME(pics[2], img_eight) && SAME(pics[3], img_four) && ys[3] == 15 - 2);
    rmods[4] = 2; /* the third of by_mod: EIGHT */
    CHECK(rules_pieces(&tiles, &rg, scene, pics, ys) == 5 + 2 && SAME(pics[1], img_eight));
    rmods[4] = 8;
    rmods[4] = 1, rg.now_ms = 450; /* round of 400: 50 into it, ONE again; mod 1: FOUR */
    CHECK(rules_pieces(&tiles, &rg, scene, pics, ys) == 5 + 2 && SAME(pics[1], img_four) && SAME(pics[2], img_one));
    rmods[4] = 3; /* the fourth of by_mod is none: no piece in this state */
    CHECK(rules_pieces(&tiles, &rg, scene, pics, ys) == 4 + 2 && SAME(pics[1], img_one));
    /* above a block: no lip; left a block: no gate; below empty: no gate either */
    rcells[1] = TEST_TILES_TILE_BLOCK, rcells[3] = TEST_TILES_TILE_BLOCK, rvariants[4] = 0, rmods[4] = 0;
    CHECK(rules_pieces(&tiles, &rg, scene, pics, ys) == 2 + 2 + 2); /* the blocks above and left, by_mod, cycle, the floor */
    rcells[3] = 0, rcells[7] = 0;
    CHECK(rules_pieces(&tiles, &rg, scene, pics, ys) == 1 + 2); /* the block above, by_mod, cycle: below nothing, no gate */
    /* beyond the grid: the game tells; a block above the top row hides the lip of a tile there */
    uint16_t top[3] = {0, TEST_TILES_TILE_RULES, 0};
    dgx_ge_grid_t edge = {.tileset = &tiles, .cells = top, .cols = 3, .rows = 1, .layer_of = {1, 2}};
    CHECK(rules_pieces(&tiles, &edge, scene, pics, ys) == 3); /* lip, by_mod, cycle (no gate: below is beyond, empty) */
    edge.outside = above_block;
    CHECK(rules_pieces(&tiles, &edge, scene, pics, ys) == 2);
    /* the time moves: the cycle of the one tile shows another picture from 100 on, then not till 400 */
    rcells[1] = rcells[3] = 0, rg.now_ms = 0;
    dgx_ge_rect_t places[4];
    CHECK(dgx_ge_grid_animate(&rg, 50, places, 4) == 0);
    CHECK(dgx_ge_grid_animate(&rg, 150, places, 4) == 1 && places[0].w > 0 && rg.now_ms == 150);
    CHECK(dgx_ge_grid_animate(&rg, 390, places, 4) == 0);
    CHECK(dgx_ge_grid_animate(&rg, 410, places, 4) == 1);

    /*
     * Laid with another cell, in steps: the tile EDGE has a body (step 1) and
     * an edge laid with the cell on its right at step 0, before that cell's
     * own body; drawn as a card of the paper, and a silhouette in color 2.
     */
    uint16_t       ecells[2] = {TEST_TILES_TILE_EDGE, TEST_TILES_TILE_EDGE};
    dgx_ge_grid_t  eg = {.tileset = &tiles, .maps = maps, .cells = ecells, .cols = 2, .rows = 1, .steps = 2, .layer_of = {1, 2}};
    dgx_ge_scene_reset(scene, 0);
    CHECK(dgx_ge_grid_add(&eg, scene) && scene->set_count == 3);
    /* cell 0: its body; cell 1: the edge of cell 0 first, then its own body; the edge of cell 1 has no cell to its right */
    CHECK(scene->set[0].x == 0 && scene->set[1].x == 8 + 1 && scene->set[2].x == 8);
    CHECK(scene->set[1].flags == DGX_GE_ITEM_CARD && scene->set[1].color == 0);
    CHECK(scene->set[0].flags == DGX_GE_BLIT_SILHOUETTE && scene->set[0].color == hero_map[1]);
    /* what a change of cell 0 may draw anew: its pieces and those its neighbours' rules may change */
    const dgx_ge_rect_t change = dgx_ge_grid_change_box(&eg, 0, 0);
    CHECK(change.x == 0 && change.x + change.w >= 8 + 1 + 3);

    /* decoration by fields of the variant: mark 2 picks FOUR, moved by 5; the fill in color 3 of hero, at the bottom-left of the cell */
    uint16_t      dcells[2]    = {TEST_TILES_TILE_DECOR, TEST_TILES_TILE_DECOR};
    uint32_t      dvariants[2] = {2 << 4 | 5, 0};
    dgx_ge_grid_t dg = {.tileset = &tiles, .maps = maps, .cells = dcells, .variants = dvariants, .cols = 2, .rows = 1, .layer_of = {1, 2}};
    dgx_ge_scene_reset(scene, 0);
    CHECK(dgx_ge_grid_add(&dg, scene) && scene->set_count == 4);
    CHECK(SAME(scene->set[0].image, img_four) && scene->set[0].x == 5 && scene->set[0].y == 7);
    CHECK(scene->set[1].flags == DGX_GE_ITEM_FILL && scene->set[1].clip.x == 1 && scene->set[1].clip.y == 6 && scene->set[1].clip.w == 3 &&
          scene->set[1].clip.h == 2 && scene->set[1].color == hero_map[2]);
    /* when mark >= 2 and not shift == 3: EIGHT in cell 0 only */
    CHECK(SAME(scene->set[2].image, img_eight) && scene->set[2].x == 0);
    /* cell 1, variant 0: no mark; the fill in color 1 */
    CHECK(scene->set[3].flags == DGX_GE_ITEM_FILL && scene->set[3].clip.x == 8 + 1 && scene->set[3].color == hero_map[0]);
    const dgx_ge_rect_t fill_box = dgx_ge_grid_cell_box(&dg, 1, 0, TEST_TILES_TILE_DECOR);
    CHECK(fill_box.x == 9 && fill_box.w == 3 && fill_box.h == 2);

    /*
     * A ring: a window of 3 x 2 onto a world without end. World row 0 has a
     * block at column 0, row 1 a floor at column 1; moved down a row, row 1
     * leads the window, row 2 comes in where row 0 was, and every cell stays at
     * its place in the scene.
     */
    uint16_t      ring_cells[6] = {TEST_TILES_TILE_BLOCK, 0, 0, 0, TEST_TILES_TILE_FLOOR, 0};
    dgx_ge_grid_t ring          = {.tileset = &tiles, .cells = ring_cells, .cols = 3, .rows = 2, .layer_of = {1, 2}, .ring = true};
    CHECK(dgx_ge_grid_cell(&ring, 1, 1) == TEST_TILES_TILE_FLOOR && dgx_ge_grid_index(&ring, 0, 1) == 3 && dgx_ge_grid_index(&ring, 3, 0) == -1);
    dgx_ge_scene_reset(scene, 0);
    CHECK(dgx_ge_grid_add(&ring, scene) && scene->set_count == 3);
    CHECK(scene->set[0].x == 0 && scene->set[0].y == 7 && scene->set[1].x == 8 + 2 && scene->set[1].y == 15 - 6);
    dgx_ge_grid_scroll(&ring, 0, 1);
    CHECK(ring.first_row == 1 && ring.ring_row == 1 && ring.y == 8 && dgx_ge_grid_index(&ring, 0, 0) == 3 && dgx_ge_grid_index(&ring, 0, 1) == 0);
    CHECK(dgx_ge_grid_cell(&ring, 1, 0) == TEST_TILES_TILE_FLOOR && dgx_ge_grid_cell(&ring, 0, 1) == TEST_TILES_TILE_BLOCK); /* not yet written */
    for (int c = 0; c < 3; ++c) ring_cells[dgx_ge_grid_index(&ring, c, 1)] = c == 2 ? TEST_TILES_TILE_BLOCK : 0;
    dgx_ge_scene_reset(scene, 0);
    CHECK(dgx_ge_grid_add(&ring, scene) && scene->set_count == 3);
    CHECK(scene->set[0].x == 8 + 2 && scene->set[0].y == 15 - 6 && scene->set[2].x == 16 && scene->set[2].y == 3 * 8 - 1);
    int rc, rr;
    CHECK(dgx_ge_grid_cell_at(&ring, 17, 20, &rc, &rr) && rc == 2 && rr == 1 && dgx_ge_grid_solid_at(&ring, 17, 20, NULL));
    /* back past the start of the world */
    dgx_ge_grid_scroll(&ring, 0, -3);
    CHECK(ring.first_row == (uint32_t)-2 && ring.y == -16 && dgx_ge_grid_index(&ring, 0, 0) == 0 && dgx_ge_grid_index(&ring, 0, 1) == 3);
    dgx_ge_grid_scroll(&ring, -4, 0);
    CHECK(ring.first_col == (uint32_t)-4 && ring.x == -32 && dgx_ge_grid_index(&ring, 0, 0) == 2 && dgx_ge_grid_index(&ring, 1, 0) == 0);
    /* the numbers of the world go round past 2^32 and the window goes on as before: 3 columns do not divide 2^32 */
    ring.first_col = 0xFFFFFFFFu;
    const int before_wrap = dgx_ge_grid_index(&ring, 1, 0);
    dgx_ge_grid_scroll(&ring, 1, 0);
    CHECK(ring.first_col == 0 && dgx_ge_grid_index(&ring, 0, 0) == before_wrap);
    dgx_ge_grid_scroll(&ring, -100000, 7);
    CHECK(ring.ring_col == (2 + 1 - 100000 % 3 + 3) % 3 && ring.ring_row == (0 + 7) % 2);

    /*
     * The open space a body is seen in. 4 x 3 cells of 8:
     *   row 0: slab slab slab .
     *   row 1: .    frame .   block
     *   row 2: floor floor floor floor
     * The sides closed: a slab its bottom, a frame its right, a block all, a
     * floor its top (as it is solid).
     */
    CHECK(dgx_ge_tile_closes(&tiles, TEST_TILES_TILE_SLAB) == DGX_GE_SOLID_BOTTOM && dgx_ge_tile_closes(&tiles, TEST_TILES_TILE_FLOOR) == DGX_GE_SOLID_TOP &&
          dgx_ge_tile_closes(&tiles, TEST_TILES_TILE_BLOCK) == DGX_GE_SOLID_ALL);
    /* the veil of a look (sense ... seen): a thicket 3, a hedge 7, the rest none; it closes nothing for the open space */
    CHECK(dgx_ge_tile_veil(&tiles, TEST_TILES_TILE_THICKET) == 3 && dgx_ge_tile_veil(&tiles, TEST_TILES_TILE_HEDGE) == 7);
    CHECK(dgx_ge_tile_veil(&tiles, TEST_TILES_TILE_BLOCK) == 0 && dgx_ge_tile_veil(&tiles, 0) == 0 && dgx_ge_tile_closes(&tiles, TEST_TILES_TILE_THICKET) == 0);
    {
        /* a row of 10 cells of 8: open, then a thicket at column 4 */
        uint16_t      row[10] = {0, 0, 0, 0, TEST_TILES_TILE_THICKET};
        dgx_ge_grid_t sight   = {.tileset = &tiles, .cells = row, .cols = 10, .rows = 1};
        CHECK(dgx_ge_grid_sight(&sight, 2, 4, 30, 4, 100) == 28);   /* in the open: the distance */
        CHECK(dgx_ge_grid_sight(&sight, 2, 4, 30, 4, 20) == -1);    /* beyond the radius */
        CHECK(dgx_ge_grid_sight(&sight, 2, 4, 60, 4, 200) == 82);   /* 58 points, 8 of them in the thicket at 4: 50 + 32 */
        CHECK(dgx_ge_grid_sight(&sight, 2, 4, 60, 4, 81) == -1);
        CHECK(dgx_ge_grid_sight(&sight, 6, 4, 6, 4, 10) == 0);
        row[4] = TEST_TILES_TILE_HEDGE;
        CHECK(dgx_ge_grid_sight(&sight, 2, 4, 60, 4, 1000) == -1);  /* nothing seen through a hedge */
        CHECK(dgx_ge_grid_sight(&sight, 2, 4, 30, 4, 100) == 28);   /* this side of it, as before */
        CHECK(dgx_ge_grid_sight_of(2, 4, 30, 4, 100, &sight) == 28);
    }
    CHECK(dgx_ge_tile_info(&tiles, TEST_TILES_TILE_FRAME, &kind, &solid) && solid == 0); /* closed for sight, not solid */
    uint16_t S = TEST_TILES_TILE_SLAB, F = TEST_TILES_TILE_FLOOR;
    uint16_t space_cells[12] = {S, S, S, 0, 0, TEST_TILES_TILE_FRAME, 0, TEST_TILES_TILE_BLOCK, F, F, F, F};
    dgx_ge_grid_t sg = {.tileset = &tiles, .cells = space_cells, .cols = 4, .rows = 3};
    const unsigned all = DGX_GE_SOLID_ALL;
    /* a head under the slabs, 4 into them: cut at the top of its row when it may go 6 deep, not when 3 */
    dgx_ge_rect_t seen = dgx_ge_grid_open_space(&sg, dgx_ge_rect(2, 4, 8, 12), 6, 15, 6, all);
    CHECK(seen.y == 8 && seen.x < 2 && seen.x + seen.w > 10);
    CHECK(dgx_ge_grid_open_space(&sg, dgx_ge_rect(2, 4, 8, 12), 6, 15, 3, all).y < 4);
    CHECK(dgx_ge_grid_open_space(&sg, dgx_ge_rect(2, 4, 8, 12), 6, 15, 6, all & ~DGX_GE_SOLID_TOP).y < 4); /* not a side to cut */
    /* wider, over the open cell 3 of row 0: not cut */
    CHECK(dgx_ge_grid_open_space(&sg, dgx_ge_rect(10, 4, 20, 12), 12, 15, 6, DGX_GE_SOLID_TOP).y < 4);
    /* right: the frame in cell 1 cuts at its right edge */
    seen = dgx_ge_grid_open_space(&sg, dgx_ge_rect(2, 9, 26, 7), 4, 15, 6, all);
    CHECK(seen.x + seen.w - 1 == 15);
    /* without the frame, the block in cell 3 at its left edge, when it closes the whole height of the body */
    space_cells[5] = 0;
    seen = dgx_ge_grid_open_space(&sg, dgx_ge_rect(2, 9, 26, 7), 4, 15, 6, all);
    CHECK(seen.x + seen.w - 1 == 23);
    seen = dgx_ge_grid_open_space(&sg, dgx_ge_rect(2, 4, 26, 12), 4, 15, 0, DGX_GE_SOLID_RIGHT); /* reaching into row 0, open over the block */
    CHECK(seen.x + seen.w > 28);
    space_cells[5] = TEST_TILES_TILE_FRAME;
    /* left: from cell 2, reaching into the frame of cell 1, which closes its right as a wall would: cut after it; not reaching it, not */
    seen = dgx_ge_grid_open_space(&sg, dgx_ge_rect(12, 9, 10, 7), 20, 15, 6, all);
    CHECK(seen.x == 16);
    CHECK(dgx_ge_grid_open_space(&sg, dgx_ge_rect(17, 9, 5, 7), 20, 15, 6, all).x < 0);
    /* a wall the body passes over is no wall it reaches into: only the cell of its far edge counts (here open) */
    space_cells[5] = TEST_TILES_TILE_BLOCK, space_cells[6] = 0;
    seen = dgx_ge_grid_open_space(&sg, dgx_ge_rect(2, 9, 18, 7), 4, 15, 6, DGX_GE_SOLID_RIGHT);
    CHECK(seen.x + seen.w - 1 > 20);
    space_cells[5] = TEST_TILES_TILE_FRAME;
    /* below: feet in row 1 and the floors of row 2, 4 into them: cut at the bottom of row 1 when 6 deep is allowed */
    seen = dgx_ge_grid_open_space(&sg, dgx_ge_rect(17, 9, 4, 11), 19, 15, 6, DGX_GE_SOLID_BOTTOM);
    CHECK(seen.y + seen.h - 1 == 15);
    seen = dgx_ge_grid_open_space(&sg, dgx_ge_rect(17, 9, 4, 11), 19, 15, 3, DGX_GE_SOLID_BOTTOM);
    CHECK(seen.y + seen.h - 1 > 20);
    /* beyond the top of the grid: slabs of another scene cut at the top, however deep */
    sg.outside = slabs_above;
    space_cells[3] = S;
    CHECK(dgx_ge_grid_open_space(&sg, dgx_ge_rect(2, -6, 8, 12), 6, 5, 0, DGX_GE_SOLID_TOP).y == 0);
    /* for a scene: the same through dgx_ge_space_clip */
    const dgx_ge_space_t space = {&sg, 6, DGX_GE_SOLID_TOP};
    CHECK(dgx_ge_space_clip(&space, dgx_ge_rect(2, 4, 8, 12), 6, 15, false).y == 8);
    /*
     * An opening: a door in row 1, cell 0, its opening 12 wide (into cell 1 by 4), 6 - mod high from the bottom of
     * the row (15). Gone into it, a body is seen only through it as well: its feet in cell 0, or in cell 1 the opening
     * reaches into; in cell 2, none near: as before; not gone in: as before. The door closes nothing for sight.
     */
    space_cells[3] = 0, space_cells[4] = TEST_TILES_TILE_DOOR;
    CHECK(dgx_ge_tile_closes(&tiles, TEST_TILES_TILE_DOOR) == 0);
    const unsigned in = all | DGX_GE_SPACE_INSIDE;
    seen = dgx_ge_grid_open_space(&sg, dgx_ge_rect(2, 4, 8, 12), 6, 15, 6, in);
    CHECK(seen.x == 0 && seen.y == 10 && seen.x + seen.w - 1 == 11 && seen.y + seen.h - 1 == 15);
    seen = dgx_ge_grid_open_space(&sg, dgx_ge_rect(8, 4, 8, 12), 12, 15, 6, in);
    CHECK(seen.y == 10 && seen.x + seen.w - 1 == 11);
    CHECK(dgx_ge_grid_open_space(&sg, dgx_ge_rect(16, 4, 8, 12), 20, 15, 6, in).y == 8);
    CHECK(dgx_ge_grid_open_space(&sg, dgx_ge_rect(2, 4, 8, 12), 6, 15, 6, all).y == 8);
    /* its height by its modifier: 2 lowers its top by 2 */
    uint8_t space_mods[12] = {0};
    space_mods[4] = 2, sg.mods = space_mods;
    CHECK(dgx_ge_grid_open_space(&sg, dgx_ge_rect(2, 4, 8, 12), 6, 15, 6, in).y == 12);
    /* for a scene: dgx_ge_space_clip told it has gone in */
    CHECK(dgx_ge_space_clip(&space, dgx_ge_rect(2, 4, 8, 12), 6, 15, true).y == 12);
    sg.mods = NULL, space_cells[4] = 0;

    /*
     * The same look in two states: rules over a block (its gate shown: left not a block, a block under it). By its
     * modifier 0 one picture, 1 another; 4 and 5 none of those and the gate at the same height (-1, -1.25 round to -1);
     * 4 and 8 the gate higher; another tile never the same; a state the same as itself always.
     */
    {
        uint16_t      two[2] = {3, 1};
        dgx_ge_grid_t g      = {.tileset = &tiles, .cells = two, .cols = 1, .rows = 2, .layer_of = {1, 2}};
        CHECK(dgx_ge_grid_same_look(&g, 0, 0, 3, 7, 3, 7));
        CHECK(!dgx_ge_grid_same_look(&g, 0, 0, 3, 0, 3, 1));
        CHECK(dgx_ge_grid_same_look(&g, 0, 0, 3, 4, 3, 5));
        CHECK(!dgx_ge_grid_same_look(&g, 0, 0, 3, 4, 3, 8));
        CHECK(!dgx_ge_grid_same_look(&g, 0, 0, 3, 4, 2, 4));
        two[1] = 0; /* nothing under it: no gate, 4 and 8 alike */
        CHECK(dgx_ge_grid_same_look(&g, 0, 0, 3, 4, 3, 8));
    }
    /* sense of cells: the nearest grass grown 2 or more of a row of three (grown 1, a block, grown 3): the third, 20 forward, number 2 */
    {
        size_t         hn;
        const uint8_t *hb = dgx_ge_pack_blob(&pack, TEST_BLOB_HERO, &hn);
        dgx_ge_anim_t  hero;
        CHECK(hb && dgx_ge_anim_open(&hero, hb, hn));
        uint16_t               gcells[3] = {TEST_TILES_TILE_FLOOR, TEST_TILES_TILE_BLOCK, TEST_TILES_TILE_FLOOR};
        uint8_t                gmods[3]  = {1, 0, 3};
        dgx_ge_grid_t          field     = {.tileset = &tiles, .cells = gcells, .mods = gmods, .cols = 3, .rows = 1};
        const dgx_ge_terrain_t eyes      = {.sense = sense_grass, .ctx = &field};
        dgx_ge_actor_t         cow;
        dgx_ge_actor_start(&cow, &hero, TEST_HERO_SCRIPT_GRAZE, 0, 4, 0);
        cow.terrain = &eyes;
        dgx_ge_actor_advance(&cow, 50, NULL, NULL);
        CHECK(cow.params[TEST_HERO_PARAM_ACTION] == 20 && cow.params[TEST_HERO_PARAM_POWER] == 20 && cow.params[TEST_HERO_PARAM_HP] == 2);
    }
    /* laid again where a change reaches: the set as the grid lays it anew, only the pieces that changed marked */
    {
        /* a piece laid with the cell on the right (edge); rules that look up, left and down (rules) */
        CHECK(tiles.reach_x == 1 && tiles.reach_y == 1);
        /* an edge at 0, 0 (its card laid with 1, 0), a door (rules) at 1, 1 with nothing above it (its lip), floors and a block below */
        uint16_t rc[12] = {
            TEST_TILES_TILE_EDGE, 0, 0, TEST_TILES_TILE_FLOOR,
            0, TEST_TILES_TILE_RULES, 0, 0,
            TEST_TILES_TILE_FLOOR, TEST_TILES_TILE_BLOCK, TEST_TILES_TILE_FLOOR, 0,
        };
        uint8_t       rm[12] = {0};
        uint16_t      laid[13];
        dgx_ge_grid_t rg = {.tileset = &tiles, .maps = maps, .cells = rc, .mods = rm, .cols = 4, .rows = 3, .x = 16, .y = 8, .layer_of = {1, 2},
                            .bottom_up = true, .laid = laid};
        dgx_ge_grid_t plain = rg;
        plain.laid          = NULL;
        dgx_ge_scene_t *rs = dgx_ge_scene_create(frame, 3, 40, 4), *whole = dgx_ge_scene_create(frame, 3, 40, 4);
        CHECK(rs && whole && dgx_ge_grid_add(&rg, rs) && laid[0] == 0 && laid[12] == rs->set_count);
        for (int k = 0; k < 12; ++k) CHECK(laid[k] <= laid[k + 1]);
        dgx_ge_scene_render(rs);
        CHECK(frame->dirty_count == 0);
        /* the door's modifier 0 -> 1: its by_mod piece another picture, in its place (its gate, mod / 4, stays); only where it was and is marked */
        dgx_ge_item_t was[40];
        memcpy(was, rs->set, sizeof(dgx_ge_item_t) * rs->set_count);
        rm[5] = 1;
        CHECK(dgx_ge_grid_relay(&rg, rs, 1, 1, false) && same_as_laid(rs, &plain, whole));
        int changed = -1, changes = 0;
        for (int i = 0; i < rs->set_count; ++i)
            if (!dgx_ge_item_same(&was[i], &rs->set[i])) changed = i, ++changes;
        CHECK(changes == 1);
        const dgx_ge_rect_t both = dgx_ge_rect_union(dgx_ge_image_box(was[changed].image, was[changed].x, was[changed].y, false),
                                                     dgx_ge_image_box(rs->set[changed].image, rs->set[changed].x, rs->set[changed].y, false));
        CHECK(frame->dirty_count == 1 && dgx_ge_rect_equal(frame->dirty[0], both));
        dgx_ge_scene_render(rs);
        /* the same again: nothing shown otherwise, nothing marked */
        CHECK(dgx_ge_grid_relay(&rg, rs, 1, 1, false) && frame->dirty_count == 0);
        /* a cell beyond the grid on the left: what reaches the grid from there (the column 0) laid again; far away: nothing */
        CHECK(dgx_ge_grid_relay(&rg, rs, -1, 0, false) && dgx_ge_grid_relay(&rg, rs, 10, 10, true) && frame->dirty_count == 0);
        /* modifier 3: by_mod has no piece then, one fewer: no, its place marked; the game lays the set anew */
        const int before = rs->set_count;
        rm[5]            = 3;
        CHECK(!dgx_ge_grid_relay(&rg, rs, 1, 1, false) && frame->dirty_count >= 1 && rs->set_count == before);
        dgx_ge_scene_set_begin(rs);
        CHECK(dgx_ge_grid_add(&rg, rs));
        dgx_ge_scene_set_end(rs);
        CHECK(rs->set_count == before - 1 && same_as_laid(rs, &plain, whole) && laid[12] == rs->set_count);
        dgx_ge_scene_render(rs);
        /* a slab (a tile of no pieces) above the door: its lip's rule (nothing above) holds no more: no, marked */
        rc[1] = TEST_TILES_TILE_SLAB;
        CHECK(!dgx_ge_grid_relay(&rg, rs, 1, 0, true) && frame->dirty_count >= 1);
        dgx_ge_scene_set_begin(rs);
        CHECK(dgx_ge_grid_add(&rg, rs));
        dgx_ge_scene_set_end(rs);
        CHECK(same_as_laid(rs, &plain, whole));
        dgx_ge_scene_render(rs);
        /* a plate for the slab: no rule tells them apart, nothing shown otherwise */
        rc[1] = TEST_TILES_TILE_PLATE;
        CHECK(dgx_ge_grid_relay(&rg, rs, 1, 0, true) && frame->dirty_count == 0 && same_as_laid(rs, &plain, whole));
        /* nothing kept: no, and all of the frame marked */
        CHECK(!dgx_ge_grid_relay(&plain, rs, 1, 1, false) && frame->dirty_count == 1 && frame->dirty[0].w == 64 && frame->dirty[0].h == 48);
        dgx_ge_scene_render(rs);
        /* a set with no room beyond it: no, all of the frame marked, the set as it was */
        dgx_ge_scene_t *tight = dgx_ge_scene_create(frame, 3, rs->set_count, 4);
        CHECK(tight && dgx_ge_grid_add(&rg, tight) && tight->set_count == tight->set_capacity);
        dgx_ge_scene_render(tight);
        rm[5] = 1;
        CHECK(!dgx_ge_grid_relay(&rg, tight, 1, 1, false) && frame->dirty_count == 1 && frame->dirty[0].w == 64 && tight->set_count == tight->set_capacity);
        /* a grid laid in part (no room): its laid is no good for relaying */
        dgx_ge_scene_t *small = dgx_ge_scene_create(frame, 3, 2, 4);
        CHECK(small && !dgx_ge_grid_add(&rg, small) && !dgx_ge_grid_relay(&rg, small, 1, 1, false));
        dgx_ge_scene_destroy(&small);
        dgx_ge_scene_destroy(&tight);
        dgx_ge_scene_destroy(&whole);
        dgx_ge_scene_destroy(&rs);
    }
    /*
     * A body over many cells (72 x 40, cells of 8) on plates here and there: what it touches is what it touches among
     * every plate, by hand, however many there are about it (more than are gathered at once: still one solid).
     */
    {
        uint16_t      many[TOUCH_COLS * TOUCH_ROWS];
        dgx_ge_grid_t lot = {.tileset = &tiles, .cells = many, .cols = TOUCH_COLS, .rows = TOUCH_ROWS};
        srand(7);
        int differ = 0, crowded = 0;
        for (int round = 0; round < 400; ++round) {
            for (int i = 0; i < TOUCH_COLS * TOUCH_ROWS; ++i) many[i] = rand() % 3 ? TEST_TILES_TILE_PLATE : 0;
            const dgx_ge_body_t slab  = {&pack, TEST_IMG_SLAB, rand() % 72 - 8, rand() % 60 - 8, rand() % 2 != 0, 1u << 1};
            const int           reach = rand() % 3;
            static touches_t    found, by_hand;
            memset(&found, 0, sizeof(found)), memset(&by_hand, 0, sizeof(by_hand));
            dgx_ge_grid_touching(&lot, &slab, 1, reach, touch_into, &found);
            touches_by_hand(&lot, &slab, reach, &by_hand);
            differ += memcmp(&found, &by_hand, sizeof(found)) != 0;
            /* the plates of the cells about it (and around them: a body may reach into the next cell): more than 32, more than are gathered */
            dgx_ge_mask_t masks[16];
            dgx_ge_pack_masks(&pack, TEST_IMG_SLAB, masks, 16);
            const dgx_ge_rect_t r = dgx_ge_mask_place(&masks[0], slab.x, slab.y, slab.flip);
            int                 about = 0;
            for (int row = (r.y - reach + 64) / 8 - 9; row <= (r.y + r.h - 1 + reach + 64) / 8 - 7; ++row)
                for (int col = (r.x - reach + 64) / 8 - 9; col <= (r.x + r.w - 1 + reach + 64) / 8 - 7; ++col)
                    about += row >= 0 && col >= 0 && row < TOUCH_ROWS && col < TOUCH_COLS && many[row * TOUCH_COLS + col];
            crowded += about > 32;
        }
        CHECK(differ == 0 && crowded > 200);
    }
    dgx_ge_scene_destroy(&scene);
    dgx_ge_frame_destroy(&frame);
    dgx_screen_destroy(&display);
    dgx_ge_tileset_close(&tiles);
    free(data);
    CHECK_DONE();
}
