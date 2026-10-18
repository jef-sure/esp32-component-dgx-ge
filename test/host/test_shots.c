/* Shots: a pool of records, places by a formula, the first met on the way (the terrain first), its kind's hit. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "dgx_ge_pack.h"
#include "dgx_ge_shots.h"
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

/* a wall from x 10 on */
static bool wall_touches(const dgx_ge_actor_t *a, int32_t x, int32_t y, dgx_ge_contact_t *c, void *ctx)
{
    (void)a, (void)ctx;
    if (x < 10 * 256) return false;
    c->x = 10 * 256, c->y = y, c->nx = -1, c->ny = 0, c->surface = 9;
    return true;
}

static int               hits;
static dgx_ge_shot_hit_t last;

static void met(dgx_ge_shots_t *s, int i, const dgx_ge_shot_hit_t *hit, void *ctx)
{
    (void)ctx;
    ++hits, last = *hit;
    dgx_ge_shots_remove(s, i);
}

int main(int argc, char **argv)
{
    size_t        size = 0, n;
    uint8_t      *data = read_file(argc > 1 ? argv[1] : "build/test.pack", &size);
    dgx_ge_pack_t pack;
    dgx_ge_anim_t dart;
    CHECK(data && dgx_ge_pack_open(&pack, data, size));
    const uint8_t *blob = dgx_ge_pack_blob(&pack, TEST_BLOB_DART, &n);
    CHECK(blob && dgx_ge_anim_open(&dart, blob, n));

    /* the kind as its first script says: picture, fall, mask */
    dgx_ge_shots_t *s = dgx_ge_shots_create(2);
    CHECK(s && dgx_ge_shots_kind(s, TEST_BLOB_DART, &dart) && sizeof(dgx_ge_shot_t) == 16);
    CHECK(s->kinds[0].image == TEST_IMG_EIGHT && s->kinds[0].ay == 100 && s->kinds[0].masks == 1u << 2);
    /* the pool: two, not three; an unknown kind none */
    CHECK(dgx_ge_shots_fire(s, TEST_BLOB_DART, 0, 19, 400, 0, 0, 5, 9) == 0);
    CHECK(dgx_ge_shots_fire(s, TEST_BLOB_DART, 0, 0, 0, -100, 0, 5, 9) == 1);
    CHECK(dgx_ge_shots_fire(s, TEST_BLOB_DART, 0, 0, 0, 0, 0, 5, 9) == -1 && dgx_ge_shots_fire(s, 999, 0, 0, 0, 0, 0, 5, 9) == -1);
    /* where: a line and a fall, 100 points a second a second */
    int x, y;
    dgx_ge_shots_at(s, 0, 500, &x, &y);
    CHECK(x == 200 && y == 31); /* 19 + 100 * 0.25 / 2 */
    dgx_ge_shots_at(s, 1, 1000, &x, &y);
    CHECK(x == 0 && y == -50); /* up 100, down 50 */
    dgx_ge_shots_remove(s, 1);

    /* bodies: a (FOUR, owner 1) at 20, 20, its body 101 on row 19; b (ONE, owner 2) at 27, its body from 23; a dart is a blade (2) */
    dgx_ge_meets_t meets;
    CHECK(dgx_ge_pack_meets(&pack, &meets));
    dgx_ge_collisions_t *bodies = dgx_ge_collisions_create(4, 4);
    dgx_ge_collisions_meets(bodies, &meets);
    const dgx_ge_body_t a = {&pack, TEST_IMG_FOUR, 20, 20, false, 1u << 1}, b = {&pack, TEST_IMG_ONE, 27, 20, false, 1u << 1};
    dgx_ge_collisions_set(bodies, 0, &a, 1, 1);
    dgx_ge_collisions_set(bodies, 1, &b, 2, 2);
    const dgx_ge_rect_t bounds = dgx_ge_rect(-100, -100, 300, 300);
    hits = 0;
    dgx_ge_shots_play(s, 100, NULL, bodies, bounds, met, NULL); /* 0 .. 40 along row 19 (it falls half a point) */
    CHECK(hits == 1 && last.handle == 0 && last.index == 1 && last.x == 20 && last.at_ms == 50 && s->shots[0].kind == 0xFF);
    /* its owner's body passed by: b */
    CHECK(dgx_ge_shots_fire(s, TEST_BLOB_DART, 0, 19, 400, 0, 100, 5, 1) >= 0);
    hits = 0;
    dgx_ge_shots_play(s, 200, NULL, bodies, bounds, met, NULL);
    CHECK(hits == 1 && last.handle == 1 && last.x == 23);
    /* a wall at 10 cuts the way before the bodies */
    const dgx_ge_terrain_t wall = {.touches = wall_touches};
    CHECK(dgx_ge_shots_fire(s, TEST_BLOB_DART, 0, 19, 400, 0, 200, 5, 9) >= 0);
    hits = 0;
    dgx_ge_shots_play(s, 300, &wall, bodies, bounds, met, NULL);
    CHECK(hits == 1 && last.handle == -1 && last.x == 10 && last.contact.surface == 9 && last.at_ms >= 224 && last.at_ms <= 226);
    /* out of the scene: gone, nothing met */
    const int far = dgx_ge_shots_fire(s, TEST_BLOB_DART, 0, -50, 0, -400, 300, 5, 9);
    hits = 0;
    dgx_ge_shots_play(s, 500, NULL, bodies, bounds, met, NULL);
    CHECK(far >= 0 && hits == 0 && !dgx_ge_shots_kind_of(s, far));
    /* what its kind does on meeting a body: damage 3, the event hit */
    const int i = dgx_ge_shots_fire(s, TEST_BLOB_DART, 0, 0, 0, 0, 500, 5, 9);
    dgx_ge_hit_t hit;
    CHECK(dgx_ge_shots_hit(s, i, 1, &hit) && hit.damage == 3 && hit.event == TEST_HERO_EVENT_HIT && !dgx_ge_shots_hit(s, i, 2, &hit));
    /* the origin moved: the shot with it */
    dgx_ge_shots_shift(s, 100, 0);
    dgx_ge_shots_at(s, i, 500, &x, &y);
    CHECK(x == -100 && y == 0);
    dgx_ge_shots_destroy(&s);
    dgx_ge_collisions_destroy(&bodies);
    free(data);
    CHECK_DONE();
}
