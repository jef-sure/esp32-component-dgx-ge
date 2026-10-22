/* Animation: frames by time, moves, what scripts tell, the three kinds of transitions, the motion. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "dgx_ge_anim.h"
#include "dgx_ge_mask.h"
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

static int      said_signal = -1, said_sound = -1, said_release = -1, releases, said_param = -1, params_changed, said_tell = -1, said_shot = -1;
static int      world_said[3][3], world_count; /* the world of the map: what, the cell, the value (or the event) */
static int      appeared, vanished, appear_at[4], appear_event; /* one called (x, y, kind, left), one gone */
static uint32_t said_at, sounds, param_at;

static void say(dgx_ge_actor_t *actor, dgx_ge_anim_say_t what, uint8_t value, uint32_t at_ms, void *ctx)
{
    (void)actor, (void)ctx;
    if (what == DGX_GE_ANIM_PARAM) {
        said_param = value, ++params_changed, param_at = at_ms;
        return;
    }
    if (what == DGX_GE_ANIM_SIGNAL) said_signal = value;
    else if (what == DGX_GE_ANIM_RELEASE) said_release = value, ++releases;
    else if (what == DGX_GE_ANIM_TELL) said_tell = value;
    else if (what == DGX_GE_ANIM_SHOOT) said_shot = value;
    else if (what == DGX_GE_ANIM_APPEAR) {
        ++appeared, appear_event = value;
        for (int i = 0; i < 4; ++i) appear_at[i] = dgx_ge_anim_said(i);
    } else if (what == DGX_GE_ANIM_VANISH) ++vanished;
    else if (what >= DGX_GE_ANIM_CELL && what <= DGX_GE_ANIM_CLEAR) {
        if (world_count < 3)
            world_said[world_count][0] = what, world_said[world_count][1] = dgx_ge_anim_said(0),
            world_said[world_count][2] = what == DGX_GE_ANIM_CELL ? value * 1000 + dgx_ge_anim_said(1) : dgx_ge_anim_said(1);
        ++world_count;
    }
    else said_sound = value, ++sounds;
    said_at = at_ms;
}

/* a terrain of a floor at y 200: a ball touches it with its lowest point, its origin */
static int contacts;

static bool floor_touches(const dgx_ge_actor_t *a, int32_t x, int32_t y, dgx_ge_contact_t *c, void *ctx)
{
    (void)a, (void)ctx;
    if (y < 200 * 256) return false;
    c->x = x, c->y = 200 * 256, c->nx = 0, c->ny = -1, c->surface = 7;
    return true;
}

/* bounce at the moment it touched, half as fast up */
static void floor_bounce(dgx_ge_actor_t *a, const dgx_ge_contact_t *found, void *ctx)
{
    (void)ctx;
    dgx_ge_contact_t c = *found;
    dgx_ge_contact_refine(a, &c);
    int32_t x, y, vx, vy;
    dgx_ge_actor_state_at(a, c.from_ms, &x, &y, &vx, &vy);
    dgx_ge_actor_restart(a, c.from_ms, x, y, vx, -vy / 2);
    ++contacts;
}

/* a handler that changes nothing: the actor put back at the moment it touched, as it was */
static void floor_same(dgx_ge_actor_t *a, const dgx_ge_contact_t *found, void *ctx)
{
    (void)ctx;
    dgx_ge_contact_t c = *found;
    dgx_ge_contact_refine(a, &c);
    int32_t x, y, vx, vy;
    dgx_ge_actor_state_at(a, c.from_ms, &x, &y, &vx, &vy);
    dgx_ge_actor_restart(a, c.from_ms, x, y, vx, vy);
    ++contacts;
}

/* a floor under x < 100 points, as the body mask (1) touches it from below wherever the actor is over it; and one from x 100 on */
static uint8_t edge_sides(const dgx_ge_actor_t *a, int32_t x, int32_t y, int mask, int reach, void *ctx)
{
    (void)a, (void)y, (void)reach, (void)ctx;
    return mask == 1 && x < 100 * 256 ? DGX_GE_TOUCH_BELOW : 0;
}

static uint8_t ledge_sides(const dgx_ge_actor_t *a, int32_t x, int32_t y, int mask, int reach, void *ctx)
{
    (void)a, (void)y, (void)reach, (void)ctx;
    return mask == 1 && x >= 100 * 256 ? DGX_GE_TOUCH_BELOW : 0;
}

/* a wall from x 150 on: the body mask (1) has it on its right once there */
static uint8_t wall_sides(const dgx_ge_actor_t *a, int32_t x, int32_t y, int mask, int reach, void *ctx)
{
    (void)a, (void)y, (void)reach, (void)ctx;
    return mask == 1 && x >= 150 * 256 ? DGX_GE_TOUCH_RIGHT : 0;
}

/* a slab from y 200 down: the body mask (1) a point over it touches it below (reach 1), in it is inside */
static uint8_t slab_sides(const dgx_ge_actor_t *a, int32_t x, int32_t y, int mask, int reach, void *ctx)
{
    (void)a, (void)x, (void)ctx;
    if (mask != 1) return 0;
    const int top = 200 * 256 - (reach > 0 ? reach : 0) * 256;
    return y >= 200 * 256 ? DGX_GE_TOUCH_INSIDE : y >= top ? DGX_GE_TOUCH_BELOW : 0;
}

static const dgx_ge_terrain_t edge_terrain  = {.sides = edge_sides};
static const dgx_ge_terrain_t slab_terrain  = {.sides = slab_sides};
static const dgx_ge_terrain_t wall_terrain  = {.sides = wall_sides};
static const dgx_ge_terrain_t ledge_terrain = {.sides = ledge_sides};

static const dgx_ge_terrain_t floor_terrain = {.touches = floor_touches, .contact = floor_bounce};
static const dgx_ge_terrain_t same_terrain  = {.touches = floor_touches, .contact = floor_same};
static const dgx_ge_terrain_t bare_floor    = {.touches = floor_touches}; /* what follows a contact is the script's */

/* The registry a sense of the heroes looks at; the looker is body 0, of owner 1. */
static dgx_ge_collisions_t *sense_registry;

static bool sense_heroes(const dgx_ge_actor_t *actor, const dgx_ge_sense_t *query, dgx_ge_sensed_t *found, void *ctx)
{
    return !query->cells && dgx_ge_sense_bodies(sense_registry, 1, actor, query, found);
}

/* appear ... with: a calf started where it is said, given its params */
static dgx_ge_actor_t calf;
static int            calves, calf_params;

static void breed_say(dgx_ge_actor_t *a, dgx_ge_anim_say_t what, uint8_t value, uint32_t at_ms, void *ctx)
{
    if (what != DGX_GE_ANIM_APPEAR) return;
    dgx_ge_actor_start(&calf, a->anim, TEST_HERO_SCRIPT_STAND, dgx_ge_anim_said(0), dgx_ge_anim_said(1), at_ms);
    calf_params = dgx_ge_anim_said_params(&calf);
    ++calves;
}

/* The clock's game: signal 1 answered with go at once, at the moment it is said; the frames told (add n 1) counted */
static int clock_frames;

