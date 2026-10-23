/*
 * A scene laid in the sprite editor (build/layout/layout.pack, made by test_scenes.py): its zones and entries by the
 * header's names, its actor started as laid (kind, script, place, the params given), its layers and pictures laid
 * into a scene, its grid (the set of tiles, cells, modifiers, variants, the layers of its slots) set up and laid; its
 * links: the cells numbered through the map, each source's first link, the links of the map fired from it to the
 * lives of their cells; a blob not whole or of another kind refused.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "dgx_ge_cells.h"
#include "dgx_ge_layout.h"
#include "dgx_ge_links.h"
#include "drivers/vscreen.h"
#include "layout_pack.h"

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc(*size);
    if (data && fread(data, 1, *size, f) != *size) free(data), data = NULL;
    fclose(f);
    return data;
}

/* the game's cells of the map: room1's 9 and room2's 4, a tile and a modifier each; every one lives as a HERO */
static int tiles[13], mods[13];

static void get(uint16_t cell, int *tile, int *mod, void *ctx)
{
    (void)ctx;
    *tile = tiles[cell], *mod = mods[cell];
}

static void set(uint16_t cell, int tile, int mod, void *ctx)
{
    (void)ctx;
    tiles[cell] = tile, mods[cell] = mod;
}

static uint16_t life_of(uint16_t cell, void *ctx)
{
    (void)ctx;
    return cell < 13 ? LAY_BLOB_HERO : 0xFFFF;
}

/* the game's output of sounds: the music it was asked for, and the fade */
static int      asked_music = -1;
static uint32_t asked_fade;

static void asked(void *ctx, int music, uint32_t fade_ms)
{
    (void)ctx;
    asked_music = music, asked_fade = fade_ms;
}

