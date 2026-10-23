/* Collisions by masks: where two masks meet, in areas of tiles; mirrored; bodies by active indices. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dgx_ge_anim.h"
#include "dgx_ge_mask.h"
#include "check.h"

static uint8_t full8[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static uint8_t sides[8] = {0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3}; /* two columns at each side */
static uint8_t left1[1] = {0x80};                                           /* one point, the leftmost */
static uint8_t strip[3] = {0xFF, 0xFF, 0xFF};                                /* 24 x 1 */

static int pairs, last_a, last_b, last_n;

static int            touch_n;
static unsigned       touch_bits;
static dgx_ge_touch_t last_touch;
static void           touched(const dgx_ge_touch_t *t, void *ctx)
{
    (void)ctx;
    ++touch_n;
    touch_bits |= t->from;
    last_touch = *t;
}

static void met(int a, int b, const dgx_ge_area_t *areas, int n, void *ctx)
{
    (void)areas, (void)ctx;
    ++pairs, last_a = a, last_b = b, last_n = n;
}

static void met_bodies(int ha, int a, int hb, int b, const dgx_ge_area_t *areas, int n, void *ctx)
{
    (void)ha, (void)hb, (void)a, (void)b, (void)areas, (void)n, (void)ctx;
    ++pairs;
}

/* A body counts when it is an actor. */
static bool is_actor(int handle, const struct dgx_ge_actor *actor, void *ctx)
{
    return actor != NULL;
}