static void clock_say(dgx_ge_actor_t *a, dgx_ge_anim_say_t what, uint8_t value, uint32_t at_ms, void *ctx)
{
    if (what == DGX_GE_ANIM_SIGNAL && value == 1) dgx_ge_actor_event(a, TEST_TIMING_EVENT_GO, at_ms, clock_say, ctx);
    if (what == DGX_GE_ANIM_PARAM) ++clock_frames;
}

/* The time of the scripts: motion that adds up however the calls come, events from its own say and from its past, a long absence lived through. */
static void clock_tests(const dgx_ge_pack_t *pack)
{
    size_t         n;
    const uint8_t *blob = dgx_ge_pack_blob(pack, TEST_BLOB_TIMING, &n);
    dgx_ge_anim_t  anim;
    CHECK(blob && dgx_ge_anim_open(&anim, blob, n));

    /* an eighth of a point a second, 100 s: 12.5 points by calls of 1, 16 and 100 ms, and in one */
    const uint32_t steps[] = {1, 16, 100, 100000};
    for (int k = 0; k < 4; ++k) {
        dgx_ge_actor_t a;
        dgx_ge_actor_start(&a, &anim, TEST_TIMING_SCRIPT_CREEP, 0, 0, 0);
        for (uint32_t t = 0; t <= 100000; t += steps[k]) dgx_ge_actor_advance(&a, t, NULL, NULL);
        CHECK(a.x == 3200);
        /* and from rest at an eighth of a point a second a second, 10 s: 6.25 points, 1.25 a second */
        dgx_ge_actor_start(&a, &anim, TEST_TIMING_SCRIPT_SLOW, 0, 0, 0);
        for (uint32_t t = 0; t <= 10000; t += steps[k] < 10000 ? steps[k] : 10000) dgx_ge_actor_advance(&a, t, NULL, NULL);
        CHECK(a.x == 1600 && a.vx == 320);
    }

    /* the game answers a signal with an event at once: the new script begins at the moment of the step, the rest of the step not made */
    dgx_ge_actor_t c;
    dgx_ge_actor_start(&c, &anim, TEST_TIMING_SCRIPT_CALLER, 0, 0, 0);
    dgx_ge_actor_advance(&c, 0, clock_say, NULL);
    CHECK(c.script == TEST_TIMING_SCRIPT_GONE && c.image == TEST_IMG_EIGHT && c.x == 0 && c.due_ms == 50 && c.params[TEST_TIMING_PARAM_N] == 5);
    dgx_ge_actor_advance(&c, 100, clock_say, NULL);
    CHECK(c.image == TEST_IMG_ONE && c.x == 10 * 256 && c.holding);

    /* an event at once from before the moment its motion has come to: the motion back to it, its frames and its place of one moment */
    dgx_ge_actor_start(&c, &anim, TEST_TIMING_SCRIPT_RUNNER, 0, 0, 0);
    dgx_ge_actor_advance(&c, 500, NULL, NULL);
    CHECK(dgx_ge_actor_event(&c, TEST_TIMING_EVENT_GO, 400, NULL, NULL));
    CHECK(c.script == TEST_TIMING_SCRIPT_GONE && c.entered_ms == 400 && c.entered_x == 8 * 256 && c.x == 8 * 256 && c.due_ms == 450 &&
          c.image == TEST_IMG_EIGHT);
    dgx_ge_actor_advance(&c, 500, NULL, NULL);
    CHECK(c.x == 20 * 256 && c.holding); /* 2 more points of its run, 10 of dx */

    /* half an hour away in one call: every one of its 10843 frames made, time is never lost (the clock of a game stands on a pause) */
    dgx_ge_actor_start(&c, &anim, TEST_TIMING_SCRIPT_TICK, 0, 0, 0);
    dgx_ge_actor_advance(&c, 0, clock_say, NULL);
    clock_frames = 0;
    dgx_ge_actor_advance(&c, 30 * 60 * 1000, clock_say, NULL);
    CHECK(clock_frames == 10843 && c.params[TEST_TIMING_PARAM_N] == 10844 && c.due_ms == 10844u * 166);
}

/* The questions of the test's game: far (0) is 21, near (1) what ctx says. */
static float answer(int what, void *ctx)
{
    return what == 0 ? 21.0f : what == 1 ? (float)*(const int *)ctx : 0.0f;
}

