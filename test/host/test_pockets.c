/* Kinds and pockets: stacks, what fits, moving, drawing and stowing by scripts, shooting what one has, laying into the world. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "dgx_ge_pack.h"
#include "dgx_ge_pockets.h"
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

/* A wall from x 115 on. */
static bool no_wall(int x, int y, void *ctx)
{
    (void)y, (void)ctx;
    return x < 115;
}

static int shots;
static void said(dgx_ge_actor_t *a, dgx_ge_anim_say_t what, uint8_t value, uint32_t at_ms, void *ctx)
{
    (void)a, (void)value, (void)at_ms, (void)ctx;
    if (what == DGX_GE_ANIM_SHOOT) ++shots;
}

int main(int argc, char **argv)
{
    size_t   size = 0;
    uint8_t *data = read_file(argc > 1 ? argv[1] : "build/test.pack", &size);
    dgx_ge_pack_t pack;
    CHECK(data && dgx_ge_pack_open(&pack, data, size));
    CHECK(sizeof(dgx_ge_pocket_t) == 8);

    /* kinds: opened once, by the number of the blob; what the header says of them */
    dgx_ge_kinds_t kinds;
    CHECK(dgx_ge_kinds_init(&kinds, &pack, 16));
    const dgx_ge_anim_t *knife = dgx_ge_kind(&kinds, TEST_BLOB_KNIFE), *carrier = dgx_ge_kind(&kinds, TEST_BLOB_CARRIER);
    CHECK(knife && carrier && dgx_ge_kind(&kinds, TEST_BLOB_KNIFE) == knife);
    /* as many places as the game says: one more kind is refused (and said once), the ones there stay */
    dgx_ge_kinds_t one;
    CHECK(dgx_ge_kinds_init(&one, &pack, 1) && dgx_ge_kind(&one, TEST_BLOB_KNIFE));
    CHECK(!dgx_ge_kind(&one, TEST_BLOB_CARRIER) && one.told && dgx_ge_kind(&one, TEST_BLOB_KNIFE) == &one.anims[0]);
    dgx_ge_kinds_free(&one);
    CHECK(!dgx_ge_kinds_init(&one, &pack, 0));
    CHECK(knife->id == TEST_BLOB_KNIFE && knife->stack == 5 && knife->pockets == 0);
    CHECK(carrier->pockets == 3 && carrier->stack == 1);
    CHECK(dgx_ge_kind(&kinds, TEST_BLOB_LEVEL) == NULL && dgx_ge_kind(&kinds, 999) == NULL); /* no animation, no blob */

    /* stacks: five plain knives to a place; a poisoned one is not one of them */
    dgx_ge_pockets_t *p = dgx_ge_pockets_create(&kinds, carrier->pockets, DGX_GE_ANIM_CARRIED);
    CHECK(p && p->capacity == 3);
    const dgx_ge_pocket_t plain = {.kind = TEST_BLOB_KNIFE, .script = TEST_KNIFE_SCRIPT_HELD}, poisoned = {.kind = TEST_BLOB_KNIFE, .script = TEST_KNIFE_SCRIPT_HELD, .params = {1}};
    CHECK(dgx_ge_pockets_fits(p, &plain, 20) == 15);
    CHECK(dgx_ge_pockets_put(p, &plain, 7) == 7 && dgx_ge_pockets_count(p, TEST_BLOB_KNIFE) == 7);
    CHECK(p->places[0].count == 5 && p->places[1].count == 2 && p->places[2].count == 0);
    CHECK(dgx_ge_pockets_fits(p, &plain, 20) == 8 && dgx_ge_pockets_fits(p, &poisoned, 20) == 5);
    CHECK(dgx_ge_pockets_put(p, &poisoned, 1) == 1 && p->places[2].count == 1 && p->places[2].params[0] == 1);
    CHECK(dgx_ge_pockets_put(p, &plain, 9) == 3 && p->places[1].count == 5);  /* only into its own stacks now */
    CHECK(dgx_ge_pockets_fits(p, &plain, 1) == 0 && dgx_ge_pockets_put(p, &poisoned, 9) == 4);
    dgx_ge_pocket_t out;
    CHECK(dgx_ge_pockets_take(p, 0, 2, &out) == 2 && out.count == 2 && out.kind == TEST_BLOB_KNIFE && p->places[0].count == 3);
    CHECK(dgx_ge_pockets_take(p, 0, 9, NULL) == 3 && p->places[0].count == 0 && dgx_ge_pockets_take(p, 0, 1, NULL) == 0);
    CHECK(dgx_ge_pockets_find(p, TEST_BLOB_KNIFE) == 1 && dgx_ge_pockets_count(p, TEST_BLOB_KNIFE) == 10);
    /* in a pocket the state is of no matter: a knife picked up lying goes onto the stack of those held */
    dgx_ge_pockets_t     *belt   = dgx_ge_pockets_create(&kinds, 2, 0);
    const dgx_ge_pocket_t picked = {.kind = TEST_BLOB_KNIFE, .script = TEST_KNIFE_SCRIPT_LYING};
    CHECK(belt && !belt->hands && dgx_ge_pockets_put(belt, &plain, 2) == 2 && dgx_ge_pockets_put(belt, &picked, 1) == 1);
    CHECK(belt->places[0].count == 3 && belt->places[1].count == 0 && belt->places[0].script == TEST_KNIFE_SCRIPT_HELD);
    dgx_ge_pockets_destroy(&belt);

    /* looting: as many as fit go, the rest stays where it was */
    dgx_ge_pockets_t *mine = dgx_ge_pockets_create(&kinds, 1, 0);
    CHECK(dgx_ge_pockets_put(mine, &plain, 3) == 3);
    CHECK(dgx_ge_pockets_move(p, 1, 5, mine, -1, 0) == 2 && mine->places[0].count == 5 && p->places[1].count == 3);
    CHECK(dgx_ge_pockets_move(p, 2, 5, mine, -1, 0) == 0 && p->places[2].count == 5); /* poisoned ones: no place for them */
    dgx_ge_pockets_destroy(&mine);
    CHECK(mine == NULL);

    /* drawn by a script, held as an actor of the pockets; stowed again, a record */
    dgx_ge_pockets_clear(p);
    CHECK(dgx_ge_pockets_put(p, &poisoned, 1) == 1);
    dgx_ge_actor_t c;
    dgx_ge_actor_start(&c, carrier, TEST_CARRIER_SCRIPT_ARM, 10, 20, 1000);
    c.pockets = p; /* the start makes no step: the advance does, with them */
    dgx_ge_actor_advance(&c, 1000, NULL, NULL);
    dgx_ge_actor_t *held = dgx_ge_actor_held(&c, TEST_POINT_HAND);
    CHECK(held == &p->hands[0] && held->anim == knife && held->script == TEST_KNIFE_SCRIPT_HELD && held->params[TEST_KNIFE_PARAM_POISON] == 1);
    CHECK(held->masks == 1u << 2 && held->image == TEST_IMG_EIGHT); /* its first steps made: it is what it is in a hand */
    CHECK(dgx_ge_pockets_count(p, TEST_BLOB_KNIFE) == 0);
    dgx_ge_actor_advance(&c, 1200, NULL, NULL); /* stowed */
    CHECK(!dgx_ge_actor_held(&c, TEST_POINT_HAND) && !p->hands[0].holder && dgx_ge_pockets_count(p, TEST_BLOB_KNIFE) == 1);
    CHECK(p->places[0].params[0] == 1 && p->places[0].script == TEST_KNIFE_SCRIPT_HELD); /* as it was: still poisoned */

    /* a hand that holds does not draw again; with nothing there nothing is drawn */
    CHECK(dgx_ge_pockets_put(p, &plain, 1) == 1);
    CHECK(dgx_ge_pockets_draw(&c, TEST_POINT_HAND, TEST_BLOB_KNIFE, 1300) && !dgx_ge_pockets_draw(&c, TEST_POINT_HAND, TEST_BLOB_KNIFE, 1300));
    CHECK(dgx_ge_pockets_count(p, TEST_BLOB_KNIFE) == 1);
    CHECK(dgx_ge_pockets_stow(&c, TEST_POINT_HAND) && !dgx_ge_pockets_stow(&c, TEST_POINT_HAND));
    CHECK(!dgx_ge_pockets_draw(&c, TEST_POINT_HAND, TEST_BLOB_CARRIER, 1300));

    /* shooting what one has: two knives, two shots, then count says none and he goes idle */
    dgx_ge_pockets_clear(p);
    CHECK(dgx_ge_pockets_put(p, &plain, 2) == 2);
    dgx_ge_actor_start(&c, carrier, TEST_CARRIER_SCRIPT_THROW, 10, 20, 2000);
    c.pockets = p;
    shots     = 0;
    dgx_ge_actor_advance(&c, 2500, said, NULL);
    CHECK(shots == 2 && c.script == TEST_CARRIER_SCRIPT_IDLE && dgx_ge_pockets_count(p, TEST_BLOB_KNIFE) == 0);

    /* without pockets: nothing to draw, count is 0, shooting as it was (endless) */
    dgx_ge_actor_t free_hand;
    dgx_ge_actor_start(&free_hand, carrier, TEST_CARRIER_SCRIPT_TWICE, 0, 0, 0);
    dgx_ge_actor_advance(&free_hand, 100, NULL, NULL);
    CHECK(!dgx_ge_actor_held(&free_hand, TEST_POINT_HAND)); /* draw without pockets: nothing */

    /* laid into the world: dropped, it lies there, kept; the place gives it up */
    dgx_ge_world_t *w = dgx_ge_world_create(2);
    dgx_ge_pockets_clear(p);
    CHECK(dgx_ge_pockets_put(p, &plain, 2) == 2);
    const int32_t h = dgx_ge_pockets_lay(p, 0, 1, w, 7, 50, 60, true, TEST_KNIFE_EVENT_DROPPED, 3000, NULL);
    dgx_ge_object_t o;
    CHECK(h >= 0 && dgx_ge_world_get(w, h, &o) && o.scene == 7 && o.kind == TEST_BLOB_KNIFE && o.keep && o.flip);
    CHECK(o.script == TEST_KNIFE_SCRIPT_LYING && o.x == 50 * 256 && o.y == 60 * 256 && o.since_ms == 3000 && p->places[0].count == 1);
    CHECK(dgx_ge_pockets_lay(p, 0, 1, w, 7, 0, 0, false, TEST_KNIFE_EVENT_DROPPED, 3000, NULL) >= 0 && p->places[0].count == 0);
    CHECK(dgx_ge_pockets_put(p, &plain, 1) == 1);
    const int32_t held_out = dgx_ge_pockets_lay(p, 0, 1, w, 7, 0, 0, false, -1, 3000, NULL); /* no event: held, not kept, nor any room left */
    CHECK(held_out < 0 && p->places[0].count == 1);
    dgx_ge_world_clear(w);
    CHECK(dgx_ge_pockets_lay(p, 0, 1, w, 7, 0, 0, false, -1, 3000, NULL) >= 0 && dgx_ge_world_get(w, dgx_ge_world_next(w, 7, -1), &o) && !o.keep);
    /* the world full of kept ones: nothing laid, it stays in the pocket */
    CHECK(dgx_ge_pockets_put(p, &plain, 3) == 3);
    dgx_ge_world_clear(w);
    CHECK(dgx_ge_pockets_lay(p, 0, 1, w, 7, 0, 0, false, TEST_KNIFE_EVENT_DROPPED, 3000, NULL) >= 0);
    CHECK(dgx_ge_pockets_lay(p, 0, 1, w, 7, 0, 0, false, TEST_KNIFE_EVENT_DROPPED, 3000, NULL) >= 0);
    CHECK(dgx_ge_pockets_lay(p, 0, 1, w, 7, 0, 0, false, TEST_KNIFE_EVENT_DROPPED, 3000, NULL) < 0 && p->places[0].count == 1);
    dgx_ge_world_destroy(&w);

    /* an event through the kind's matrix: a knife lying, picked, is one held */
    dgx_ge_pocket_t lying_one = {.kind = TEST_BLOB_KNIFE, .script = TEST_KNIFE_SCRIPT_LYING, .count = 3};
    dgx_ge_pocket_event(&kinds, &lying_one, TEST_KNIFE_EVENT_PICKED, 3500);
    CHECK(lying_one.script == TEST_KNIFE_SCRIPT_HELD && lying_one.count == 3);
    dgx_ge_pocket_event(&kinds, &lying_one, TEST_KNIFE_EVENT_PICKED, 3500); /* means nothing held: as it was */
    CHECK(lying_one.script == TEST_KNIFE_SCRIPT_HELD);

    /* laying down: the same in a stack, others side by side, no room: a heap; a heap searched is tidied */
    dgx_ge_world_t *f = dgx_ge_world_create(16);
    CHECK(dgx_ge_world_boxes(f, &kinds, 2, 3));
    const dgx_ge_lay_t how = {.step = 10, .reach = 20, .heap = TEST_BLOB_HEAP, .can_lie = no_wall};
    const dgx_ge_pocket_t knife_with[6] = {{.kind = TEST_BLOB_KNIFE, .script = TEST_KNIFE_SCRIPT_HELD},
                                           {.kind = TEST_BLOB_KNIFE, .script = TEST_KNIFE_SCRIPT_HELD, .params = {1}},
                                           {.kind = TEST_BLOB_KNIFE, .script = TEST_KNIFE_SCRIPT_HELD, .params = {2}},
                                           {.kind = TEST_BLOB_KNIFE, .script = TEST_KNIFE_SCRIPT_HELD, .params = {3}},
                                           {.kind = TEST_BLOB_KNIFE, .script = TEST_KNIFE_SCRIPT_HELD, .params = {4}},
                                           {.kind = TEST_BLOB_KNIFE, .script = TEST_KNIFE_SCRIPT_HELD, .params = {5}}};
    const int32_t first = dgx_ge_world_lay(f, &kinds, &knife_with[0], 1, 4, 100, 50, false, TEST_KNIFE_EVENT_DROPPED, 4000, &how);
    CHECK(dgx_ge_world_lay(f, &kinds, &knife_with[0], 1, 4, 100, 50, false, TEST_KNIFE_EVENT_DROPPED, 4100, &how) == first);
    dgx_ge_object_t r;
    CHECK(dgx_ge_world_get(f, first, &r) && r.count == 2 && r.script == TEST_KNIFE_SCRIPT_LYING && r.x == 100 * 256 && count_of(f, 4) == 1);
    dgx_ge_actor_t shown;
    dgx_ge_object_actor(&r, knife, &shown);
    dgx_ge_actor_advance(&shown, 4100, NULL, NULL);
    CHECK(shown.amount == 2 && shown.image == TEST_IMG_EIGHT + 1); /* two lying: their frame */
    const int32_t poisoned_at = dgx_ge_world_lay(f, &kinds, &knife_with[1], 1, 4, 100, 50, false, TEST_KNIFE_EVENT_DROPPED, 4200, &how);
    CHECK(poisoned_at != first && dgx_ge_world_get(f, poisoned_at, &r) && r.x == 110 * 256 && r.count == 1); /* beside, a step right */
    CHECK(dgx_ge_world_lay(f, &kinds, &knife_with[1], 2, 4, 100, 50, false, TEST_KNIFE_EVENT_DROPPED, 4300, &how) == poisoned_at);
    CHECK(dgx_ge_world_get(f, poisoned_at, &r) && r.count == 3);                       /* onto its own stack, within reach */
    const int32_t left_one = dgx_ge_world_lay(f, &kinds, &knife_with[2], 1, 4, 100, 50, false, TEST_KNIFE_EVENT_DROPPED, 4400, &how);
    CHECK(dgx_ge_world_get(f, left_one, &r) && r.x == 90 * 256);                       /* a step left */
    const int32_t far_left = dgx_ge_world_lay(f, &kinds, &knife_with[3], 1, 4, 100, 50, false, TEST_KNIFE_EVENT_DROPPED, 4500, &how);
    CHECK(dgx_ge_world_get(f, far_left, &r) && r.x == 80 * 256);                       /* two right is a wall: two left */
    const int32_t heap = dgx_ge_world_lay(f, &kinds, &knife_with[4], 1, 4, 100, 50, false, TEST_KNIFE_EVENT_DROPPED, 4600, &how);
    dgx_ge_pockets_t *box = dgx_ge_world_box(f, heap);
    CHECK(dgx_ge_world_get(f, heap, &r) && r.kind == TEST_BLOB_HEAP && r.box && r.keep && r.x == 100 * 256 && box);
    CHECK(!dgx_ge_world_get(f, first, &r) && count_of(f, 4) == 4);                     /* the two that lay there are in it */
    CHECK(dgx_ge_pockets_count(box, TEST_BLOB_KNIFE) == 3 && box->places[0].count == 2 && box->places[0].script == TEST_KNIFE_SCRIPT_LYING);
    CHECK(dgx_ge_world_lay(f, &kinds, &knife_with[5], 1, 4, 100, 50, false, TEST_KNIFE_EVENT_DROPPED, 4700, &how) == heap); /* into the heap */
    CHECK(dgx_ge_world_lay(f, &kinds, &knife_with[0], 1, 4, 100, 50, false, TEST_KNIFE_EVENT_DROPPED, 4700, &how) == heap); /* its stack in it */
    CHECK(box->places[0].count == 3 && dgx_ge_world_lay(f, &kinds, &knife_with[3], 9, 4, 100, 50, false, -1, 4700, &how) < 0); /* full */
    CHECK(dgx_ge_world_box(f, left_one) == NULL);
    /* searched: one thing left, the heap is it; none left, it is gone */
    dgx_ge_pockets_take(box, 1, 9, NULL);
    dgx_ge_pockets_take(box, 2, 9, NULL);
    dgx_ge_world_tidy(f, left_one, TEST_BLOB_HEAP, 4800); /* no heap: nothing */
    CHECK(dgx_ge_world_get(f, left_one, &r) && r.kind == TEST_BLOB_KNIFE);
    dgx_ge_world_tidy(f, heap, TEST_BLOB_HEAP, 4800);
    CHECK(dgx_ge_world_get(f, heap, &r) && r.kind == TEST_BLOB_KNIFE && r.count == 3 && !r.box && r.x == 100 * 256 && r.since_ms == 4800);
    const int32_t heap2 = dgx_ge_world_lay(f, &kinds, &knife_with[4], 1, 4, 100, 50, false, TEST_KNIFE_EVENT_DROPPED, 4900, &how);
    CHECK(dgx_ge_world_get(f, heap2, &r) && r.kind == TEST_BLOB_HEAP && dgx_ge_pockets_count(dgx_ge_world_box(f, heap2), TEST_BLOB_KNIFE) == 4);
    dgx_ge_pockets_clear(dgx_ge_world_box(f, heap2));
    dgx_ge_world_tidy(f, heap2, TEST_BLOB_HEAP, 5000);
    CHECK(!dgx_ge_world_get(f, heap2, &r) && dgx_ge_world_new_box(f) == 1); /* gone, its box free again */
    /* a heap removed with things in it: a new one gets its box empty */
    CHECK(dgx_ge_world_lay(f, &kinds, &knife_with[4], 1, 4, 100, 50, false, TEST_KNIFE_EVENT_DROPPED, 5100, &how) >= 0); /* lies at 100 */
    const int32_t heap3 = dgx_ge_world_lay(f, &kinds, &knife_with[5], 1, 4, 100, 50, false, TEST_KNIFE_EVENT_DROPPED, 5100, &how);
    CHECK(dgx_ge_world_get(f, heap3, &r) && r.kind == TEST_BLOB_HEAP && dgx_ge_pockets_count(dgx_ge_world_box(f, heap3), TEST_BLOB_KNIFE) == 2);
    dgx_ge_world_remove(f, heap3);
    CHECK(dgx_ge_world_lay(f, &kinds, &knife_with[0], 1, 4, 100, 50, false, TEST_KNIFE_EVENT_DROPPED, 5200, &how) >= 0); /* lies at 100 */
    const int32_t heap4 = dgx_ge_world_lay(f, &kinds, &knife_with[4], 1, 4, 100, 50, false, TEST_KNIFE_EVENT_DROPPED, 5200, &how);
    CHECK(dgx_ge_world_get(f, heap4, &r) && r.kind == TEST_BLOB_HEAP && dgx_ge_pockets_count(dgx_ge_world_box(f, heap4), TEST_BLOB_KNIFE) == 2);
    dgx_ge_world_destroy(&f);

    /* kept is of the script: lying it is, picked up into a hand it is not */
    dgx_ge_actor_t k;
    dgx_ge_actor_start(&k, knife, TEST_KNIFE_SCRIPT_LYING, 0, 0, 4000);
    dgx_ge_actor_advance(&k, 4000, NULL, NULL);
    dgx_ge_object_t ko = {0};
    dgx_ge_object_of(&ko, &k);
    CHECK(k.keep && ko.keep);
    dgx_ge_actor_event(&k, TEST_KNIFE_EVENT_PICKED, 4100, NULL, NULL);
    dgx_ge_object_of(&ko, &k);
    CHECK(k.script == TEST_KNIFE_SCRIPT_HELD && !k.keep && !ko.keep);

    dgx_ge_pockets_destroy(&p);
    CHECK(p == NULL);
    dgx_ge_kinds_free(&kinds);
    free(data);
    CHECK_DONE();
}
