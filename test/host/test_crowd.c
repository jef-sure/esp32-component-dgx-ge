/* A crowd: strewn by its seed, the same every time; a wolf that hunts and eats, sheep that run, a hit's damage and
   events; a mother that bears with genes, no place for one more; a walker turned by the bounds; the items of a scene. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "dgx_ge_crowd.h"
#include "drivers/vscreen.h"
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

static bool open_crowd(dgx_ge_crowd_t *c, const dgx_ge_pack_t *pack, int blob)
{
    size_t         n;
    const uint8_t *b = dgx_ge_pack_blob(pack, blob, &n);
    return b && dgx_ge_crowd_open(c, b, n);
}

/* The place of the first of a kind, -1 none. */
static int first_of(const dgx_ge_crowd_t *c, uint16_t kind)
{
    for (int i = 0; i < c->places; ++i)
        if (c->members[i].kind == kind) return i;
    return -1;
}

static int sounds;

static void heard(dgx_ge_actor_t *actor, dgx_ge_anim_say_t what, uint8_t value, uint32_t at_ms, void *ctx)
{
    (void)actor, (void)value, (void)at_ms, (void)ctx;
    if (what == DGX_GE_ANIM_SOUND) ++sounds;
}

int main(int argc, char **argv)
{
    size_t   size = 0;
    uint8_t *data = read_file(argc > 1 ? argv[1] : "build/test.pack", &size);
    dgx_ge_pack_t pack;
    CHECK(data && dgx_ge_pack_open(&pack, data, size));
    dgx_ge_kinds_t kinds;
    CHECK(dgx_ge_kinds_init(&kinds, &pack, 16));

    /* not a crowd; one of more kinds than a byte names */
    dgx_ge_crowd_t c;
    CHECK(!dgx_ge_crowd_open(&c, data, 8));
    {
        size_t         n;
        const uint8_t *b   = dgx_ge_pack_blob(&pack, TEST_BLOB_FIELD, &n);
        uint8_t       *big = calloc(1, 24 + 256 * 8);
        memcpy(big, b, 24);
        big[8] = 0, big[9] = 1, big[10] = big[11] = 0; /* 256 kinds, no groups */
        CHECK(!dgx_ge_crowd_open(&c, big, 24 + 256 * 8));
        big[8] = 255, big[9] = 0;
        CHECK(dgx_ge_crowd_open(&c, big, 24 + 256 * 8)); /* 255 do */
        free(big);
    }

    /* the field: a wolf in its corner, three sheep strewn in theirs, the same places from the same seed */
    CHECK(open_crowd(&c, &pack, TEST_BLOB_FIELD) && c.places == 8 && c.kind_count == 2 && c.groups == 2);
    CHECK(c.bounds.w == 200 && c.bounds.h == 150);
    CHECK(dgx_ge_crowd_start(&c, &pack, &kinds, NULL, 0));
    CHECK(dgx_ge_crowd_count(&c, TEST_BLOB_WOLF) == 1 && dgx_ge_crowd_count(&c, TEST_BLOB_SHEEP) == 3);
    CHECK(dgx_ge_crowd_count(&c, DGX_GE_CROWD_FREE) == 4);
    int32_t was[8][2];
    for (int i = 0; i < c.places; ++i) {
        const dgx_ge_member_t *m = &c.members[i];
        if (m->kind == DGX_GE_CROWD_FREE) continue;
        const int x = dgx_ge_actor_x(&m->actor), y = dgx_ge_actor_y(&m->actor);
        if (m->kind == TEST_BLOB_WOLF) CHECK(x >= 10 && x < 30 && y >= 10 && y < 30);
        else CHECK(x >= 120 && x < 180 && y >= 80 && y < 130);
        was[i][0] = m->actor.x, was[i][1] = m->actor.y;
    }
    dgx_ge_crowd_t again;
    CHECK(open_crowd(&again, &pack, TEST_BLOB_FIELD) && dgx_ge_crowd_start(&again, &pack, &kinds, NULL, 0));
    for (int i = 0; i < 4; ++i) CHECK(again.members[i].actor.x == was[i][0] && again.members[i].actor.y == was[i][1]);
    dgx_ge_crowd_stop(&again);

    /* a few steps: the wolf has sensed the nearest sheep (seen: its hp, its. of the kind) and makes for it */
    dgx_ge_crowd_play(&c, 350, heard, NULL);
    const int w = first_of(&c, TEST_BLOB_WOLF);
    CHECK(w >= 0 && c.members[w].actor.params[TEST_WOLF_PARAM_D] > 0 && c.members[w].actor.params[TEST_WOLF_PARAM_SEEN] == 5);
    CHECK(c.members[w].actor.vx > 0 && c.members[w].actor.vy > 0); /* the sheep are right and down of it */
    /* every frame, in the scene: bodies of each, the registry knows their actors */
    for (int i = 0; i < c.places; ++i)
        if (c.members[i].kind != DGX_GE_CROWD_FREE) CHECK(dgx_ge_collisions_actor_of(c.bodies, i) == &c.members[i].actor);

    /* on until it has eaten: a sheep hit (damage 2 off its hp, event eaten) lies dead, the wolf fed (recoil ate) */
    bool seen_dead = false;
    int  t         = 350;
    while (t < 60000 && c.members[w].actor.params[TEST_WOLF_PARAM_FOOD] == 0) {
        t += 20;
        dgx_ge_crowd_play(&c, (uint32_t)t, heard, NULL);
        for (int i = 0; i < c.places; ++i) {
            const dgx_ge_member_t *m = &c.members[i];
            if (m->kind == TEST_BLOB_SHEEP && m->actor.script == TEST_SHEEP_SCRIPT_DEAD) {
                CHECK(m->actor.params[TEST_SHEEP_PARAM_HP] == 3);
                seen_dead = true;
            }
        }
    }
    CHECK(c.members[w].actor.params[TEST_WOLF_PARAM_FOOD] >= 1 && seen_dead);
    /* the dead one vanishes after its 200 ms: its place is free, the others live on */
    for (int k = 0; k < 20; ++k) t += 20, dgx_ge_crowd_play(&c, (uint32_t)t, heard, NULL);
    CHECK(dgx_ge_crowd_count(&c, TEST_BLOB_SHEEP) == 2);
    /* nobody left the bounds (solid beyond them: the steering only pushes them there) */
    for (int i = 0; i < c.places; ++i)
        if (c.members[i].kind != DGX_GE_CROWD_FREE) {
            const int x = dgx_ge_actor_x(&c.members[i].actor), y = dgx_ge_actor_y(&c.members[i].actor);
            CHECK(x > -100 && x < 300 && y > -100 && y < 250);
        }
    /* the items: one an actor, in the layer of its kind, its key its y */
    dgx_screen_t   *display = dgx_vscreen_init(200, 150, 16, DgxScreenRGB);
    dgx_ge_frame_t *frame   = dgx_ge_frame_create(display, 0, 0, 200, 150);
    dgx_ge_scene_t *scene   = dgx_ge_scene_create(frame, 2, 0, 8);
    uint8_t         hero_map[15] = {1, 2, 3};
    const uint8_t  *maps[TEST_SET_COUNT] = {[TEST_SET_HERO] = hero_map};
    dgx_ge_scene_actors_begin(scene);
    CHECK(dgx_ge_crowd_items(&c, maps, scene) && scene->actor_count == dgx_ge_crowd_count(&c, DGX_GE_CROWD_FREE));
    for (int i = 0; i < scene->actor_count; ++i) CHECK(scene->actors[i].layer == 1 && scene->actors[i].map == hero_map);
    CHECK(scene->actors[0].key == scene->actors[0].y);
    dgx_ge_scene_t *small = dgx_ge_scene_create(frame, 2, 0, 1);
    dgx_ge_scene_actors_begin(small);
    CHECK(!dgx_ge_crowd_items(&c, maps, small) && small->actor_count == 1); /* no room for all */
    dgx_ge_scene_destroy(&small);
    /* played out the same from the same seed: a second run of the same crowd ends where the first did */
    dgx_ge_crowd_t twin;
    CHECK(open_crowd(&twin, &pack, TEST_BLOB_FIELD) && dgx_ge_crowd_start(&twin, &pack, &kinds, NULL, 0));
    dgx_ge_crowd_play(&twin, 350, NULL, NULL);
    for (int u = 350; u < t;) u += 20, dgx_ge_crowd_play(&twin, (uint32_t)u, NULL, NULL);
    for (int i = 0; i < c.places; ++i) {
        CHECK(twin.members[i].kind == c.members[i].kind);
        if (c.members[i].kind != DGX_GE_CROWD_FREE) CHECK(twin.members[i].actor.x == c.members[i].actor.x && twin.members[i].actor.y == c.members[i].actor.y);
    }
    dgx_ge_crowd_stop(&twin);
    dgx_ge_crowd_stop(&c);
    CHECK(!c.members && !c.bodies);

    /* the nursery: a mother bears two (appear ... with gene), there is a place for one: it has its genes, faces left, is 10 right */
    CHECK(open_crowd(&c, &pack, TEST_BLOB_NURSERY) && dgx_ge_crowd_start(&c, &pack, &kinds, NULL, 0));
    dgx_ge_crowd_play(&c, 400, NULL, NULL);
    CHECK(dgx_ge_crowd_count(&c, TEST_BLOB_SHEEP) == 0);
    dgx_ge_crowd_play(&c, 600, NULL, NULL);
    CHECK(dgx_ge_crowd_count(&c, TEST_BLOB_SHEEP) == 1 && dgx_ge_crowd_count(&c, TEST_BLOB_MOTHER) == 1 && c.told_full);
    const int lamb = first_of(&c, TEST_BLOB_SHEEP), mother = first_of(&c, TEST_BLOB_MOTHER);
    CHECK(c.members[lamb].actor.params[TEST_SHEEP_PARAM_GENE] == 14 && c.members[lamb].actor.entered_params[TEST_SHEEP_PARAM_GENE] == 14);
    CHECK(c.members[lamb].actor.flip && c.members[lamb].actor.due_ms > 500); /* moved on to now in the play it was born in */
    CHECK(c.members[lamb].actor.params[TEST_SHEEP_PARAM_HP] == 5); /* the rest as they start */
    (void)mother;
    dgx_ge_crowd_stop(&c);

    /* blind: a watcher looks at one standing still 50 points off (seen within 100, and within 60) over a grid of 10 x 4 cells of 8 */
    {
        uint16_t      cells[40] = {0};
        dgx_ge_grid_t grid      = {.tileset = NULL, .cells = cells, .cols = 10, .rows = 4};
        size_t        tn;
        const uint8_t *tb = dgx_ge_pack_blob(&pack, TEST_BLOB_TILES, &tn);
        dgx_ge_tileset_t tiles;
        CHECK(tb && dgx_ge_tileset_open(&tiles, &pack, tb, tn));
        grid.tileset = &tiles;
        CHECK(open_crowd(&c, &pack, TEST_BLOB_BLIND) && dgx_ge_crowd_start(&c, &pack, &kinds, &grid, 0));
        const int  eye = first_of(&c, TEST_BLOB_LOOKOUT);
        const int16_t *p = c.members[eye].actor.params;
        dgx_ge_crowd_play(&c, 150, NULL, NULL);
        CHECK(p[TEST_LOOKOUT_PARAM_D] == 50 && p[TEST_LOOKOUT_PARAM_FAR] == 50 && p[TEST_LOOKOUT_PARAM_HEDGE] == -1); /* the open: seen; no cell */
        CHECK(p[TEST_LOOKOUT_PARAM_REACH] == 40 && p[TEST_LOOKOUT_PARAM_NEAR] == -1); /* its nose reaches 40 at its second step: not yet */
        for (int r = 0; r < 4; ++r) cells[r * 10 + 4] = TEST_TILES_TILE_HEDGE; /* a hedge between them */
        dgx_ge_crowd_play(&c, 300, NULL, NULL);
        CHECK(p[TEST_LOOKOUT_PARAM_D] == -1 && p[TEST_LOOKOUT_PARAM_FAR] == -1);
        CHECK(p[TEST_LOOKOUT_PARAM_REACH] >= 50 && p[TEST_LOOKOUT_PARAM_NEAR] == 50); /* the nose: 50 and on, through the hedge */
        CHECK(p[TEST_LOOKOUT_PARAM_HEDGE] > 0 && p[TEST_LOOKOUT_PARAM_HEDGE] < 40); /* the hedge itself is seen: its own veil not counted */
        for (int r = 0; r < 4; ++r) cells[r * 10 + 4] = TEST_TILES_TILE_THICKET; /* a thicket: 8 points through it cost 32 */
        dgx_ge_crowd_play(&c, 450, NULL, NULL);
        CHECK(p[TEST_LOOKOUT_PARAM_D] == 50 && p[TEST_LOOKOUT_PARAM_FAR] == -1); /* within 100 as 74, not within 60; d is the distance */
        dgx_ge_crowd_stop(&c);
        dgx_ge_tileset_close(&tiles);
    }

    /* the pen: a walker goes right to the bound, turns there (its contact with the wall), goes left to the other, and so on */
    CHECK(open_crowd(&c, &pack, TEST_BLOB_PEN) && dgx_ge_crowd_start(&c, &pack, &kinds, NULL, 0));
    int turns = 0, low = 1000, high = -1000;
    bool facing = false;
    for (int u = 0; u < 5000; u += 20) {
        dgx_ge_crowd_play(&c, (uint32_t)u, NULL, NULL);
        const dgx_ge_actor_t *a = &c.members[0].actor;
        if (a->flip != facing) ++turns, facing = a->flip;
        if (dgx_ge_actor_x(a) < low) low = dgx_ge_actor_x(a);
        if (dgx_ge_actor_x(a) > high) high = dgx_ge_actor_x(a);
    }
    /* asked only whether a point is solid (a ray may ask so): beyond the bounds yes, inside no; nothing written */
    CHECK(c.terrain.touches(&c.members[0].actor, 70 * 256, 20 * 256, NULL, c.terrain.ctx));
    CHECK(!c.terrain.touches(&c.members[0].actor, 30 * 256, 20 * 256, NULL, c.terrain.ctx));
    CHECK(turns == 4); /* 5 s at 50 points a second over a pen 60 wide, less its body (3): a leg in 1.1 s */
    CHECK(low >= 0 && low <= 3 && high >= 56 && high < 60); /* it went as far as each wall, never into one */
    dgx_ge_crowd_stop(&c);
    (void)sounds;

    dgx_ge_scene_destroy(&scene);
    dgx_ge_frame_destroy(&frame);
    dgx_screen_destroy(&display);
    dgx_ge_kinds_free(&kinds);
    free(data);
    CHECK_DONE();
}
