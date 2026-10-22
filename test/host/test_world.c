/* Objects of the world: a ring of records by scenes, and actors made of them again as they would be. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "dgx_ge_pack.h"
#include "dgx_ge_pockets.h"
#include "dgx_ge_world.h"
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

static int count_of(const dgx_ge_world_t *w, int scene)
{
    int n = 0;
    for (int32_t h = dgx_ge_world_next(w, scene, -1); h >= 0; h = dgx_ge_world_next(w, scene, h)) ++n;
    return n;
}

int main(int argc, char **argv)
{
    /* the ring: three places; the fourth record takes the place of the first */
    dgx_ge_world_t *w = dgx_ge_world_create(3);
    CHECK(w != NULL && sizeof(dgx_ge_object_t) == 28);
    dgx_ge_object_t o = {.scene = 5, .kind = 1}, got;
    const int32_t   a = dgx_ge_world_add(w, &o);
    o.kind            = 2;
    const int32_t b   = dgx_ge_world_add(w, &o);
    o.scene = 6, o.kind = 3;
    const int32_t c = dgx_ge_world_add(w, &o);
    CHECK(a >= 0 && b >= 0 && c >= 0 && a != b && b != c);
    CHECK(count_of(w, 5) == 2 && count_of(w, 6) == 1 && count_of(w, 7) == 0);
    CHECK(dgx_ge_world_next(w, 5, -1) == a && dgx_ge_world_next(w, 5, a) == b && dgx_ge_world_next(w, 5, b) == -1); /* oldest first */
    o.scene = 5, o.kind = 4;
    const int32_t d = dgx_ge_world_add(w, &o);
    CHECK(!dgx_ge_world_get(w, a, &got) && dgx_ge_world_get(w, d, &got) && got.kind == 4); /* a is lost to d, in its place */
    CHECK(dgx_ge_world_next(w, 5, -1) == b && dgx_ge_world_next(w, 5, b) == d);           /* still oldest first */
    CHECK(!dgx_ge_world_put(w, a, &o));                                                    /* a handle gone stays gone */
    /* removed while going over: the walk goes on past it */
    int seen = 0;
    for (int32_t h = dgx_ge_world_next(w, 5, -1); h >= 0; h = dgx_ge_world_next(w, 5, h)) {
        dgx_ge_world_remove(w, h);
        ++seen;
    }
    CHECK(seen == 2 && count_of(w, 5) == 0 && count_of(w, 6) == 1);
    /* only the middle one removed while going over: each still met once */
    dgx_ge_world_t *m = dgx_ge_world_create(4);
    o.scene           = 1;
    for (int i = 0; i < 3; ++i) dgx_ge_world_add(m, &o);
    seen = 0;
    for (int32_t h = dgx_ge_world_next(m, 1, -1); h >= 0 && seen < 10; h = dgx_ge_world_next(m, 1, h))
        if (++seen == 2) dgx_ge_world_remove(m, h);
    CHECK(seen == 3 && count_of(m, 1) == 2);
    dgx_ge_world_destroy(&m);
    o.scene = 6;
    for (int i = 0; i < 5; ++i) dgx_ge_world_add(w, &o);
    CHECK(count_of(w, 6) == 3 && !dgx_ge_world_get(w, c, &got)); /* full of the newest three */
    dgx_ge_world_clear(w);
    CHECK(count_of(w, 6) == 0);
    dgx_ge_world_destroy(&w);
    CHECK(w == NULL);

    /* kept records: the ring passes them over and gives up the oldest of the rest; all kept, nothing more goes in */
    dgx_ge_world_t *k = dgx_ge_world_create(3);
    dgx_ge_object_t sword = {.scene = 2, .kind = 7, .keep = true}, shard = {.scene = 2, .kind = 8};
    const int32_t   s1 = dgx_ge_world_add(k, &sword), g1 = dgx_ge_world_add(k, &shard), g2 = dgx_ge_world_add(k, &shard);
    const int32_t   g3 = dgx_ge_world_add(k, &shard);
    CHECK(g3 >= 0 && dgx_ge_world_get(k, s1, &got) && got.keep && !dgx_ge_world_get(k, g1, &got) && dgx_ge_world_get(k, g2, &got));
    const int32_t s2 = dgx_ge_world_add(k, &sword), s3 = dgx_ge_world_add(k, &sword);
    CHECK(s2 >= 0 && s3 >= 0 && !dgx_ge_world_get(k, g2, &got) && !dgx_ge_world_get(k, g3, &got));
    CHECK(dgx_ge_world_add(k, &shard) == -1 && dgx_ge_world_add(k, &sword) == -1 && count_of(k, 2) == 3);
    dgx_ge_world_remove(k, s2);
    CHECK(dgx_ge_world_add(k, &shard) >= 0 && count_of(k, 2) == 3);
    dgx_ge_world_destroy(&k);

    /* a free place first: of a, b, c, b removed, d takes its place and a is still there; full again, the oldest goes */
    dgx_ge_world_t *f = dgx_ge_world_create(3);
    o                 = (dgx_ge_object_t){.scene = 3};
    const int32_t fa = dgx_ge_world_add(f, &o), fb = dgx_ge_world_add(f, &o), fc = dgx_ge_world_add(f, &o);
    dgx_ge_world_remove(f, fb);
    const int32_t fd = dgx_ge_world_add(f, &o);
    CHECK(fd >= 0 && dgx_ge_world_get(f, fa, &got) && dgx_ge_world_get(f, fc, &got) && dgx_ge_world_get(f, fd, &got) && count_of(f, 3) == 3);
    const int32_t fe = dgx_ge_world_add(f, &o);
    CHECK(fe >= 0 && !dgx_ge_world_get(f, fa, &got) && dgx_ge_world_get(f, fc, &got) && dgx_ge_world_get(f, fd, &got));
    /* the same with a kept record in the way: a free place goes before a record not kept */
    dgx_ge_world_clear(f);
    const int32_t fk = dgx_ge_world_add(f, &sword), fs = dgx_ge_world_add(f, &shard), ft = dgx_ge_world_add(f, &shard);
    dgx_ge_world_remove(f, ft);
    CHECK(dgx_ge_world_add(f, &shard) >= 0 && dgx_ge_world_get(f, fk, &got) && dgx_ge_world_get(f, fs, &got));
    /* boxes that could not be made: none at all (no box missing among them), and they may be asked for again */
    CHECK(!dgx_ge_world_boxes(f, NULL, 2, 0) && dgx_ge_world_new_box(f) == 0);
    CHECK(dgx_ge_world_boxes(f, NULL, 2, 4) && dgx_ge_world_new_box(f) == 1);
    dgx_ge_world_destroy(&f);

    /*
     * Characters leaving with what they hold, more of them than boxes: the oldest not kept gives up its box and is
     * forgotten whole; the newest keeps its things. Every box held by a kept record: refused, nothing changed.
     */
    dgx_ge_world_t   *h = dgx_ge_world_create(3);
    dgx_ge_pockets_t *held = dgx_ge_pockets_create(NULL, 4, 0);
    CHECK(h && held && dgx_ge_world_boxes(h, NULL, 2, 4));
    const dgx_ge_pocket_t blade = {.kind = 7, .script = 1}, flask = {.kind = 8};
    const dgx_ge_object_t guard = {.scene = 4, .kind = 3};
    dgx_ge_pockets_put(held, &blade, 1);
    const int32_t ha = dgx_ge_world_add_holding(h, &guard, held, 0);
    CHECK(ha >= 0 && held->places[0].count == 0 && dgx_ge_world_box(h, ha) && dgx_ge_world_box(h, ha)->places[0].kind == 7);
    dgx_ge_pockets_put(held, &flask, 2);
    const int32_t hb = dgx_ge_world_add_holding(h, &guard, held, 0);
    CHECK(hb >= 0 && dgx_ge_world_box(h, hb) && dgx_ge_world_box(h, hb) != dgx_ge_world_box(h, ha));
    dgx_ge_pockets_put(held, &blade, 1);
    const int32_t hc = dgx_ge_world_add_holding(h, &guard, held, 0);
    dgx_ge_object_t was;
    CHECK(hc >= 0 && !dgx_ge_world_get(h, ha, &was) && dgx_ge_world_get(h, hb, &was) && held->places[0].count == 0);
    const dgx_ge_pockets_t *in = dgx_ge_world_box(h, hc);
    CHECK(in && in->places[0].kind == 7 && in->places[0].count == 1 && in->places[1].count == 0); /* its own, nothing of the forgotten */
    /* nothing held: no box needed */
    const int32_t hd = dgx_ge_world_add_holding(h, &guard, held, 0);
    CHECK(hd >= 0 && !dgx_ge_world_box(h, hd));
    /* every box held by a kept record (a heap, a chest): no box for a new one, and the world and its pockets as they were */
    dgx_ge_world_clear(h);
    dgx_ge_object_t chest = {.scene = 4, .kind = 5, .keep = true};
    chest.box = (uint8_t)dgx_ge_world_new_box(h);
    CHECK(dgx_ge_world_add(h, &chest) >= 0);
    chest.box = (uint8_t)dgx_ge_world_new_box(h);
    CHECK(dgx_ge_world_add(h, &chest) >= 0 && dgx_ge_world_new_box(h) == 0);
    dgx_ge_pockets_put(held, &blade, 1);
    CHECK(dgx_ge_world_add_holding(h, &guard, held, 0) == -1 && held->places[0].count == 1);
    dgx_ge_pockets_destroy(&held);
    dgx_ge_world_destroy(&h);

    /*
     * Kept and made again: an actor kept after it was sent into a script and
     * made from the record later is where the live one is, at any moment.
     */
    size_t   size = 0;
    uint8_t *data = read_file(argc > 1 ? argv[1] : "build/test.pack", &size);
    dgx_ge_pack_t pack;
    dgx_ge_anim_t anim;
    size_t        blob_size = 0;
    CHECK(data && dgx_ge_pack_open(&pack, data, size));
    const uint8_t *blob = dgx_ge_pack_blob(&pack, TEST_BLOB_HERO, &blob_size);
    CHECK(blob && dgx_ge_anim_open(&anim, blob, blob_size));
    dgx_ge_actor_t live, made;
    dgx_ge_actor_start(&live, &anim, TEST_HERO_SCRIPT_RUN, 30, 40, 1000);
    live.flip = true;
    dgx_ge_actor_advance(&live, 1120, NULL, NULL);
    live.params[TEST_HERO_PARAM_HP] = 10; /* not as it starts: the record keeps it */
    dgx_ge_actor_event(&live, TEST_HERO_EVENT_HIT, 1120, NULL, NULL); /* hurt: hp - 5, turned, moved */
    dgx_ge_actor_advance(&live, 1170, NULL, NULL);
    dgx_ge_object_t kept = {.scene = 9, .kind = 1};
    dgx_ge_object_of(&kept, &live);
    CHECK(kept.scene == 9 && kept.kind == 1 && kept.script == TEST_HERO_SCRIPT_HURT && kept.since_ms == 1120 && kept.flip);
    CHECK(kept.params[TEST_HERO_PARAM_HP] == 10 && live.params[TEST_HERO_PARAM_HP] == 5);
    dgx_ge_object_actor(&kept, &anim, &made);
    dgx_ge_actor_advance(&made, 1170, NULL, NULL);
    CHECK(made.x == live.x && made.y == live.y && made.flip == live.flip && made.image == live.image &&
          made.params[TEST_HERO_PARAM_HP] == live.params[TEST_HERO_PARAM_HP]);
    dgx_ge_actor_advance(&live, 9000, NULL, NULL);
    dgx_ge_object_actor(&kept, &anim, &made);
    dgx_ge_actor_advance(&made, 9000, NULL, NULL);
    CHECK(made.x == live.x && made.y == live.y && made.image == live.image && made.holding == live.holding);
    free(data);
    CHECK_DONE();
}