int main(void)
{
    const dgx_ge_mask_t square = {.index = 1, .x = 0, .y = 0, .width = 8, .height = 8, .bits = full8};
    const dgx_ge_mask_t twin   = {.index = 2, .x = 0, .y = 0, .width = 8, .height = 8, .bits = sides};
    dgx_ge_area_t       areas[DGX_GE_AREAS_MAX];

    /* two squares overlapping at a corner of 4 x 4: one area, 16 points, its middle in the corner */
    int n = dgx_ge_masks_overlap(&square, 10, 10, false, &square, 14, 14, false, 8, areas);
    CHECK(n == 1 && areas[0].points == 16);
    CHECK(areas[0].box.x == 14 && areas[0].box.y == 14 && areas[0].box.w == 4 && areas[0].box.h == 4);
    CHECK(areas[0].cx >= 14 && areas[0].cx <= 17 && areas[0].cy >= 14 && areas[0].cy <= 17);
    CHECK(dgx_ge_masks_overlap(&square, 0, 0, false, &square, 8, 0, false, 8, areas) == 0); /* side by side, not meeting */

    /* a mask of two strips on a square: two areas of tiles of 2, 16 points each, left and right */
    n = dgx_ge_masks_overlap(&twin, 0, 0, false, &square, 0, 0, false, 2, areas);
    CHECK(n == 2 && areas[0].points == 16 && areas[1].points == 16);
    CHECK((areas[0].box.x == 0 && areas[1].box.x == 6) || (areas[0].box.x == 6 && areas[1].box.x == 0));
    /* with tiles of 8 the strips are one tile: one area of 32 */
    CHECK(dgx_ge_masks_overlap(&twin, 0, 0, false, &square, 0, 0, false, 8, areas) == 1 && areas[0].points == 32);

    /* mirrored about the column of the origin: a point 2 left of it is 2 right of it */
    const dgx_ge_mask_t dot  = {.index = 3, .x = -2, .y = 0, .width = 1, .height = 1, .bits = left1};
    const dgx_ge_mask_t line = {.index = 4, .x = 0, .y = 0, .width = 24, .height = 1, .bits = strip};
    CHECK(dgx_ge_masks_overlap(&dot, 10, 5, false, &line, 0, 5, false, 1, areas) == 1 && areas[0].cx == 8);
    CHECK(dgx_ge_masks_overlap(&dot, 10, 5, true, &line, 0, 5, false, 1, areas) == 1 && areas[0].cx == 12);

    /* a wide overlap is cut into no more than 16 x 16 tiles: tiles grow, one area still */
    static uint8_t big[64 * 8];
    memset(big, 0xFF, sizeof(big));
    const dgx_ge_mask_t wide = {.index = 5, .x = 0, .y = 0, .width = 64, .height = 64, .bits = big};
    CHECK(dgx_ge_masks_overlap(&wide, 0, 0, false, &wide, 0, 0, false, 1, areas) == 1 && areas[0].points == 64 * 64);

    /* bodies of the test pack: only active masks meet */
    size_t   size = 0;
    FILE    *f    = fopen("build/test.pack", "rb");
    uint8_t *data = NULL;
    if (f) {
        fseek(f, 0, SEEK_END);
        size = (size_t)ftell(f);
        fseek(f, 0, SEEK_SET);
        data = malloc(size);
        if (fread(data, 1, size, f) != size) size = 0;
        fclose(f);
    }
    dgx_ge_pack_t pack;
    CHECK(data && dgx_ge_pack_open(&pack, data, size));
    /* FOUR: body (1) 3 x 2 from (0, -1), blade (2) 2 x 1 at (3, -1); ONE: body 9 x 2 from (-4, -1) */
    dgx_ge_body_t a = {&pack, 1, 20, 20, false, 1u << 1 | 1u << 2}, b = {&pack, 0, 27, 20, false, 1u << 1};
    pairs = 0;
    /* the blade of a at x 23..24, the body of b from 23: they meet; the body of a at 20..22 does not */
    CHECK(dgx_ge_bodies_overlap(&a, &b, 4, met, NULL) == 1 && pairs == 1 && last_a == 2 && last_b == 1 && last_n == 1);
    a.active = 1u << 1; /* the blade not active: nothing */
    CHECK(dgx_ge_bodies_overlap(&a, &b, 4, met, NULL) == 0);
    /*
     * Bodies by what has changed: a (FOUR) and b (ONE) meet blade on body; c far
     * away; d of the owner of a, on a, never meets it.
     */
    dgx_ge_collisions_t *col = dgx_ge_collisions_create(8, 4);
    CHECK(col != NULL);
    const dgx_ge_body_t far = {&pack, 0, 200, 200, false, 1u << 1};
    a.active = 1u << 1 | 1u << 2;
    dgx_ge_collisions_set(col, 0, &a, 1, 1);
    dgx_ge_collisions_set(col, 1, &b, 2, 2);
    dgx_ge_collisions_set(col, 2, &far, 3, 3);
    dgx_ge_collisions_set(col, 3, &a, 1, 1);
    pairs = 0;
    CHECK(dgx_ge_collisions_look(col, met_bodies, NULL) == 2 && pairs == 2); /* a and d each meet b; a and d, of one owner, not */
    CHECK(dgx_ge_collisions_look(col, met_bodies, NULL) == 0);               /* nothing changed: nothing looked at */

    /* the nearest body: b 3 points off; b's owner passed by, a and d at 10 alike: the lower handle; none within 5 */
    int ndx = 0, ndy = 0;
    CHECK(dgx_ge_collisions_nearest(col, 30, 20, 50, 0, NULL, NULL, &ndx, &ndy) == 1 && ndx == -3 && ndy == 0);
    CHECK(dgx_ge_collisions_nearest(col, 30, 20, 50, 2, NULL, NULL, &ndx, &ndy) == 0 && ndx == -10);
    CHECK(dgx_ge_collisions_nearest(col, 30, 20, 5, 2, NULL, NULL, NULL, NULL) == -1);
    /* by what it is: only a body that is an actor counts; the actor is forgotten with its body */
    dgx_ge_actor_t *who = (dgx_ge_actor_t *)(void *)&far; /* any address: the registry only keeps it */
    CHECK(dgx_ge_collisions_actor_of(col, 3) == NULL);
    dgx_ge_collisions_actor(col, 3, who);
    CHECK(dgx_ge_collisions_actor_of(col, 3) == who);
    CHECK(dgx_ge_collisions_nearest(col, 30, 20, 50, 0, is_actor, NULL, &ndx, &ndy) == 3 && ndx == -10);
    CHECK(dgx_ge_collisions_nearest(col, 200, 200, 300, 0, is_actor, NULL, NULL, NULL) == 3); /* far is nearer, but no actor */
    dgx_ge_collisions_remove(col, 3);
    CHECK(dgx_ge_collisions_actor_of(col, 3) == NULL && dgx_ge_collisions_nearest(col, 30, 20, 50, 0, is_actor, NULL, NULL, NULL) == -1);
    dgx_ge_collisions_set(col, 3, &a, 1, 1);
    dgx_ge_collisions_look(col, NULL, NULL); /* d back as it was: its pairs looked at once more, nothing changed after */
    dgx_ge_collisions_set(col, 1, &b, 2, 2);                              /* given again the same: still nothing */
    CHECK(dgx_ge_collisions_look(col, met_bodies, NULL) == 0);
    dgx_ge_body_t moved = b;
    moved.x             = 100;                                         /* b away: nothing meets it */
    dgx_ge_collisions_set(col, 1, &moved, 2, 2);
    CHECK(dgx_ge_collisions_look(col, met_bodies, NULL) == 0);
    dgx_ge_collisions_set(col, 1, &b, 2, 2);                              /* b back: it meets a and d again */
    CHECK(dgx_ge_collisions_look(col, met_bodies, NULL) == 2);
    dgx_ge_collisions_remove(col, 3);
    dgx_ge_collisions_set(col, 1, &moved, 2, 2);
    dgx_ge_collisions_set(col, 1, &b, 2, 2);
    CHECK(dgx_ge_collisions_look(col, met_bodies, NULL) == 1);                /* d is gone */
    /* the origin moved: the bodies go with it; given again where they now are, none has changed */
    dgx_ge_collisions_shift(col, 64, -8);
    dgx_ge_body_t a_moved = a, b_moved = b;
    a_moved.x -= 64, a_moved.y += 8, b_moved.x -= 64, b_moved.y += 8;
    dgx_ge_collisions_set(col, 0, &a_moved, 1, 1);
    dgx_ge_collisions_set(col, 1, &b_moved, 2, 2);
    CHECK(dgx_ge_collisions_look(col, met_bodies, NULL) == 0);
    moved = b_moved, moved.x += 100;
    dgx_ge_collisions_set(col, 1, &moved, 2, 2);
    dgx_ge_collisions_set(col, 1, &b_moved, 2, 2);
    CHECK(dgx_ge_collisions_look(col, met_bodies, NULL) == 1);                /* they still meet where they are */
    dgx_ge_collisions_destroy(&col);

    /*
     * Who meets whom by sides: the test pack lets a blade meet a body across
     * sides and a body a body within one. a's blade on b's body; their bodies
     * meet once b stands at 24.
     */
    dgx_ge_meets_t meets;
    CHECK(dgx_ge_pack_meets(&pack, &meets) && meets.foreign[2] == 1u << 1 && meets.foreign[1] == 1u << 2 && meets.own[1] == 1u << 1 &&
          meets.own[2] == 0);
    col = dgx_ge_collisions_create(4, 4);
    dgx_ge_collisions_meets(col, &meets);
    dgx_ge_collisions_set(col, 0, &a, 1, 1);
    dgx_ge_collisions_set(col, 1, &b, 2, 2);
    CHECK(dgx_ge_collisions_look(col, met_bodies, NULL) == 1);                /* sides apart: blade on body */
    dgx_ge_collisions_set(col, 1, &b, 2, 1);
    CHECK(dgx_ge_collisions_look(col, met_bodies, NULL) == 0);                /* one side: a blade does not cut its own */
    dgx_ge_body_t close = b;
    close.x             = 24;
    dgx_ge_collisions_set(col, 1, &close, 2, 1);
    CHECK(dgx_ge_collisions_look(col, met_bodies, NULL) == 1);                /* but bodies of one side bump */
    dgx_ge_collisions_set(col, 1, &close, 2, 2);
    CHECK(dgx_ge_collisions_look(col, met_bodies, NULL) == 0);                /* across sides bodies do not */
    dgx_ge_collisions_set(col, 1, &close, 1, 1);
    CHECK(dgx_ge_collisions_look(col, met_bodies, NULL) == 0);                /* of one owner: never */
    dgx_ge_collisions_set(col, 1, &close, 0, 1);
    CHECK(dgx_ge_collisions_look(col, met_bodies, NULL) == 1);                /* nobody's own any more, still of the side */
    dgx_ge_collisions_set(col, 0, &a, 1, 0);
    dgx_ge_collisions_set(col, 1, &close, 0, 0);
    CHECK(dgx_ge_collisions_look(col, met_bodies, NULL) == 0);                /* side 0 is nobody's, both: foreign */
    dgx_ge_collisions_set(col, 0, &a, 1, 1);
    dgx_ge_collisions_meets(col, NULL);
    CHECK(dgx_ge_collisions_look(col, met_bodies, NULL) == 1);                /* other rules, looked at anew: foreign all, body on body */
    dgx_ge_collisions_set(col, 1, &close, 0, 1);
    CHECK(dgx_ge_collisions_look(col, met_bodies, NULL) == 0);                /* own none */
    dgx_ge_collisions_destroy(&col);

    /*
     * A path through the bodies, as a blade would go (mask 2): a (FOUR) at 20,
     * its body 101 on row 19; b (ONE) at 27, its body from 23. The first met
     * along the path; the owner's passed by; one's own side by the meets
     * (foreign BLADE BODY, no own one).
     */
    dgx_ge_collisions_t *ray = dgx_ge_collisions_create(4, 4);
    dgx_ge_collisions_meets(ray, &meets);
    a.active = 1u << 1, b.active = 1u << 1;
    dgx_ge_collisions_set(ray, 0, &a, 1, 1);
    dgx_ge_collisions_set(ray, 1, &b, 2, 2);
    dgx_ge_ray_hit_t rh;
    CHECK(dgx_ge_collisions_ray(ray, 0, 19, 40, 19, 2, 9, 5, &rh) && rh.handle == 0 && rh.index == 1 && rh.x == 20 && rh.y == 19);
    CHECK(rh.along > 0.49f && rh.along < 0.51f);
    CHECK(dgx_ge_collisions_ray(ray, 0, 19, 40, 19, 2, 1, 5, &rh) && rh.handle == 1 && rh.x == 23); /* a is the owner's: passed by */
    CHECK(!dgx_ge_collisions_ray(ray, 0, 19, 40, 19, 2, 1, 2, &rh));                            /* b is of its side: no own blade on body */
    CHECK(!dgx_ge_collisions_ray(ray, 0, 19, 19, 19, 2, 9, 5, &rh));                            /* it stops short of them */
    CHECK(dgx_ge_collisions_ray(ray, 40, 19, 0, 19, 2, 9, 5, &rh) && rh.handle == 1 && rh.x == 31); /* from the right: b's last point first */
    CHECK(!dgx_ge_collisions_ray(ray, 0, 19, 40, 19, 3, 9, 5, &rh));                            /* a mask the meets do not name */
    dgx_ge_collisions_destroy(&ray);
    /* what a body touches among the bodies of a registry: standing on one (the point (10,8) over its (10,9)); not one of its owner; by reach */
    dgx_ge_collisions_t *reg = dgx_ge_collisions_create(4, 4);
    const dgx_ge_body_t  floor_body = {&pack, 1, 10, 10, false, 1u << 1}, stander = {&pack, 1, 10, 8, false, 1u << 1};
    dgx_ge_collisions_set(reg, 0, &floor_body, 2, 2);
    touch_n = 0, touch_bits = 0;
    CHECK(dgx_ge_collisions_touching(reg, &stander, 1, 1, -1, 1, touched, NULL) == 1 && touch_n == 1 && touch_bits == DGX_GE_TOUCH_BELOW);
    CHECK(last_touch.handle == 0 && last_touch.index == 1 && last_touch.mask == 1 && last_touch.area.points == 1 && last_touch.area.cx == 10 && last_touch.area.cy == 8);
    CHECK(dgx_ge_collisions_touching(reg, &stander, 2, 2, -1, 1, NULL, NULL) == 0); /* of one owner: one whole */
    CHECK(dgx_ge_collisions_touch_sides(reg, &stander, 1, 1, 2, 1) == 0);         /* its blade is not active */
    touch_n = 0;
    CHECK(dgx_ge_collisions_touching(reg, &stander, 1, 1, 1, 2, touched, NULL) == 1 && last_touch.area.points == 4); /* within 2 every point of it has one of the floor under it */
    dgx_ge_collisions_destroy(&reg);
    free(data);

    /* two masks of 256 x 256 one over the other: 65536 points meet, more than an area counts (it says as many as it can), its middle in the middle */
    static uint8_t      screen[256 * 32];
    memset(screen, 0xFF, sizeof(screen));
    const dgx_ge_mask_t whole = {.index = 0, .width = 256, .height = 256, .bits = screen};
    n = dgx_ge_masks_overlap(&whole, 0, 0, false, &whole, 0, 0, false, 8, areas);
    CHECK(n == 1 && areas[0].points == 0xFFFF && areas[0].box.w == 256 && areas[0].box.h == 256);
    CHECK(areas[0].cx >= 126 && areas[0].cx <= 128 && areas[0].cy >= 126 && areas[0].cy <= 128);
    /* in one tile of them all (a tile as large as the overlap): still met, the same middle */
    n = dgx_ge_masks_overlap(&whole, 0, 0, false, &whole, 0, 0, false, 256, areas);
    CHECK(n == 1 && areas[0].points == 0xFFFF && areas[0].cx >= 126 && areas[0].cx <= 128 && areas[0].cy >= 126 && areas[0].cy <= 128);
    CHECK_DONE();
}