int main(int argc, char **argv)
{
    size_t   size = 0;
    uint8_t *data = read_file(argc > 1 ? argv[1] : "build/layout/layout.pack", &size);
    dgx_ge_pack_t pack;
    CHECK(data && dgx_ge_pack_open(&pack, data, size));
    size_t           n;
    const uint8_t   *blob = dgx_ge_pack_blob(&pack, LAY_BLOB_SCENE_ROOM1, &n);
    dgx_ge_layout_t  l;
    CHECK(blob && dgx_ge_layout_open(&l, blob, n));
    CHECK(l.layers == 2 && l.grids == 1 && l.width == 64 && l.height == 32 && l.pictures == 2 && l.actors == 1 && l.zones == 2 && l.entries == 1 &&
          l.sources == 3);

    /* the scene's music: the one it names and its fade, asked of the output as the game enters it; room2 names none */
    uint32_t fade = 0;
    CHECK(dgx_ge_layout_music_of(&l, &fade) == LAY_MUSIC_THEME && fade == 300 && dgx_ge_layout_music_of(&l, NULL) == LAY_MUSIC_THEME);
    const dgx_ge_sound_t out = {.ctx = NULL, .music = asked};
    dgx_ge_layout_music(&l, &out);
    CHECK(asked_music == LAY_MUSIC_THEME && asked_fade == 300);
    dgx_ge_layout_music(&l, NULL); /* no output: nothing */
    {
        size_t          n2;
        const uint8_t  *b2 = dgx_ge_pack_blob(&pack, LAY_BLOB_SCENE_ROOM2, &n2);
        dgx_ge_layout_t l2;
        CHECK(b2 && dgx_ge_layout_open(&l2, b2, n2) && dgx_ge_layout_music_of(&l2, &fade) == -1);
        asked_music = -5;
        dgx_ge_layout_music(&l2, &out);
        CHECK(asked_music == -5); /* what plays goes on */
        /* silence named: 255, as a script's music stop */
        uint8_t *quiet = malloc(n2);
        memcpy(quiet, b2, n2);
        quiet[33] = 2, quiet[34] = 0xF4, quiet[35] = 1;
        CHECK(dgx_ge_layout_open(&l2, quiet, n2) && dgx_ge_layout_music_of(&l2, &fade) == 255 && fade == 500);
        dgx_ge_layout_music(&l2, &out);
        CHECK(asked_music == 255 && asked_fade == 500);
        free(quiet);
    }

    /* zones and entries by their names */
    dgx_ge_rect_t place;
    int           kind = -1, x = 0, y = 0;
    bool          left = true;
    CHECK(dgx_ge_layout_zone(&l, LAY_SCENE_ROOM1_ZONE_DOOR, &place, &kind) && dgx_ge_rect_equal(place, dgx_ge_rect(50, 10, 8, 16)) && kind == LAY_ZONE_EXIT);
    CHECK(dgx_ge_layout_zone(&l, LAY_SCENE_ROOM1_ZONE_PIT, &place, &kind) && dgx_ge_rect_equal(place, dgx_ge_rect(0, 30, 64, 2)) && kind == LAY_ZONE_TRAP);
    CHECK(!dgx_ge_layout_zone(&l, 2, &place, &kind));
    CHECK(dgx_ge_layout_entry(&l, LAY_SCENE_ROOM1_ENTRY_START, &x, &y, &left) && x == 4 && y == 28 && !left);
    CHECK(!dgx_ge_layout_entry(&l, 1, &x, &y, &left));

    /* the actor: as laid, and started so — its script, place and facing, hp as given, speed its kind's own */
    dgx_ge_layout_actor_t a;
    CHECK(dgx_ge_layout_actor(&l, LAY_SCENE_ROOM1_ACTOR_GUARD, &a) && a.kind == LAY_BLOB_HERO && a.script == LAY_HERO_SCRIPT_WALK && a.layer == 0 &&
          a.x == 30 && a.y == 28 && !a.flip && a.turn == 3 && a.params == 1);
    dgx_ge_kinds_t kinds;
    CHECK(dgx_ge_kinds_init(&kinds, &pack, 4));
    dgx_ge_actor_t guard;
    CHECK(dgx_ge_layout_actor_start(&l, LAY_SCENE_ROOM1_ACTOR_GUARD, &guard, &kinds, 0));
    CHECK(guard.turn == 3 && guard.entered_turn == 3 && dgx_ge_actor_rotation(&guard) == 270); /* laid turned 270 */
    CHECK(guard.script == LAY_HERO_SCRIPT_WALK && dgx_ge_actor_x(&guard) == 30 && dgx_ge_actor_y(&guard) == 28 && !guard.flip &&
          guard.params[LAY_HERO_PARAM_HP] == 5 && guard.params[LAY_HERO_PARAM_SPEED] == 1);
    CHECK(!dgx_ge_layout_actor_start(&l, 1, &guard, &kinds, 0));
    /* laid facing left (a copy of the room with its facing bit set: the record is 10 bytes before its params): it entered its script so */
    {
        uint8_t *copy = malloc(n);
        memcpy(copy, blob, n);
        dgx_ge_layout_t turned;
        CHECK(dgx_ge_layout_open(&turned, copy, n) && dgx_ge_layout_actor(&turned, LAY_SCENE_ROOM1_ACTOR_GUARD, &a));
        copy[a.given - copy - 10 + 8] |= 1;
        CHECK(dgx_ge_layout_actor_start(&turned, LAY_SCENE_ROOM1_ACTOR_GUARD, &guard, &kinds, 0) && guard.flip && guard.turn == 3);
        CHECK(guard.entered_flip && guard.entered_params[LAY_HERO_PARAM_HP] == 5 && guard.entered_params[LAY_HERO_PARAM_SPEED] == 1);
        /* a scene of the version before (no turn) refused */
        copy[4] = 4;
        CHECK(!dgx_ge_layout_open(&turned, copy, n));
        free(copy);
    }

    /* the paper, the layers and pictures into a scene: its background the color 0 of the frame, factors and orders, the pictures as pieces of its set */
    dgx_screen_t   *display = dgx_vscreen_init(64, 32, 16, DgxScreenRGB);
    dgx_ge_frame_t *frame   = dgx_ge_frame_create(display, 0, 0, 64, 32);
    dgx_ge_scene_t *scene   = dgx_ge_scene_create(frame, 2, 16, 4);
    dgx_image_t     images[2];
    dgx_ge_scene_reset(scene, 3);
    dgx_ge_scene_render(scene);
    CHECK(frame->dirty_count == 0);
    dgx_ge_scene_set_begin(scene);
    CHECK(dgx_ge_layout_scene(&l, scene, &pack, NULL, images));
    CHECK(scene->paper == 0 && frame->rgb[0][0] == 20 && frame->rgb[0][1] == 20 && frame->rgb[0][2] == 42 && frame->dirty_count == 1 &&
          frame->dirty[0].w == 64 && frame->dirty[0].h == 32);
    CHECK(scene->layers[0].factor == 256 && scene->layers[0].order == DGX_GE_ORDER_ADDED && scene->layers[1].factor == 128 &&
          scene->layers[1].order == DGX_GE_ORDER_KEY);
    CHECK(scene->set_count == 2 && scene->set[0].image == &images[0] && images[0].width == 4 && scene->set[0].x == 10 && scene->set[0].y == 20 &&
          (scene->set[0].flags & DGX_GE_BLIT_FLIP_X) && scene->set[0].layer == 0 && dgx_ge_rect_equal(scene->set[0].clip, dgx_ge_rect(0, 0, 32, 32)));
    CHECK(scene->set[1].key == 3 && scene->set[1].layer == 1 && !(scene->set[1].flags & DGX_GE_BLIT_FLIP_X) && dgx_ge_rect_empty(scene->set[1].clip));
    CHECK(scene->set[0].turn == 0 && scene->set[1].turn == 2); /* the front one turned 180 */

    /* the grid: its set of tiles, place, size, cells, modifiers, variants, the layers of its slots; set up and laid: a piece a tile */
    dgx_ge_layout_grid_t lg;
    CHECK(dgx_ge_layout_grid(&l, LAY_SCENE_ROOM1_GRID_GRID, &lg) && lg.tiles == LAY_BLOB_TILES && lg.x == 0 && lg.y == 8 && lg.cols == 3 &&
          lg.rows == 3 && lg.first == 0 && lg.slots == 2 && lg.layer_of[0] == 0 && lg.layer_of[1] == 1);
    CHECK(lg.cells[0] == 0 && lg.cells[1] == LAY_TILES_TILE_FLOOR && lg.cells[2] == LAY_TILES_TILE_WALL && lg.cells[3] == 1 && lg.cells[5] == 0 && lg.cells[8] == 1);
    CHECK(lg.mods && lg.mods[1] == 7 && lg.mods[0] == 0 && lg.variants && lg.variants[2] == 70000 && lg.variants[1] == 0 && lg.variants[3] == 0);
    dgx_ge_layout_grid_t none;
    CHECK(!dgx_ge_layout_grid(&l, 1, &none)); /* one grid only */
    const uint8_t   *tb = dgx_ge_pack_blob(&pack, LAY_BLOB_TILES, &n);
    dgx_ge_tileset_t tiles;
    CHECK(tb && dgx_ge_tileset_open(&tiles, &pack, tb, n));
    dgx_ge_layout_grid(&l, LAY_SCENE_ROOM1_GRID_GRID, &lg);
    uint16_t      cells[9];
    dgx_ge_grid_t grid = {0};
    memcpy(cells, lg.cells, sizeof cells); /* the game's copy: it may change them */
    dgx_ge_layout_grid_set(&lg, &grid, &tiles, cells);
    CHECK(grid.tileset == &tiles && grid.cells == cells && grid.cols == 3 && grid.rows == 3 && grid.y == 8 && grid.mods == lg.mods &&
          grid.variants == lg.variants && grid.layer_of[1] == 1 && grid.layer_of[2] == 0xFF);
    CHECK(dgx_ge_grid_add(&grid, scene) && scene->set_count == 2 + 5); /* five cells with a tile, a piece each */
    dgx_ge_scene_set_end(scene);
    /* laid anew, the paper the same: nothing drawn anew for it */
    dgx_ge_scene_render(scene);
    dgx_ge_scene_set_begin(scene);
    CHECK(dgx_ge_layout_scene(&l, scene, &pack, NULL, images) && frame->dirty_count == 0);
    dgx_ge_scene_set_end(scene);

    /* the cells numbered through the map: room1's grid 0..8, room2's 9..12; a number to its cell, of this scene or not */
    int gi = -1, col = -1, row = -1;
    CHECK(dgx_ge_layout_cell_at(&l, 8, &gi, &col, &row) && gi == LAY_SCENE_ROOM1_GRID_GRID && col == 2 && row == 2);
    CHECK(dgx_ge_layout_cell_at(&l, 1, &gi, &col, &row) && col == 1 && row == 0 && !dgx_ge_layout_cell_at(&l, 9, &gi, &col, &row));
    /* the sources and their first links: the plate (cell 1) 0, the guard 2, the pit 3; the door and cell 0 fire nothing */
    CHECK(dgx_ge_layout_links_of(&l, DGX_GE_LAYOUT_CELL, 1) == 0 && dgx_ge_layout_links_of(&l, DGX_GE_LAYOUT_ACTOR, LAY_SCENE_ROOM1_ACTOR_GUARD) == 2 &&
          dgx_ge_layout_links_of(&l, DGX_GE_LAYOUT_ZONE, LAY_SCENE_ROOM1_ZONE_PIT) == 3);
    CHECK(dgx_ge_layout_links_of(&l, DGX_GE_LAYOUT_ZONE, LAY_SCENE_ROOM1_ZONE_DOOR) == -1 && dgx_ge_layout_links_of(&l, DGX_GE_LAYOUT_CELL, 0) == -1);
    /* fired: the plate's two links tell the lives of cells 8 (with its value, 5) and 6; the guard's, the event fired, cell 3 */
    const uint8_t *lb = dgx_ge_pack_blob(&pack, LAY_BLOB_WORLD_LEVEL_LINKS, &n);
    dgx_ge_links_t links;
    CHECK(lb && dgx_ge_links_open(&links, lb, n) && links.count == 5);
    dgx_ge_cells_t lives;
    CHECK(dgx_ge_cells_init(&lives, &kinds, 8, &(dgx_ge_cells_io_t){get, set, NULL, NULL, 13, NULL}));
    CHECK(dgx_ge_links_fire(&links, dgx_ge_layout_links_of(&l, DGX_GE_LAYOUT_CELL, 1), LAY_EVENT_GO, &lives, life_of, NULL, 0) == 2);
    CHECK(dgx_ge_cells_living(&lives, 8) && dgx_ge_cells_living(&lives, 6) && !dgx_ge_cells_living(&lives, 3) && lives.lives[0].cell == 8 &&
          lives.lives[0].own[0] == 5);
    CHECK(dgx_ge_links_fire(&links, dgx_ge_layout_links_of(&l, DGX_GE_LAYOUT_ACTOR, LAY_SCENE_ROOM1_ACTOR_GUARD), LAY_EVENT_GO, &lives, life_of, NULL,
                            0) == 1 &&
          dgx_ge_cells_living(&lives, 3) && !dgx_ge_cells_living(&lives, 4));
    dgx_ge_cells_free(&lives);

    /* a scene of fewer layers than laid; a blob cut short, of another version, of another kind */
    dgx_ge_scene_t *one = dgx_ge_scene_create(frame, 1, 16, 4);
    CHECK(!dgx_ge_layout_scene(&l, one, &pack, NULL, images));
    blob = dgx_ge_pack_blob(&pack, LAY_BLOB_SCENE_ROOM1, &n);
    dgx_ge_layout_t bad;
    CHECK(!dgx_ge_layout_open(&bad, blob, n - 1) && !dgx_ge_layout_open(&bad, blob, 20));
    uint8_t *copy = malloc(n);
    memcpy(copy, blob, n), copy[4] = 1; /* the version before the links */
    CHECK(!dgx_ge_layout_open(&bad, copy, n));
    memcpy(copy, blob, n), copy[0] = 'X';
    CHECK(!dgx_ge_layout_open(&bad, copy, n));
    blob = dgx_ge_pack_blob(&pack, LAY_BLOB_SCENE_ROOM2, &n);
    CHECK(blob && dgx_ge_layout_open(&bad, blob, n) && bad.entries == 1 && bad.actors == 0 && bad.grids == 1 && bad.sources == 1);
    CHECK(dgx_ge_layout_entry(&bad, LAY_SCENE_ROOM2_ENTRY_IN, &x, &y, &left) && x == 2 && y == 20 && left);
    /* room2: its grid's cells after room1's, its plate (cell 12, its last) from link 4 */
    CHECK(dgx_ge_layout_grid(&bad, LAY_SCENE_ROOM2_GRID_FLOOR, &lg) && lg.first == 9 && dgx_ge_layout_cell_at(&bad, 12, &gi, &col, &row) && gi == 0 &&
          col == 1 && row == 1 && !dgx_ge_layout_cell_at(&bad, 8, &gi, &col, &row));
    CHECK(dgx_ge_layout_links_of(&bad, DGX_GE_LAYOUT_CELL, 12) == 4 && dgx_ge_layout_links_of(&bad, DGX_GE_LAYOUT_CELL, 1) == -1);
    /* room2 laid: no background in the document, no paper; the game's stays, nothing drawn anew for it */
    dgx_ge_scene_reset(scene, 2);
    dgx_ge_scene_render(scene);
    dgx_ge_scene_set_begin(scene);
    CHECK(dgx_ge_layout_scene(&bad, scene, &pack, NULL, images) && scene->paper == 2 && frame->rgb[0][0] == 20 && frame->rgb[0][2] == 42 &&
          frame->dirty_count == 0);
    dgx_ge_scene_set_end(scene);
    free(copy);
    dgx_ge_tileset_close(&tiles);
    dgx_ge_kinds_free(&kinds);
    dgx_ge_scene_destroy(&one);
    dgx_ge_scene_destroy(&scene);
    dgx_ge_frame_destroy(&frame);
    dgx_screen_destroy(&display);
    free(data);
    CHECK_DONE();
}