int main(int argc, char **argv)
{
    size_t        size = 0;
    uint8_t      *data = argc > 1 ? read_file(argv[1], &size) : NULL;
    dgx_ge_pack_t pack;
    CHECK(data && dgx_ge_pack_open(&pack, data, size));
    size_t         n;
    const uint8_t *blob = dgx_ge_pack_blob(&pack, TEST_BLOB_HERO, &n);
    dgx_ge_anim_t  anim;
    CHECK(blob && dgx_ge_anim_open(&anim, blob, n));
    CHECK(anim.scripts == 27 && anim.events == 6 && anim.params == 3);

    /* a run: frames at their times, the moves between them */
    dgx_ge_actor_t a;
    dgx_ge_actor_start(&a, &anim, TEST_HERO_SCRIPT_RUN, 10, 50, 0);
    dgx_ge_actor_advance(&a, 0, say, NULL);
    CHECK(a.image == TEST_IMG_FOUR && a.params[TEST_HERO_PARAM_ACTION] == 1 && a.params[TEST_HERO_PARAM_HP] == 3 && dgx_ge_actor_x(&a) == 10);
    dgx_ge_actor_advance(&a, 49, say, NULL);
    CHECK(a.image == TEST_IMG_FOUR);
    dgx_ge_actor_advance(&a, 50, say, NULL);
    CHECK(a.image == TEST_IMG_EIGHT && a.image_dx == 1 && a.image_dy == -1 && dgx_ge_actor_x(&a) == 12);
    dgx_ge_actor_advance(&a, 100, say, NULL);
    CHECK(said_signal == 7 && a.image == TEST_IMG_ONE && dgx_ge_actor_x(&a) == 14);
    dgx_ge_actor_advance(&a, 150, say, NULL);
    CHECK(said_sound == 2 && dgx_ge_actor_x(&a) == 17 && a.script == TEST_HERO_SCRIPT_RUN);

    /* stop at a branch point: the loop has none, so it waits for its end */
    CHECK(dgx_ge_actor_event(&a, TEST_HERO_EVENT_STOP, 160, say, NULL));
    CHECK(a.script == TEST_HERO_SCRIPT_RUN && a.pending);
    dgx_ge_actor_advance(&a, 199, say, NULL);
    CHECK(a.script == TEST_HERO_SCRIPT_RUN);
    dgx_ge_actor_advance(&a, 200, say, NULL);
    CHECK(a.script == TEST_HERO_SCRIPT_STAND && a.params[TEST_HERO_PARAM_ACTION] == 0 && a.image == TEST_IMG_ONE && dgx_ge_actor_x(&a) == 20);

    /* an event the script has no rule for means nothing */
    CHECK(!dgx_ge_actor_event(&a, TEST_HERO_EVENT_STOP, 210, say, NULL) && !a.pending);

    /* jump at the end of stand: the frame is shown out, then the run begins */
    CHECK(dgx_ge_actor_event(&a, TEST_HERO_EVENT_JUMP, 210, say, NULL));
    dgx_ge_actor_advance(&a, 299, say, NULL);
    CHECK(a.script == TEST_HERO_SCRIPT_STAND);
    dgx_ge_actor_advance(&a, 300, say, NULL);
    CHECK(a.script == TEST_HERO_SCRIPT_RUN && a.image == TEST_IMG_FOUR);

    /* hit at once: turned round, masks, holds after its frame; a move right after the turn already goes the other way */
    const int before_hit = dgx_ge_actor_x(&a);
    CHECK(dgx_ge_actor_event(&a, TEST_HERO_EVENT_HIT, 320, say, NULL));
    CHECK(dgx_ge_actor_x(&a) == before_hit - 4);
    CHECK(a.script == TEST_HERO_SCRIPT_HURT && a.flip && a.masks == (1u << 0 | 1u << 3) && a.image == TEST_IMG_FOUR);
    CHECK(a.params[TEST_HERO_PARAM_HP] == -2 && a.params[TEST_HERO_PARAM_ACTION] == 1); /* params keep across scripts */
    const int x = dgx_ge_actor_x(&a);
    dgx_ge_actor_advance(&a, 2000, say, NULL);
    CHECK(a.holding && dgx_ge_actor_x(&a) == x - 5);

    /* hit in hurt has a rule of its own: a fall, holding or not */
    CHECK(dgx_ge_actor_event(&a, TEST_HERO_EVENT_HIT, 2000, say, NULL) && a.script == TEST_HERO_SCRIPT_FALL && !a.holding);

    /* the motion: a second of 200 px/s^2 is 100 px, made in one call or in many */
    dgx_ge_actor_t one, many;
    dgx_ge_actor_start(&one, &anim, TEST_HERO_SCRIPT_FALL, 0, 0, 1000);
    dgx_ge_actor_start(&many, &anim, TEST_HERO_SCRIPT_FALL, 0, 0, 1000);
    dgx_ge_actor_advance(&one, 2000, say, NULL);
    for (uint32_t t = 1000; t <= 2000; t += 7) dgx_ge_actor_advance(&many, t, say, NULL);
    dgx_ge_actor_advance(&many, 2000, say, NULL);
    CHECK(dgx_ge_actor_y(&one) == 100);
    CHECK(abs(dgx_ge_actor_y(&many) - 100) <= 1 && one.holding && many.holding);

    /*
     * Bound to time: the run, 10 s in one call, is where and what it is when
     * moved on in small calls; every step passed is made, a sound says the
     * moment of its step. The cycle is 50 ms and 3 px from 100 ms on.
     */
    dgx_ge_actor_t once, often;
    dgx_ge_actor_start(&once, &anim, TEST_HERO_SCRIPT_RUN, 10, 0, 0);
    dgx_ge_actor_start(&often, &anim, TEST_HERO_SCRIPT_RUN, 10, 0, 0);
    sounds = 0;
    dgx_ge_actor_advance(&once, 10120, say, NULL);
    CHECK(sounds == 200 && said_at == 10100);
    for (uint32_t t = 0; t <= 10120; t += 13) dgx_ge_actor_advance(&often, t, say, NULL);
    dgx_ge_actor_advance(&often, 10120, say, NULL);
    CHECK(dgx_ge_actor_x(&once) == 14 + 3 * 200 && dgx_ge_actor_x(&often) == dgx_ge_actor_x(&once));
    CHECK(once.image == TEST_IMG_ONE && often.image == once.image && once.due_ms == often.due_ms && once.due_ms == 10150);

    /* a path: the place is the formula of the time, wherever it is looked at; turned round, x goes the other way */
    dgx_ge_actor_t thrown;
    dgx_ge_actor_start(&thrown, &anim, TEST_HERO_SCRIPT_ARC, 100, 50, 1000);
    dgx_ge_actor_advance(&thrown, 1500, say, NULL);
    CHECK(dgx_ge_actor_x(&thrown) == 120 && dgx_ge_actor_y(&thrown) == 44);   /* 40 * .5, -120 * .5 + 216 * .25 */
    CHECK(abs(thrown.vy - 96 * 256) < 256 && abs(thrown.vx - 40 * 256) < 256); /* -120 + 432 * .5 points a second down */
    dgx_ge_actor_advance(&thrown, 1999, say, NULL);
    CHECK(dgx_ge_actor_x(&thrown) == 139 && dgx_ge_actor_y(&thrown) == 145); /* 39.96, -119.88 + 215.57 */
    /* at 2000 the path gives way to a velocity of numbers and a param: 10, 120 / 2 */
    dgx_ge_actor_advance(&thrown, 2500, say, NULL);
    CHECK(thrown.path == 0 && thrown.vx == 10 * 256 && thrown.vy == 60 * 256);
    CHECK(thrown.params[TEST_HERO_PARAM_HP] == 3 + 20);
    /* the origin moved by 1000, 500: on its path the actor goes on from the moved place, exactly */
    dgx_ge_actor_t shifted;
    dgx_ge_actor_start(&shifted, &anim, TEST_HERO_SCRIPT_ARC, 100, 50, 1000);
    dgx_ge_actor_advance(&shifted, 1500, say, NULL);
    dgx_ge_actor_shift(&shifted, 1000, 500);
    CHECK(dgx_ge_actor_x(&shifted) == 120 - 1000 && dgx_ge_actor_y(&shifted) == 44 - 500);
    CHECK(shifted.entered_x == (100 - 1000) * 256 && shifted.entered_y == (50 - 500) * 256); /* where it entered its script, as a record keeps it */
    dgx_ge_actor_advance(&shifted, 1999, say, NULL);
    CHECK(dgx_ge_actor_x(&shifted) == 139 - 1000 && dgx_ge_actor_y(&shifted) == 145 - 500);
    /* what masks do when they meet, as the script says: from the step after the first frame until the next masks */
    dgx_ge_actor_t striker;
    dgx_ge_hit_t   blow;
    dgx_ge_actor_start(&striker, &anim, TEST_HERO_SCRIPT_STRIKE, 0, 0, 0);
    dgx_ge_actor_advance(&striker, 50, say, NULL);
    CHECK(striker.masks == (1u << 1 | 1u << 2) && !dgx_ge_actor_hit(&striker, 2, 1, &blow));
    CHECK(dgx_ge_actor_carried(&striker, TEST_POINT_HAND) == TEST_IMG_EIGHT && dgx_ge_actor_carried(&striker, TEST_POINT_HAND + 1) == -1);
    dgx_ge_actor_advance(&striker, 150, say, NULL);
    CHECK(dgx_ge_actor_hit(&striker, 2, 1, &blow) && blow.damage == 4 && blow.event == TEST_HERO_EVENT_HIT && blow.recoil == -1);
    CHECK(dgx_ge_actor_hit(&striker, 2, 3, &blow) && blow.on == (1u << 1 | 1u << 3));
    CHECK(dgx_ge_actor_hit(&striker, 2, 2, &blow) && blow.damage == 0 && blow.event == -1 && blow.recoil == TEST_HERO_EVENT_STOP);
    CHECK(!dgx_ge_actor_hit(&striker, 1, 1, &blow) && !dgx_ge_actor_hit(&striker, 2, 0, &blow));
    dgx_ge_actor_advance(&striker, 250, say, NULL);
    CHECK(!dgx_ge_actor_hit(&striker, 2, 1, &blow)); /* masks again: the strike is over */
    CHECK(dgx_ge_actor_carried(&striker, TEST_POINT_HAND) == -1); /* and nothing in the hand */
    /* let go: the hand is empty, the game is told what at what velocity, x as the actor faces; once */
    dgx_ge_actor_t thrower;
    dgx_ge_actor_start(&thrower, &anim, TEST_HERO_SCRIPT_THROW, 0, 0, 1000);
    thrower.flip = true;
    releases     = 0;
    dgx_ge_actor_advance(&thrower, 1050, say, NULL);
    CHECK(releases == 0 && dgx_ge_actor_carried(&thrower, TEST_POINT_HAND) == TEST_IMG_EIGHT);
    dgx_ge_actor_advance(&thrower, 1300, say, NULL);
    CHECK(releases == 1 && said_release == TEST_POINT_HAND && said_at == 1100 && dgx_ge_actor_carried(&thrower, TEST_POINT_HAND) == -1);
    CHECK(thrower.released.image == TEST_IMG_EIGHT && thrower.released.vx == -60 && thrower.released.vy == -30);
    /* the game puts something into the hand and takes it out; with both slots full a third point holds nothing */
    CHECK(dgx_ge_actor_carry(&thrower, TEST_POINT_HAND, TEST_IMG_FOUR) && dgx_ge_actor_carried(&thrower, TEST_POINT_HAND) == TEST_IMG_FOUR);
    CHECK(dgx_ge_actor_carry(&thrower, 7, TEST_IMG_ONE) && !dgx_ge_actor_carry(&thrower, 8, TEST_IMG_ONE));
    CHECK(dgx_ge_actor_carry(&thrower, TEST_POINT_HAND, -1) && dgx_ge_actor_carried(&thrower, TEST_POINT_HAND) == -1);

    /*
     * The point an actor entered its script: started, sent by an event at
     * once, gone on at the end. Started again from it, it is the same later.
     */
    dgx_ge_actor_t walker;
    dgx_ge_actor_start(&walker, &anim, TEST_HERO_SCRIPT_RUN, 10, 20, 500);
    CHECK(walker.entered_ms == 500 && walker.entered_x == 10 * 256 && walker.entered_params[TEST_HERO_PARAM_HP] == 3);
    dgx_ge_actor_advance(&walker, 640, say, NULL);
    const int32_t x_then = walker.x;
    dgx_ge_actor_event(&walker, TEST_HERO_EVENT_HIT, 640, say, NULL); /* hurt, at once: from where it is at 640, then turned, moved */
    CHECK(walker.script == TEST_HERO_SCRIPT_HURT && walker.entered_ms == 640 && walker.entered_x == x_then && !walker.entered_flip &&
          walker.x != x_then);
    dgx_ge_actor_advance(&walker, 700, say, NULL);
    CHECK(walker.entered_params[TEST_HERO_PARAM_HP] == 3 && walker.params[TEST_HERO_PARAM_HP] == -2);
    dgx_ge_actor_event(&walker, TEST_HERO_EVENT_HIT, 760, say, NULL); /* hurt -> fall */
    dgx_ge_actor_t again;
    dgx_ge_actor_start(&again, &anim, walker.script, 0, 0, walker.entered_ms);
    again.x = again.entered_x = walker.entered_x, again.y = walker.entered_y, again.flip = walker.entered_flip;
    memcpy(again.params, walker.entered_params, sizeof(again.params));
    dgx_ge_actor_advance(&walker, 3000, say, NULL);
    dgx_ge_actor_advance(&again, 3000, say, NULL);
    CHECK(again.x == walker.x && again.y == walker.y && again.image == walker.image && again.flip == walker.flip &&
          again.params[TEST_HERO_PARAM_ACTION] == walker.params[TEST_HERO_PARAM_ACTION]);

    /*
     * A thing in a hand, an actor of its own: shown by the frame the hand holds
     * it by, else by its own; active only where the holder's move is too;
     * thrown free at the step's moment and velocity; dropped.
     */
    dgx_ge_actor_t holder, thing;
    dgx_ge_actor_start(&holder, &anim, TEST_HERO_SCRIPT_STRIKE, 0, 0, 0);
    dgx_ge_actor_start(&thing, &anim, TEST_HERO_SCRIPT_STAND, 0, 0, 0);
    dgx_ge_actor_advance(&holder, 50, say, NULL);
    dgx_ge_actor_advance(&thing, 50, say, NULL);
    CHECK(dgx_ge_actor_hold(&holder, TEST_POINT_HAND, &thing) && dgx_ge_actor_held(&holder, TEST_POINT_HAND) == &thing && thing.holder == &holder);
    CHECK(dgx_ge_held_image(&thing) == TEST_IMG_EIGHT); /* the hand's frame of it */
    thing.masks = 1u << 1 | 1u << 2 | 1u << 3;
    CHECK(dgx_ge_held_masks(&thing) == (1u << 1 | 1u << 2)); /* the strike has 1 2 active */
    dgx_ge_actor_advance(&holder, 250, say, NULL);           /* carry hand none: its own frame; masks 1 */
    CHECK(dgx_ge_held_image(&thing) == TEST_IMG_ONE && dgx_ge_held_masks(&thing) == 1u << 1 && dgx_ge_actor_held(&holder, TEST_POINT_HAND) == &thing);
    dgx_ge_actor_drop(&thing);
    CHECK(!thing.holder && !dgx_ge_actor_held(&holder, TEST_POINT_HAND) && dgx_ge_held_masks(&thing) == thing.masks);
    dgx_ge_actor_t pitcher, stone;
    dgx_ge_actor_start(&pitcher, &anim, TEST_HERO_SCRIPT_THROW, 0, 0, 1000);
    pitcher.flip = true;
    dgx_ge_actor_start(&stone, &anim, TEST_HERO_SCRIPT_STAND, 30, 40, 1000);
    dgx_ge_actor_advance(&pitcher, 1050, say, NULL);
    CHECK(dgx_ge_actor_hold(&pitcher, TEST_POINT_HAND, &stone));
    releases = 0;
    dgx_ge_actor_advance(&pitcher, 1300, say, NULL);
    CHECK(releases == 1 && !stone.holder && !dgx_ge_actor_held(&pitcher, TEST_POINT_HAND) && pitcher.released.image == TEST_IMG_EIGHT);
    CHECK(stone.vx == -60 * 256 && stone.vy == -30 * 256 && stone.moved_ms == 1100); /* thrown when the hand let go */
    dgx_ge_actor_advance(&stone, 1300, say, NULL);
    CHECK(dgx_ge_actor_x(&stone) == 30 - 12 && dgx_ge_actor_y(&stone) == 40 - 6); /* 0.2 s of it */

    /* a param changed by its script: told, with the moment; set to what it was: not; now is the step's moment in seconds */
    dgx_ge_actor_t tipsy;
    dgx_ge_actor_start(&tipsy, &anim, TEST_HERO_SCRIPT_TIPSY, 0, 0, 20000);
    params_changed = 0, said_tell = -1;
    dgx_ge_actor_advance(&tipsy, 20050, say, NULL);
    CHECK(params_changed == 1 && said_param == TEST_HERO_PARAM_HP && param_at == 20000 && tipsy.params[TEST_HERO_PARAM_HP] == 30);
    CHECK(said_tell == TEST_EVENT_DRANK_WINE && said_at == 20000);
    dgx_ge_actor_advance(&tipsy, 20500, say, NULL);
    CHECK(params_changed == 1); /* add 0: no change, nothing told */

    /*
     * a moment kept in a param (now + 10, seconds of the clock) at 10 hours of it, at 40000 s, 2.3 days (where a float
     * of the milliseconds over 1000 falls short of the second) and 23 days: whole, and waited for to its second; past
     * 32 bits a value stops at the limit, so does add
     */
    const uint32_t clocks[] = {36000000u, 40000000u, 199991000u, 2000000000u};
    for (int i = 0; i < 4; ++i) {
        dgx_ge_actor_t sober;
        dgx_ge_actor_start(&sober, &anim, TEST_HERO_SCRIPT_SOBER, 0, 0, clocks[i]);
        dgx_ge_actor_advance(&sober, clocks[i] + 9999, NULL, NULL);
        CHECK(sober.params[TEST_HERO_PARAM_HP] == (int32_t)(clocks[i] / 1000 + 10) && sober.params[TEST_HERO_PARAM_POWER] == INT32_MAX &&
              sober.params[TEST_HERO_PARAM_ACTION] == 0);
        dgx_ge_actor_advance(&sober, clocks[i] + 10000, NULL, NULL);
        CHECK(sober.params[TEST_HERO_PARAM_ACTION] == 7);
    }
    dgx_ge_actor_t low;
    dgx_ge_actor_start(&low, &anim, TEST_HERO_SCRIPT_HURT, 0, 0, 0);
    low.params[TEST_HERO_PARAM_HP] = INT32_MIN + 2; /* add hp -5 */
    dgx_ge_actor_advance(&low, 0, NULL, NULL);
    CHECK(low.params[TEST_HERO_PARAM_HP] == INT32_MIN);

    /* the world of a map acted on: a cell told an event with a value, a button pressed, a cell cleared; their numbers worked out */
    {
        const uint8_t *wb = dgx_ge_pack_blob(&pack, TEST_BLOB_WARDEN, &n);
        dgx_ge_anim_t  wa;
        dgx_ge_actor_t warden;
        CHECK(wb && dgx_ge_anim_open(&wa, wb, n));
        dgx_ge_actor_start(&warden, &wa, 0, 0, 0, 0);
        world_count = 0;
        CHECK(dgx_ge_actor_event(&warden, TEST_EVENT_LOOSE, 10, say, NULL));
        dgx_ge_actor_advance(&warden, 10, say, NULL);
        CHECK(world_count == 3);
        CHECK(world_said[0][0] == DGX_GE_ANIM_CELL && world_said[0][1] == 34 && world_said[0][2] == TEST_EVENT_LOOSE * 1000 + 6);
        CHECK(world_said[1][0] == DGX_GE_ANIM_PRESS && world_said[1][1] == 7 && world_said[2][0] == DGX_GE_ANIM_CLEAR && world_said[2][1] == 41);
        CHECK(dgx_ge_anim_said(5) == 0);
        /* where it is in its script: it walks till x 120, calls one of a kind there, and is gone */
        dgx_ge_actor_start(&warden, &wa, TEST_WARDEN_SCRIPT_WALK, 100, 40, 0);
        world_count = 0, appeared = vanished = 0;
        dgx_ge_actor_advance(&warden, 1000, say, NULL);
        CHECK(appeared == 1 && vanished == 1 && appear_at[0] == 125 && appear_at[1] == 40 && appear_at[2] == TEST_BLOB_HERO && appear_at[3] == 1);
        CHECK(appear_event == TEST_EVENT_LOOSE && dgx_ge_actor_x(&warden) == 120 && said_at == 200);
        /* a watch begun over nothing (nothing within 8 points below): the loss at once, a ms on; 5 points over the slab: none */
        dgx_ge_actor_t stander;
        dgx_ge_actor_start(&stander, &wa, TEST_WARDEN_SCRIPT_STAND, 50, 150, 0);
        stander.terrain = &slab_terrain;
        dgx_ge_actor_advance(&stander, 500, say, NULL);
        CHECK(stander.script == TEST_WARDEN_SCRIPT_REST);
        dgx_ge_actor_start(&stander, &wa, TEST_WARDEN_SCRIPT_STAND, 50, 195, 0);
        stander.terrain = &slab_terrain;
        dgx_ge_actor_advance(&stander, 500, say, NULL);
        CHECK(stander.script == TEST_WARDEN_SCRIPT_STAND);
        /* begun before the terrain was given, over nothing: lost at its first look with one */
        dgx_ge_actor_start(&stander, &wa, TEST_WARDEN_SCRIPT_STAND, 50, 150, 0);
        dgx_ge_actor_advance(&stander, 200, say, NULL);
        CHECK(stander.script == TEST_WARDEN_SCRIPT_STAND);
        stander.terrain = &slab_terrain;
        dgx_ge_actor_advance(&stander, 300, say, NULL);
        CHECK(stander.script == TEST_WARDEN_SCRIPT_REST);
        dgx_ge_actor_start(&stander, &wa, TEST_WARDEN_SCRIPT_STAND, 50, 195, 0); /* near the slab: no */
        dgx_ge_actor_advance(&stander, 200, say, NULL);
        stander.terrain = &slab_terrain;
        dgx_ge_actor_advance(&stander, 300, say, NULL);
        CHECK(stander.script == TEST_WARDEN_SCRIPT_STAND);
    }

    /* a shot fired by the script: from a point, its kind and velocity, x as the actor faces */
    dgx_ge_actor_t gunner;
    dgx_ge_actor_start(&gunner, &anim, TEST_HERO_SCRIPT_GUNNER, 0, 0, 0);
    gunner.flip = true;
    dgx_ge_actor_advance(&gunner, 150, say, NULL);
    CHECK(said_shot == TEST_POINT_HAND && said_at == 100 && gunner.shot.kind == TEST_BLOB_DART && gunner.shot.vx == -50 && gunner.shot.vy == -20);

    /* a contact that names an event: at the moment it touched, put there, into the script the matrix says */
    dgx_ge_actor_t glass, glass2;
    dgx_ge_actor_start(&glass, &anim, TEST_HERO_SCRIPT_GLASS, 0, 0, 0);
    dgx_ge_actor_start(&glass2, &anim, TEST_HERO_SCRIPT_GLASS, 0, 0, 0);
    glass.terrain = glass2.terrain = &bare_floor;
    dgx_ge_actor_advance(&glass, 2000, say, NULL);
    CHECK(glass.script == TEST_HERO_SCRIPT_BROKEN && dgx_ge_actor_y(&glass) == 200 && glass.vy == 0 && glass.image == TEST_IMG_FOUR);
    CHECK(glass.entered_ms >= 960 && glass.entered_ms <= 964 && glass.entered_y >= 200 * 256 && glass.entered_y < 201 * 256);
    dgx_ge_actor_t glass3;
    dgx_ge_actor_start(&glass3, &anim, TEST_HERO_SCRIPT_GLASS, 0, 0, 0);
    glass3.terrain = &bare_floor;
    dgx_ge_actor_advance(&glass3, 1500, say, NULL); /* broken at 963, its frame lasts a second: it does not hold yet */
    CHECK(glass3.script == TEST_HERO_SCRIPT_BROKEN && !glass3.holding && glass3.due_ms == glass3.entered_ms + 1000);
    for (uint32_t t = 0; t <= 2000; t += 33) dgx_ge_actor_advance(&glass2, t, say, NULL); /* looked at often: the same */
    CHECK(glass2.script == TEST_HERO_SCRIPT_BROKEN && glass2.entered_ms == glass.entered_ms && abs(glass2.y - glass.y) < 256);

    dgx_ge_actor_t turned;
    dgx_ge_actor_start(&turned, &anim, TEST_HERO_SCRIPT_ARC, 100, 50, 1000);
    turned.flip = true;
    dgx_ge_actor_advance(&turned, 1500, say, NULL);
    CHECK(dgx_ge_actor_x(&turned) == 80 && dgx_ge_actor_y(&turned) == 44);

    /*
     * Contacts: a ball dropped on a floor at 200 bounces at the moment it
     * touched (about 962 ms in: 200 = 432 t^2 / 2), wherever it is looked at:
     * in one call or in many it is in the same place and as fast.
     */
    dgx_ge_actor_t ball, other;
    dgx_ge_actor_start(&ball, &anim, TEST_HERO_SCRIPT_DROP, 0, 0, 0);
    dgx_ge_actor_start(&other, &anim, TEST_HERO_SCRIPT_DROP, 0, 0, 0);
    ball.terrain = other.terrain = &floor_terrain;
    contacts = 0;
    dgx_ge_actor_advance(&ball, 1200, say, NULL);
    CHECK(contacts == 1 && ball.vy < 0 && dgx_ge_actor_y(&ball) < 200);
    contacts = 0;
    for (uint32_t t = 0; t <= 1200; t += 17) dgx_ge_actor_advance(&other, t, say, NULL);
    dgx_ge_actor_advance(&other, 1200, say, NULL);
    CHECK(contacts == 1 && abs(other.y - ball.y) <= 256 && abs(other.vy - ball.vy) <= 3 * 256);
    /* up at about 208 points a second, half of the 415 it came down with */
    CHECK(abs(ball.vy / 256 + 208 - (int)(432 * 0.238)) <= 3);
    /*
     * Many bounces on the way to now, in one call: every one of them is
     * dealt with (there is no limit), and the ball never goes through the
     * floor: halving its speed at each, it ends up hopping low on it.
     */
    dgx_ge_actor_t hopper;
    dgx_ge_actor_start(&hopper, &anim, TEST_HERO_SCRIPT_DROP, 0, 0, 0);
    hopper.terrain = &floor_terrain;
    contacts       = 0;
    dgx_ge_actor_advance(&hopper, 6000, say, NULL);
    CHECK(contacts > 20 && dgx_ge_actor_y(&hopper) <= 200 && dgx_ge_actor_y(&hopper) >= 198);
    /* a handler that puts the ball back as it was: the same contact for ever; stopped, the ball at rest on the floor, not through it */
    dgx_ge_actor_t stuck;
    dgx_ge_actor_start(&stuck, &anim, TEST_HERO_SCRIPT_DROP, 0, 0, 0);
    stuck.terrain = &same_terrain;
    contacts      = 0;
    dgx_ge_actor_advance(&stuck, 6000, say, NULL);
    /* twice at most: the first move was one step of 3 s, the one from the restart two, a step of 1/256 apart; then the same */
    CHECK(contacts >= 1 && contacts <= 2 && stuck.vy == 0 && stuck.ay == 0 && dgx_ge_actor_y(&stuck) >= 199 && dgx_ge_actor_y(&stuck) <= 200);
    const int were = contacts;
    dgx_ge_actor_advance(&stuck, 60000, say, NULL);
    CHECK(contacts == were && dgx_ge_actor_y(&stuck) >= 199 && dgx_ge_actor_y(&stuck) <= 200);

    /*
     * Contacts by masks: a walker (40 points a second) on a floor that ends
     * at x 100 loses it at 2500 ms: fell at that moment, there, in one call
     * or in many; one in the air that comes over a floor from x 100 on lands
     * at 2500 and stands (its script stops watching).
     */
    dgx_ge_actor_t edger, edger2, lander;
    dgx_ge_actor_start(&edger, &anim, TEST_HERO_SCRIPT_WALKER, 0, 0, 0);
    dgx_ge_actor_start(&edger2, &anim, TEST_HERO_SCRIPT_WALKER, 0, 0, 0);
    edger.terrain = edger2.terrain = &edge_terrain;
    dgx_ge_actor_advance(&edger2, 0, say, NULL); /* its first steps: the watch set, touching below where it stands */
    CHECK(edger2.contacts[0].mask == 1 && edger2.contacts[0].from == DGX_GE_TOUCH_BELOW && edger2.contacts[0].reach == 1 && edger2.contacts[0].was == DGX_GE_TOUCH_BELOW);
    CHECK(edger2.contacts[0].on == TEST_HERO_EVENT_LANDED && edger2.contacts[0].off == TEST_HERO_EVENT_FELL && edger2.contacts[1].mask == 0xFF);
    dgx_ge_actor_advance(&edger, 4000, say, NULL);
    CHECK(edger.script == TEST_HERO_SCRIPT_FALL && edger.entered_ms == 2500 && edger.entered_x == 100 * 256 && dgx_ge_actor_x(&edger) == 160);
    for (uint32_t t = 37; t <= 4000; t += 37) dgx_ge_actor_advance(&edger2, t, say, NULL);
    dgx_ge_actor_advance(&edger2, 4000, say, NULL);
    /* the same moment and place of the fall; the way on differs by the rounding of many steps against one, a point or two */
    CHECK(edger2.script == TEST_HERO_SCRIPT_FALL && abs((int32_t)(edger2.entered_ms - 2500)) <= 1 && abs(edger2.entered_x - 100 * 256) <= 256);
    CHECK(abs(edger2.x - edger.x) <= 256 && abs(edger2.y - edger.y) <= 2 * 256);
    dgx_ge_actor_start(&lander, &anim, TEST_HERO_SCRIPT_WALKER, 0, 0, 0);
    lander.terrain = &ledge_terrain;
    dgx_ge_actor_advance(&lander, 4000, say, NULL);
    CHECK(lander.script == TEST_HERO_SCRIPT_EDGE_STAND && lander.entered_ms == 2500 && dgx_ge_actor_x(&lander) == 100 && lander.vx == 0);
    CHECK(lander.contacts[0].mask == 0xFF); /* contact 1 off */

    /*
     * A plunge at 3 points a ms into a slab whose top is at y 200: the ms grain skips the one point over it (below),
     * the next look finds the mask inside; inside is a touch on any side watched: landed at that ms, a point in at most.
     */
    dgx_ge_actor_t plunger;
    dgx_ge_actor_start(&plunger, &anim, TEST_HERO_SCRIPT_PLUNGE, 0, 0, 0);
    plunger.terrain = &slab_terrain;
    dgx_ge_actor_advance(&plunger, 1000, say, NULL);
    CHECK(plunger.script == TEST_HERO_SCRIPT_EDGE_STAND && plunger.entered_ms >= 66 && plunger.entered_ms <= 67);
    CHECK(dgx_ge_actor_y(&plunger) >= 199 && dgx_ge_actor_y(&plunger) <= 201 && plunger.vy == 0);

    /* smoothstep on a path: 80 points in half a second, 40 at a quarter (the curve's middle), then it stays */
    dgx_ge_actor_t eased;
    dgx_ge_actor_start(&eased, &anim, TEST_HERO_SCRIPT_EASE, 0, 0, 0);
    dgx_ge_actor_advance(&eased, 100, say, NULL); /* a fifth of the way: 0.2^2 * (3 - 0.4) = 0.104 of 80 */
    CHECK(dgx_ge_actor_x(&eased) == 8);
    dgx_ge_actor_advance(&eased, 250, say, NULL);
    CHECK(dgx_ge_actor_x(&eased) == 40);
    dgx_ge_actor_advance(&eased, 1000, say, NULL);
    CHECK(dgx_ge_actor_x(&eased) == 80);

    /* bits and a constant table, worked out by the runtime (of params): 3 ^ 8 + 4 ones + 15; -7 + 0 + 0 outside; 5 */
    dgx_ge_actor_t bitter;
    dgx_ge_actor_start(&bitter, &anim, TEST_HERO_SCRIPT_BITWORK, 0, 0, 0);
    dgx_ge_actor_advance(&bitter, 50, say, NULL);
    CHECK(bitter.params[TEST_HERO_PARAM_ACTION] == 30 && bitter.params[TEST_HERO_PARAM_POWER] == -7 && bitter.params[TEST_HERO_PARAM_HP] == 5);
    CHECK(anim.tables && anim.tables_size == 2 + 3 * 2);

    /*
     * game.: the game answers by the number of the question (far 0: 21, near 1: as it says); near says 5, so the dice are
     * not cast; near 0: they are, once; no function: 0
     */
    uint32_t       seed = 7, *dice = dgx_ge_dice;
    dgx_ge_actor_t asker;
    int            near = 5;
    dgx_ge_dice         = &seed;
    dgx_ge_expr_ask     = answer;
    dgx_ge_expr_ask_ctx = &near;
    dgx_ge_actor_start(&asker, &anim, TEST_HERO_SCRIPT_ASK, 0, 0, 0);
    dgx_ge_actor_advance(&asker, 0, say, NULL);
    CHECK(asker.params[TEST_HERO_PARAM_ACTION] == 42 && asker.params[TEST_HERO_PARAM_POWER] == 120 && seed == 7);
    near = 0;
    dgx_ge_actor_start(&asker, &anim, TEST_HERO_SCRIPT_ASK, 0, 0, 0);
    dgx_ge_actor_advance(&asker, 0, say, NULL);
    CHECK(asker.params[TEST_HERO_PARAM_ACTION] == 42 && seed == 7u * 214013u + 2531011u &&
          asker.params[TEST_HERO_PARAM_POWER] == (int)((seed >> 16) % 1001));
    /* lazy: near 5, and casts (1), or and ?: do not (1, 7); near 0, and does not (0), or and ?: do: twice */
    near = 5, seed = 7;
    dgx_ge_actor_start(&asker, &anim, TEST_HERO_SCRIPT_LAZY, 0, 0, 0);
    dgx_ge_actor_advance(&asker, 0, say, NULL);
    CHECK(asker.params[TEST_HERO_PARAM_ACTION] == 1 && asker.params[TEST_HERO_PARAM_HP] == 1 && asker.params[TEST_HERO_PARAM_POWER] == 7 &&
          seed == 7u * 214013u + 2531011u);
    near = 0, seed = 7;
    dgx_ge_actor_start(&asker, &anim, TEST_HERO_SCRIPT_LAZY, 0, 0, 0);
    dgx_ge_actor_advance(&asker, 0, say, NULL);
    CHECK(asker.params[TEST_HERO_PARAM_ACTION] == 0 && asker.params[TEST_HERO_PARAM_HP] == 1 && seed == (7u * 214013u + 2531011u) * 214013u + 2531011u &&
          asker.params[TEST_HERO_PARAM_POWER] == (int)((seed >> 16) % 1001));
    dgx_ge_expr_ask = NULL;
    dgx_ge_actor_start(&asker, &anim, TEST_HERO_SCRIPT_ASK, 0, 0, 0);
    dgx_ge_actor_advance(&asker, 0, say, NULL);
    CHECK(asker.params[TEST_HERO_PARAM_ACTION] == 0);
    dgx_ge_dice = dice;

    /* axis(): the pad of the input the game gave, -1..1 (half to the right, all the way up, a trigger pulled); none: 0 */
    dgx_ge_input_t pad = {0};
    pad.axes[DGX_GE_AXIS_LX] = 16384, pad.axes[DGX_GE_AXIS_LY] = -DGX_GE_AXIS_MAX, pad.axes[DGX_GE_AXIS_RT] = DGX_GE_AXIS_MAX;
    dgx_ge_expr_input = &pad;
    dgx_ge_actor_t stick;
    dgx_ge_actor_start(&stick, &anim, TEST_HERO_SCRIPT_STICK, 0, 0, 0);
    dgx_ge_actor_advance(&stick, 0, say, NULL);
    CHECK(stick.params[TEST_HERO_PARAM_ACTION] == 50 && stick.params[TEST_HERO_PARAM_HP] == -100 && stick.params[TEST_HERO_PARAM_POWER] == 100);
    dgx_ge_expr_input = NULL;
    dgx_ge_actor_start(&stick, &anim, TEST_HERO_SCRIPT_STICK, 0, 0, 0);
    dgx_ge_actor_advance(&stick, 0, say, NULL);
    CHECK(stick.params[TEST_HERO_PARAM_ACTION] == 0 && stick.params[TEST_HERO_PARAM_HP] == 0 && stick.params[TEST_HERO_PARAM_POWER] == 0);

    /*
     * sense: four heroes of the registry, the looker at 0, 0 facing forward (right); one at 50 with 1 life (where says no),
     * one at 100 with 3, one at 90 with 5: that one, 90 forward, its lives * 10; turned round it is 90 behind; none within 10
     */
    anim.id = TEST_BLOB_HERO; /* its kind, as dgx_ge_kind would know it */
    dgx_ge_collisions_t *reg = dgx_ge_collisions_create(4, 8);
    dgx_ge_actor_t       herd[4];
    const int            at[4][3] = {{0, 0, 3}, {30, 40, 1}, {-60, 80, 3}, {90, 0, 5}};
    for (int i = 0; i < 4; ++i) {
        dgx_ge_actor_start(&herd[i], &anim, i ? TEST_HERO_SCRIPT_STAND : TEST_HERO_SCRIPT_LOOK, at[i][0], at[i][1], 0);
        herd[i].params[TEST_HERO_PARAM_HP] = at[i][2];
        const dgx_ge_body_t body = {&pack, TEST_IMG_ONE, at[i][0], at[i][1], false, 0};
        dgx_ge_collisions_set(reg, i, &body, (uint16_t)(i + 1), 1);
        dgx_ge_collisions_actor(reg, i, &herd[i]);
    }
    sense_registry               = reg;
    const dgx_ge_terrain_t eyes = {.sense = sense_heroes};
    herd[0].terrain             = &eyes;
    herd[0].params[TEST_HERO_PARAM_HP] = 3;
    dgx_ge_actor_advance(&herd[0], 50, say, NULL);
    CHECK(herd[0].params[TEST_HERO_PARAM_ACTION] == 90 && herd[0].params[TEST_HERO_PARAM_POWER] == 90 && herd[0].params[TEST_HERO_PARAM_HP] == 50);
    dgx_ge_actor_advance(&herd[0], 150, say, NULL);
    CHECK(herd[0].params[TEST_HERO_PARAM_ACTION] == 90 && herd[0].params[TEST_HERO_PARAM_POWER] == -90);
    dgx_ge_actor_advance(&herd[0], 250, say, NULL);
    CHECK(herd[0].params[TEST_HERO_PARAM_ACTION] == -1 && herd[0].params[TEST_HERO_PARAM_POWER] == -90);
    dgx_ge_collisions_destroy(&reg);
    anim.id = 0xFFFF;

    /* steer: seek, flee by a force of 20, arrive within its radius, pursue where it will be, wander by an angle; turned, the same forward */
    dgx_ge_actor_t chaser;
    dgx_ge_actor_start(&chaser, &anim, TEST_HERO_SCRIPT_CHASE, 0, 0, 0);
    dgx_ge_actor_advance(&chaser, 50, say, NULL);
    CHECK(chaser.vx == 60 * 256 && chaser.vy == 80 * 256 && chaser.ax == 0 && chaser.ay == 0);
    dgx_ge_actor_advance(&chaser, 150, say, NULL);
    CHECK(chaser.vx == 48 * 256 && chaser.vy == 64 * 256);
    dgx_ge_actor_advance(&chaser, 250, say, NULL);
    CHECK(chaser.vx == 30 * 256 && chaser.vy == 40 * 256);
    dgx_ge_actor_advance(&chaser, 350, say, NULL);
    CHECK(abs(chaser.vx - (int32_t)lroundf(50 * 100 / sqrtf(100 * 100 + 50 * 50) * 256)) <= 1 &&
          abs(chaser.vy - (int32_t)lroundf(50 * 50 / sqrtf(100 * 100 + 50 * 50) * 256)) <= 1);
    dgx_ge_actor_advance(&chaser, 450, say, NULL);
    CHECK(abs(chaser.vx) <= 1 && chaser.vy == 10 * 256 && chaser.params[TEST_HERO_PARAM_ACTION] == 90);
    dgx_ge_actor_t leftward;
    dgx_ge_actor_start(&leftward, &anim, TEST_HERO_SCRIPT_CHASE, 0, 0, 0);
    leftward.flip = true;
    dgx_ge_actor_advance(&leftward, 50, say, NULL);
    CHECK(leftward.vx == -60 * 256 && leftward.vy == 80 * 256); /* forward is left */

    /* appear ... with: the game starts the calf in the say and takes the params given to it; outside that say there are none */
    dgx_ge_actor_t breeder;
    dgx_ge_actor_start(&breeder, &anim, TEST_HERO_SCRIPT_BREED, 0, 0, 0);
    dgx_ge_actor_advance(&breeder, 50, breed_say, NULL);
    CHECK(calves == 1 && calf_params == 2 && dgx_ge_actor_x(&calf) == 5 && dgx_ge_actor_y(&calf) == 6);
    CHECK(calf.params[TEST_HERO_PARAM_HP] == 61 && calf.entered_params[TEST_HERO_PARAM_HP] == 61 && calf.params[TEST_HERO_PARAM_ACTION] == 7);
    CHECK(dgx_ge_anim_said_params(&calf) == 0);
    dgx_ge_actor_start(&breeder, &anim, TEST_HERO_SCRIPT_BREED, 0, 0, 0);
    breeder.params[TEST_HERO_PARAM_POWER] = 100000; /* given past 16 bits: whole */
    dgx_ge_actor_advance(&breeder, 50, breed_say, NULL);
    CHECK(calves == 2 && calf.params[TEST_HERO_PARAM_HP] == 50001 && calf.entered_params[TEST_HERO_PARAM_HP] == 50001);

    /* a watch set again with other events after a step took the feet off the floor: the loss is told, with the new event */
    dgx_ge_actor_t hopper2;
    dgx_ge_actor_start(&hopper2, &anim, TEST_HERO_SCRIPT_HOP, 0, 0, 0);
    hopper2.terrain = &edge_terrain;
    dgx_ge_actor_advance(&hopper2, 200, say, NULL);
    CHECK(hopper2.script == TEST_HERO_SCRIPT_HURT && hopper2.entered_ms >= 50 && hopper2.entered_ms <= 51 && hopper2.entered_x == 120 * 256);

    /* a wall met beside (either side) while walking: hit at the moment, 150 points at 40 a second */
    dgx_ge_actor_t waller;
    dgx_ge_actor_start(&waller, &anim, TEST_HERO_SCRIPT_WALKWALL, 0, 0, 0);
    waller.terrain = &wall_terrain;
    dgx_ge_actor_advance(&waller, 3800, say, NULL); /* hurt at the wall at 3750 (the next ms after the last one clear); it walks on, its velocity kept */
    CHECK(waller.script == TEST_HERO_SCRIPT_HURT && waller.entered_ms >= 3750 && waller.entered_ms <= 3751 && waller.entered_x >> 8 == 150);

    /* a ray: from above down to the floor it is hit at 200; along above the floor nothing */
    dgx_ge_contact_t hit;
    CHECK(dgx_ge_terrain_ray(&floor_terrain, 10 * 256, 0, 50 * 256, 400 * 256, 8, &hit) && hit.y == 200 * 256 && hit.surface == 7);
    CHECK(abs(hit.x - 30 * 256) <= 256); /* half way across, a point either way */
    CHECK(!dgx_ge_terrain_ray(&floor_terrain, 0, 100 * 256, 300 * 256, 150 * 256, 8, &hit));

    /* a broken blob is refused */
    uint8_t *broken = malloc(n);
    memcpy(broken, blob, n);
    broken[0] ^= 1;
    CHECK(!dgx_ge_anim_open(&anim, broken, n));
    memcpy(broken, blob, n);
    CHECK(!dgx_ge_anim_open(&anim, broken, n - 1));
    free(broken);
    clock_tests(&pack);
    free(data);
    CHECK_DONE();
}
