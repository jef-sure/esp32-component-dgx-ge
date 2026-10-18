/* The graph of scenes: scenes of maps, sides one way, exits by name of a scene or of its map, entries, the rules of a map. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "dgx_ge_game.h"
#include "dgx_ge_graph.h"
#include "dgx_ge_pack.h"
#include "test_pack.h"

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

/* the game's ear for the rules: a button pressed in a cell */
static int pressed = -1;

static void rules_said(dgx_ge_actor_t *a, dgx_ge_anim_say_t what, uint8_t value, uint32_t at_ms, void *ctx)
{
    (void)a, (void)value, (void)at_ms, (void)ctx;
    if (what == DGX_GE_ANIM_PRESS) pressed = dgx_ge_anim_said(0);
}

int main(int argc, char **argv)
{
    size_t   size = 0;
    uint8_t *data = read_file(argc > 1 ? argv[1] : "build/test.pack", &size);
    dgx_ge_pack_t pack;
    CHECK(data && dgx_ge_pack_open(&pack, data, size));
    size_t         n;
    const uint8_t *blob = dgx_ge_pack_blob(&pack, TEST_BLOB_WORLD, &n);
    dgx_ge_graph_t g;
    CHECK(blob && dgx_ge_graph_open(&g, blob, n) && g.scenes == 3 && g.maps == 2);
    CHECK(g.start_scene == 0 && g.start_entry == TEST_WORLD_ENTRY_START);

    /* scenes: their maps and numbers there; found back */
    int map, index, kind;
    CHECK(dgx_ge_graph_scene(&g, 2, &map, &index, &kind) && map == TEST_WORLD_MAP_CELLAR && index == 1 && kind == 0);
    CHECK(dgx_ge_graph_find(&g, TEST_WORLD_MAP_HALL, 2) == 1 && dgx_ge_graph_find(&g, TEST_WORLD_MAP_CELLAR, 2) == -1);
    CHECK(!dgx_ge_graph_scene(&g, 3, NULL, NULL, NULL));

    /* sides: one way each, none where none was given */
    CHECK(dgx_ge_graph_side(&g, 0, DGX_GE_SIDE_RIGHT) == 1 && dgx_ge_graph_side(&g, 1, DGX_GE_SIDE_LEFT) == 0);
    CHECK(dgx_ge_graph_side(&g, 0, DGX_GE_SIDE_LEFT) == -1 && dgx_ge_graph_side(&g, 2, DGX_GE_SIDE_UP) == -1 && dgx_ge_graph_side(&g, 0, 7) == -1);

    /* exits: the hatch of the first room, one way, fading; the map's from any of its rooms; none of that name */
    dgx_ge_go_t go;
    CHECK(dgx_ge_graph_exit(&g, 0, TEST_WORLD_EXIT_HATCH, &go) && go.scene == 2 && go.entry == TEST_WORLD_ENTRY_SLOPE && go.transition == DGX_GE_GO_FADE);
    CHECK(!dgx_ge_graph_exit(&g, 1, TEST_WORLD_EXIT_HATCH, &go)); /* the second room has no hatch */
    CHECK(dgx_ge_graph_exit(&g, 1, TEST_WORLD_EXIT_START, &go) && go.scene == 0 && go.entry == TEST_WORLD_ENTRY_START && go.transition == DGX_GE_GO_CUT);
    CHECK(!dgx_ge_graph_exit(&g, 2, TEST_WORLD_EXIT_START, &go)); /* the cellar's map has none */

    /* entries: a place, a facing, what the one coming is told */
    dgx_ge_entry_t e;
    CHECK(dgx_ge_graph_entry(&g, 2, TEST_WORLD_ENTRY_SLOPE, &e) && e.x == 40 && e.y == 0 && !e.left && e.arrive == TEST_EVENT_FALL);
    CHECK(dgx_ge_graph_entry(&g, 1, TEST_WORLD_ENTRY_MARK, &e) && e.left && e.arrive == 0xFF && e.fires == 3);
    CHECK(dgx_ge_graph_entry(&g, 2, TEST_WORLD_ENTRY_SLOPE, &e) && e.fires == 0xFF);
    /* the event of coming into a scene: the second room tells the hall's rules its end */
    CHECK(dgx_ge_graph_scene_event(&g, 1) == TEST_EVENT_END && dgx_ge_graph_scene_event(&g, 0) == -1 && dgx_ge_graph_scene_event(&g, 9) == -1);
    /* the events of leaving by a side: beside a scene, or at an edge with none (out of the map); none elsewhere */
    CHECK(dgx_ge_graph_leave_event(&g, 0, DGX_GE_SIDE_RIGHT) == TEST_EVENT_END && dgx_ge_graph_leave_event(&g, 1, DGX_GE_SIDE_DOWN) == TEST_EVENT_DEATH);
    CHECK(dgx_ge_graph_side(&g, 1, DGX_GE_SIDE_DOWN) == -1 && dgx_ge_graph_leave_event(&g, 1, DGX_GE_SIDE_LEFT) == -1);
    CHECK(dgx_ge_graph_leave_event(&g, 0, 7) == -1 && dgx_ge_graph_leave_event(&g, 9, DGX_GE_SIDE_LEFT) == -1);
    /* the number a map is named by: the cellar's, none for the hall */
    CHECK(dgx_ge_graph_map_number(&g, TEST_WORLD_MAP_CELLAR) == 12 && dgx_ge_graph_map_number(&g, TEST_WORLD_MAP_HALL) == 0);
    CHECK(dgx_ge_graph_map_number(&g, 5) == 0);
    /* a transition at once and as one is */
    CHECK(dgx_ge_graph_exit(&g, 2, TEST_WORLD_EXIT_LIFT, &go) && go.scene == 0 && go.transition == DGX_GE_GO_SEAMLESS);
    CHECK(!dgx_ge_graph_entry(&g, 0, TEST_WORLD_ENTRY_MARK, &e));

    /* the rules of the hall: died twice, back at the mark; the third time the start, and counting anew; the end leads on */
    int data_blob, rules;
    CHECK(dgx_ge_graph_map(&g, TEST_WORLD_MAP_HALL, &data_blob, &rules) && data_blob == TEST_BLOB_LEVEL && rules == TEST_BLOB_WORLD_HALL_RULES);
    CHECK(dgx_ge_graph_map(&g, TEST_WORLD_MAP_CELLAR, &data_blob, &rules) && data_blob == -1 && rules == -1);
    const uint8_t *rb = dgx_ge_pack_blob(&pack, rules = TEST_BLOB_WORLD_HALL_RULES, &n);
    dgx_ge_anim_t  anim;
    CHECK(rb && dgx_ge_anim_open(&anim, rb, n));
    dgx_ge_actor_t hall;
    dgx_ge_actor_start(&hall, &anim, 0, 0, 0, 0);
    CHECK(dgx_ge_graph_rule(&hall, TEST_EVENT_DEATH, 100, NULL, NULL) == TEST_WORLD_EXIT_CHECKPOINT);
    CHECK(dgx_ge_graph_rule(&hall, TEST_EVENT_DEATH, 200, NULL, NULL) == TEST_WORLD_EXIT_CHECKPOINT);
    CHECK(dgx_ge_graph_rule(&hall, TEST_EVENT_DEATH, 300, NULL, NULL) == TEST_WORLD_EXIT_START);
    CHECK(dgx_ge_graph_rule(&hall, TEST_EVENT_DEATH, 400, NULL, NULL) == TEST_WORLD_EXIT_CHECKPOINT);
    /* what else the rules say goes to the game: a button pressed, its cell; the exit taken still */
    pressed = -1;
    CHECK(dgx_ge_graph_rule(&hall, TEST_EVENT_END, 500, rules_said, NULL) == TEST_WORLD_EXIT_NEXT && pressed == 9);
    CHECK(dgx_ge_graph_rule(&hall, -1, 550, rules_said, NULL) == -1); /* only moved on */
    CHECK(dgx_ge_graph_rule(&hall, TEST_EVENT_OPEN, 600, NULL, NULL) == -1); /* an event the map has no rule for */

    CHECK(!dgx_ge_graph_open(&g, data, 4));

    /* the constants of the game: by their places, as written; none there: what is asked otherwise */
    dgx_ge_game_t game;
    blob = dgx_ge_pack_blob(&pack, TEST_BLOB_GAME, &n);
    CHECK(blob && dgx_ge_game_open(&game, blob, n) && game.count == 4);
    CHECK(dgx_ge_game_value(&game, TEST_GAME_KINDS, 0) == 16 && dgx_ge_game_value(&game, TEST_GAME_LIVES, 0) == 4);
    CHECK(TEST_GAME_MINUTES == 2 && dgx_ge_game_value(&game, TEST_GAME_MINUTES, 0) == 60 && dgx_ge_game_value(&game, 7, -5) == -5);
    CHECK(!dgx_ge_game_open(&game, data, 4) && dgx_ge_game_value(&game, 0, 9) == 9);
    free(data);
    CHECK_DONE();
}
