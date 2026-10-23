/*
 * An actor's orientation: its mirror (flip) and its quarter turn, one of the eight of DGX (build/orient/orient.pack,
 * made by make_orient_pack.py). The eight as DGX draws them, pixel by pixel against DGX's own model of them, at the
 * scale of DGX and larger (the engine's own), their boxes and what a scene draws anew when a picture turns; masks
 * placed, met and touched turned; a script's rotate by a number, a param, a variable of the game, into a turn, rounded
 * and round the circle; what is said in an actor's axes turned (velocity, accel, dx, dy, the picture's offset, a
 * release, a path, a point and the thing held there, front of a contact, a sense) and the turn read (rotation,
 * its.rotation, from C); records of the world, a save, a scene laid, a show, a crowd keep it; blobs of the versions
 * before refused.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "dgx_ge_anim.h"
#include "dgx_ge_crowd.h"
#include "dgx_ge_game.h"
#include "dgx_ge_glue.h"
#include "dgx_ge_mask.h"
#include "dgx_ge_pockets.h"
#include "dgx_ge_save.h"
#include "dgx_ge_scene.h"
#include "dgx_ge_show.h"
#include "dgx_ge_world.h"
#include "drivers/vscreen.h"
#include "orient_pack.h"

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

/* DGX's model of its eight (its test/host/test_image.c): where the pixel sx, sy of a picture w x h goes in the picture turned */
static void dgx_turned(dgx_output_orientation_t turn, int w, int h, int sx, int sy, int *dx, int *dy)
{
    switch (turn) {
    case DgxOutputNormal: *dx = sx, *dy = sy; break;
    case DgxOutputMirrorX: *dx = w - 1 - sx, *dy = sy; break;
    case DgxOutputMirrorY: *dx = sx, *dy = h - 1 - sy; break;
    case DgxOutputRotate180: *dx = w - 1 - sx, *dy = h - 1 - sy; break;
    case DgxOutputTranspose: *dx = sy, *dy = sx; break;
    case DgxOutputRotate90CCW: *dx = sy, *dy = w - 1 - sx; break;
    case DgxOutputRotate90CW: *dx = h - 1 - sy, *dy = sx; break;
    default: *dx = h - 1 - sy, *dy = w - 1 - sx; break;
    }
}

/* The orientation as it is meant: mirrored left to right (flip), then a quarter turn clockwise on the screen, y down, so many times. */
static void meant(unsigned o, int x, int y, int *wx, int *wy)
{
    if (o & 1) x = -x;
    for (unsigned k = 0; k < (o >> 1 & 3); ++k) {
        const int t = x;
        x = -y, y = t; /* right goes down: clockwise as one sees it */
    }
    *wx = x, *wy = y;
}

/* What the game's terrain says to the scripts: the sides a mask touches (the feet: a floor below once there is one), and a sense. */
static bool                  floor_now;
static const dgx_ge_actor_t *seen;
static int                   seen_dx, seen_dy;

static uint8_t sides(const dgx_ge_actor_t *a, int32_t x, int32_t y, int mask, int reach, void *ctx)
{
    (void)a, (void)x, (void)y, (void)reach, (void)ctx;
    return floor_now && mask == 2 ? DGX_GE_TOUCH_BELOW : 0;
}

static bool sense(const dgx_ge_actor_t *a, const dgx_ge_sense_t *q, dgx_ge_sensed_t *found, void *ctx)
{
    (void)ctx;
    if (!seen) return false;
    const dgx_ge_sensed_t f = {.dx = seen_dx, .dy = seen_dy, .actor = seen};
    if (!dgx_ge_actor_sees(a, q->where, &f, false)) return false;
    *found = f;
    return true;
}

static dgx_ge_terrain_t terrain = {.sides = sides, .sense = sense};

/* An actor of TURNER at 50, 50 in a script from 0, its param deg given, its steps up to now made. */
static dgx_ge_actor_t run(const dgx_ge_anim_t *anim, int script, int deg, uint32_t now_ms)
{
    dgx_ge_actor_t a;
    dgx_ge_actor_start(&a, anim, script, 50, 50, 0);
    a.params[OR_TURNER_PARAM_DEG] = deg;
    a.terrain                     = &terrain;
    dgx_ge_actor_advance(&a, now_ms, NULL, NULL);
    return a;
}

/* A body given its flip as before (true): the mirror; its byte holds the turn too, the size as before. */
_Static_assert(sizeof(dgx_ge_body_t) == sizeof(void *) + 3 * sizeof(int) + 4, "a body: the same size");
static const dgx_ge_body_t old_body = {.flip = true};

int main(int argc, char **argv)
{
    CHECK(old_body.flip == dgx_ge_orient(true, 0)); /* true, as before: the mirror */
    size_t        size = 0;
    uint8_t      *data = read_file(argc > 1 ? argv[1] : "build/orient/orient.pack", &size);
    dgx_ge_pack_t pack;
    CHECK(data && dgx_ge_pack_open(&pack, data, size));
    dgx_ge_kinds_t kinds;
    CHECK(dgx_ge_kinds_init(&kinds, &pack, 8));
    const dgx_ge_anim_t *anim = dgx_ge_kind(&kinds, OR_BLOB_TURNER);
    CHECK(anim != NULL);
    if (!anim) CHECK_DONE();
    dgx_image_t arrow, hilt, bar;
    CHECK(dgx_ge_pack_image(&pack, OR_IMG_ARROW, &arrow) && dgx_ge_pack_image(&pack, OR_IMG_HILT, &hilt) && dgx_ge_pack_image(&pack, OR_IMG_BAR, &bar));
    CHECK(arrow.width == 5 && arrow.height == 3 && arrow.origin_x == 1 && arrow.origin_y == 2);

    /*
     * The eight: an orientation is flip | quarter turns << 1, mirrored and then turned clockwise; it undoes; DGX's
     * orientation of it sends every pixel where it means, as DGX's own model says; drawn, at the scale of DGX and
     * three times larger (the engine's own), every pixel is there and nothing else, in its box (a quarter turn
     * swaps its width and height)
     */
    dgx_screen_t *s8 = dgx_vscreen_init(48, 48, 8, DgxScreenRGB);
    uint8_t      *px = ((dgx_vscreen_t *)s8)->v_array;
    CHECK(dgx_ge_orient(false, 0) == 0 && dgx_ge_orient(true, 0) == 1 && dgx_ge_orient(false, 1) == 2 && dgx_ge_orient(true, 3) == 7 && dgx_ge_orient(false, 5) == 2);
    for (unsigned o = 0; o < 8; ++o) {
        for (int y = -3; y <= 3; ++y)
            for (int x = -3; x <= 3; ++x) {
                int32_t ax = x, ay = y;
                int     mx, my;
                dgx_ge_orient_apply(o, &ax, &ay);
                meant(o, x, y, &mx, &my);
                CHECK(ax == mx && ay == my);
                float fx = (float)x, fy = (float)y;
                dgx_ge_orient_apply_f(o, &fx, &fy);
                CHECK(fx == (float)mx && fy == (float)my);
                dgx_ge_orient_apply(dgx_ge_orient_inverse(o), &ax, &ay);
                CHECK(ax == x && ay == y);
            }
        const dgx_output_orientation_t d = dgx_ge_orient_output(o);
        int                            ox, oy;
        dgx_turned(d, arrow.width, arrow.height, arrow.origin_x, arrow.origin_y, &ox, &oy);
        for (int sy = 0; sy < arrow.height; ++sy)
            for (int sx = 0; sx < arrow.width; ++sx) {
                int     dx, dy;
                int32_t wx = sx - arrow.origin_x, wy = sy - arrow.origin_y;
                dgx_turned(d, arrow.width, arrow.height, sx, sy, &dx, &dy);
                dgx_ge_orient_apply(o, &wx, &wy);
                CHECK(dx - ox == wx && dy - oy == wy);
            }
        for (int n = 1; n <= 3; n += 2) {
            memset(px, 0, 48 * 48);
            const dgx_ge_item_t item = {.image = &arrow, .flags = (uint8_t)(o & 1 ? DGX_GE_BLIT_FLIP_X : 0), .turn = (uint8_t)(o >> 1), .scale = (uint8_t)n};
            CHECK(dgx_ge_item_orient(&item) == o);
            dgx_ge_item_draw(s8, &item, 24, 24, dgx_ge_rect(0, 0, 48, 48), dgx_ge_rect(0, 0, 48, 48));
            int changed = 0, l = 99, t = 99, r = -1, b = -1;
            for (int y = 0; y < 48; ++y)
                for (int x = 0; x < 48; ++x)
                    if (px[y * 48 + x]) {
                        ++changed;
                        l = x < l ? x : l, t = y < t ? y : t, r = x > r ? x : r, b = y > b ? y : b;
                    }
            CHECK(changed == 15 * n * n);
            for (int sy = 0; sy < arrow.height; ++sy)
                for (int sx = 0; sx < arrow.width; ++sx) {
                    int32_t wx = sx - arrow.origin_x, wy = sy - arrow.origin_y;
                    dgx_ge_orient_apply(o, &wx, &wy);
                    for (int k = 0; k < n * n; ++k) CHECK(px[(24 + wy * n + k / n) * 48 + 24 + wx * n + k % n] == sy * 5 + sx + 1);
                }
            const dgx_ge_rect_t box = dgx_ge_image_box_scaled(&arrow, 24, 24, o, n);
            CHECK(dgx_ge_rect_equal(box, dgx_ge_rect(l, t, r - l + 1, b - t + 1)));
            CHECK(box.w == (o & 2 ? 3 : 5) * n && box.h == (o & 2 ? 5 : 3) * n);
        }
    }
    /* a bool flip where an orientation goes: the mirror, as always */
    CHECK(dgx_ge_rect_equal(dgx_ge_image_box(&arrow, 24, 24, true), dgx_ge_rect(24 - 3, 22, 5, 3)));
    dgx_screen_destroy(&s8);

    /* a scene: a picture turned is drawn anew where it was and where it is; turned alone (same place, same picture), too */
    {
        enum { W = 24, H = 16 };
        dgx_screen_t   *display = dgx_vscreen_init(W, H, 16, DgxScreenRGB);
        dgx_ge_frame_t *frame   = dgx_ge_frame_create(display, 0, 0, W, H);
        dgx_ge_scene_t *scene   = dgx_ge_scene_create(frame, 1, 1, 4);
        CHECK(display && frame && scene);
        static const uint8_t ink[15] = {0, 0, 0, 0, 7};
#define AT(x, y) frame->canvas.pixels[(y) * W + (x)]
        const int turns[] = {1, 0, 0, 3, 1};
        for (int k = 0; k < 5; ++k) {
            const uint32_t sent = frame->pixels_sent;
            dgx_ge_scene_actors_begin(scene);
            dgx_ge_scene_actor(scene, &(dgx_ge_item_t){.image = &bar, .map = ink, .x = 8, .y = 7, .turn = (uint8_t)turns[k]});
            dgx_ge_scene_render(scene);
            int drawn = 0, right = 1;
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x) {
                    /* the bar from its origin: right (0), down (1), up (3) */
                    const bool on = turns[k] == 0 ? y == 7 && x >= 8 && x < 14 : turns[k] == 1 ? x == 8 && y >= 7 && y < 13 : x == 8 && y <= 7 && y > 1;
                    drawn += AT(x, y) == 7;
                    if ((AT(x, y) == 7) != on) right = 0;
                }
            CHECK(right && drawn == 6);
            if (k == 2) CHECK(frame->pixels_sent == sent); /* nothing changed: nothing sent */
        }
#undef AT
        dgx_ge_scene_destroy(&scene);
        dgx_ge_frame_destroy(&frame);
        dgx_screen_destroy(&display);
    }

    /*
     * Masks: placed turned (a quarter turn swaps width and height), each point where the orientation sends it; two
     * bodies meet where their turned masks do; feet touch the floor below them, turned the floor is where they are
     * (a quarter turn clockwise: on the left); a ray meets a body where its turned masks lie
     */
    dgx_ge_mask_t masks[4];
    const int     nmasks = dgx_ge_pack_masks(&pack, OR_IMG_ARROW, masks, 4);
    CHECK(nmasks == 2 && masks[0].index == 1 && masks[1].index == 2);
    const dgx_ge_mask_t *body = &masks[0], *feet = &masks[1];
    for (unsigned o = 0; o < 8; ++o) {
        const dgx_ge_rect_t r = dgx_ge_mask_place(body, 20, 20, o);
        CHECK(r.w == (o & 2 ? 3 : 5) && r.h == (o & 2 ? 5 : 3));
        int in = 0;
        for (int y = r.y; y < r.y + r.h; ++y)
            for (int x = r.x; x < r.x + r.w; ++x) in += dgx_ge_mask_has(body, r, o, x, y);
        CHECK(in == 14);
        /* the corner the body has not: forward and up of its picture (3, -2 from the origin) */
        int32_t cx = 3, cy = -2;
        dgx_ge_orient_apply(o, &cx, &cy);
        CHECK(!dgx_ge_mask_has(body, r, o, 20 + cx, 20 + cy));
        int32_t ex = -1, ey = 0;
        dgx_ge_orient_apply(o, &ex, &ey);
        CHECK(dgx_ge_mask_has(body, r, o, 20 + ex, 20 + ey));
        /* against a body unturned one point to the right: they meet where both have points */
        dgx_ge_area_t areas[DGX_GE_AREAS_MAX];
        const int     n = dgx_ge_masks_overlap(body, 20, 20, o, body, 21, 20, 0, 4, areas);
        const dgx_ge_rect_t s = dgx_ge_mask_place(body, 21, 20, 0);
        int                 both = 0;
        for (int y = 0; y < 48; ++y)
            for (int x = 0; x < 48; ++x)
                both += x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h && dgx_ge_mask_has(body, r, o, x, y) && x >= s.x && x < s.x + s.w &&
                        y >= s.y && y < s.y + s.h && dgx_ge_mask_has(body, s, 0, x, y);
        int points = 0;
        for (int i = 0; i < n; ++i) points += areas[i].points;
        CHECK(both > 0 && points == both);
    }
    {
        dgx_ge_area_t       areas[DGX_GE_TOUCH_SIDES];
        /* unturned, the feet a row at y 20 from x 19 to 23; turned clockwise, a column at x 20 from y 19 to 23 */
        const dgx_ge_rect_t floor = dgx_ge_rect(22, 21, 10, 4), wall = dgx_ge_rect(16, 0, 4, 48);
        CHECK(dgx_ge_mask_touch_rect(feet, dgx_ge_mask_place(feet, 20, 20, 0), 0, floor, 1, areas) == DGX_GE_TOUCH_BELOW && areas[1].points == 2);
        CHECK(dgx_ge_mask_touch_rect(feet, dgx_ge_mask_place(feet, 20, 20, 2), 2, wall, 1, areas) == DGX_GE_TOUCH_LEFT && areas[3].points == 5);
        CHECK(dgx_ge_mask_touch_rect(feet, dgx_ge_mask_place(feet, 20, 20, 2), 2, floor, 1, areas) == 0); /* its feet are not over that floor now */
        /* a quarter turn the other way (270): the feet on the right */
        CHECK(dgx_ge_mask_touch_rect(feet, dgx_ge_mask_place(feet, 20, 20, 6), 6, dgx_ge_rect(21, 0, 4, 48), 1, areas) == DGX_GE_TOUCH_RIGHT);
    }
    {
        dgx_ge_collisions_t *c = dgx_ge_collisions_create(4, 8);
        dgx_ge_actor_t       a;
        dgx_ge_actor_start(&a, anim, OR_TURNER_SCRIPT_STAND, 20, 20, 0);
        dgx_ge_actor_advance(&a, 0, NULL, NULL);
        a.masks = 1u << 2; /* the feet */
        dgx_ge_actor_set_rotation(&a, 90);
        dgx_ge_collisions_set_actor(c, 0, &pack, &a, 1, 1);
        dgx_ge_ray_hit_t hit;
        /* the feet turned: a column at x 20 from y 19 to 23, not the row at y 20 from x 19 to 23 */
        CHECK(dgx_ge_collisions_ray(c, 20, 30, 20, 10, 2, 0, 0, &hit) && hit.handle == 0 && hit.y == 23);
        CHECK(!dgx_ge_collisions_ray(c, 23, 30, 23, 21, 2, 0, 0, &hit));
        dgx_ge_actor_set_rotation(&a, 0);
        dgx_ge_collisions_set_actor(c, 0, &pack, &a, 1, 1);
        CHECK(dgx_ge_collisions_ray(c, 23, 30, 23, 10, 2, 0, 0, &hit) && hit.y == 20);
        dgx_ge_collisions_destroy(&c);
    }

    /* rotate by a number, a param, a variable of the game: degrees to the nearest quarter turn (a half away from zero), round the circle */
    dgx_ge_vars_t  vars;
    size_t         vars_size;
    const uint8_t *vars_blob = dgx_ge_pack_blob(&pack, OR_BLOB_VARIABLES, &vars_size);
    CHECK(dgx_ge_vars_open(&vars, vars_blob, vars_size));
    dgx_ge_game_vars = &vars;
    static const struct {
        int script, degrees, turn;
    } by[] = {
        {OR_TURNER_SCRIPT_C45, 45, 1},   {OR_TURNER_SCRIPT_C44, 44, 0},   {OR_TURNER_SCRIPT_CM45, -45, 3},  {OR_TURNER_SCRIPT_C135, 135, 2},
        {OR_TURNER_SCRIPT_CM90, -90, 3}, {OR_TURNER_SCRIPT_C450, 450, 1}, {OR_TURNER_SCRIPT_CM135, -135, 2}, {OR_TURNER_SCRIPT_C225, 225, 3},
    };
    for (size_t i = 0; i < sizeof by / sizeof by[0]; ++i) {
        const dgx_ge_actor_t c = run(anim, by[i].script, 0, 0), p = run(anim, OR_TURNER_SCRIPT_BY_PARAM, by[i].degrees, 0);
        vars.values[OR_GAME_VAR_FACING] = by[i].degrees;
        const dgx_ge_actor_t v = run(anim, OR_TURNER_SCRIPT_BY_VAR, 0, 0);
        dgx_ge_actor_t       g = c;
        dgx_ge_actor_set_rotation(&g, by[i].degrees);
        CHECK(c.turn == by[i].turn && p.turn == by[i].turn && v.turn == by[i].turn && g.turn == by[i].turn);
        CHECK(dgx_ge_actor_rotation(&c) == by[i].turn * 90 && !c.flip);
    }
    dgx_ge_game_vars = NULL;
    CHECK(run(anim, OR_TURNER_SCRIPT_TO_180, 0, 0).turn == 2 && run(anim, OR_TURNER_SCRIPT_TO_EXPR, 3, 0).turn == 3 &&
          run(anim, OR_TURNER_SCRIPT_TO_EXPR, -1, 0).turn == 3 && run(anim, OR_TURNER_SCRIPT_TWICE, 0, 0).turn == 3);
    {
        dgx_ge_actor_t a = run(anim, OR_TURNER_SCRIPT_STAND, 0, 0);
        dgx_ge_actor_set_rotation(&a, -2147483647 - 1); /* any whole number */
        CHECK(a.turn == ((-2147483648LL - 45) / 90 % 4 + 4) % 4);
        a.flip = true, a.turn = 1;
        CHECK(dgx_ge_actor_orient(&a) == 3 && dgx_ge_actor_rotation(&a) == 90);
    }

    /* what is said in its axes, turned a quarter clockwise: forward is down, down is left */
    {
        dgx_ge_actor_t a = run(anim, OR_TURNER_SCRIPT_MOVES, 0, 0);
        CHECK(a.turn == 1 && a.vx == 0 && a.vy == 40 * 256 && a.ax == -300 * 256 && a.ay == 0);
        CHECK(a.params[OR_TURNER_PARAM_GOT] == 40 && a.params[OR_TURNER_PARAM_D] == 0); /* vx, vy read in its axes */
        CHECK(dgx_ge_actor_x(&a) == 48 && dgx_ge_actor_y(&a) == 53);                    /* dx 3: down; dy 2: left */
        int x, y;
        dgx_ge_actor_picture_at(&a, &x, &y);
        CHECK(x == 47 && y == 55); /* at 2, 1 */
        dgx_ge_item_t item = {.layer = 1};
        CHECK(dgx_ge_item_actor(&item, &a, &pack, NULL, &(dgx_image_t){0}) && item.x == 47 && item.y == 55 && item.turn == 1 && !(item.flags & DGX_GE_BLIT_FLIP_X));
        /* held at its hand (3, -1), by the hilt's own (1, 0): 2, -1 of its axes, the hilt turned with it */
        dgx_ge_item_t child;
        int           z;
        CHECK(dgx_ge_item_held(&pack, &item, OR_IMG_ARROW, OR_POINT_HAND, &hilt, NULL, OR_IMG_HILT, &child, &z));
        CHECK(child.x == 48 && child.y == 57 && child.turn == 1 && child.layer == 1);
        /* mirrored too (3): the hand less the grip, 2, -1, mirrored is -2, -1, turned clockwise 1, -2 */
        a.flip = true;
        CHECK(dgx_ge_item_actor(&item, &a, &pack, NULL, &(dgx_image_t){0}) && (item.flags & DGX_GE_BLIT_FLIP_X) && dgx_ge_item_orient(&item) == 3);
        CHECK(dgx_ge_item_held(&pack, &item, OR_IMG_ARROW, OR_POINT_HAND, &hilt, NULL, OR_IMG_HILT, &child, &z));
        CHECK(child.x == item.x + 1 && child.y == item.y - 2 && dgx_ge_item_orient(&child) == 3);
        a.flip = false;
        dgx_ge_actor_advance(&a, 150, NULL, NULL);
        CHECK(a.released.image == OR_IMG_HILT && a.released.vx == 60 && a.released.vy == 120); /* release 120 -60 */
        CHECK(a.shot.kind == OR_BLOB_TURNER && a.shot.vx == 0 && a.shot.vy == 300); /* shoot 300 0: down */
        const dgx_ge_actor_t st = run(anim, OR_TURNER_SCRIPT_STEERS, 0, 0);
        CHECK(st.vx == -10 * 256 && st.vy == 0); /* seek 0, 50 of its axes: left */
    }
    {
        dgx_ge_actor_t a = run(anim, OR_TURNER_SCRIPT_WALK, 0, 500);
        CHECK(dgx_ge_actor_x(&a) == 50 && dgx_ge_actor_y(&a) == 55); /* t * 10 forward: down */
        dgx_ge_actor_advance(&a, 1500, NULL, NULL);
        CHECK(a.turn == 0 && dgx_ge_actor_x(&a) == 50 && dgx_ge_actor_y(&a) == 65); /* turned back, the path goes on as it began */
    }

    /* the turn read: rotation; of the one sensed, its.rotation, and where it is in the axes of the one looking */
    {
        const dgx_ge_actor_t r = run(anim, OR_TURNER_SCRIPT_READS, 0, 0);
        CHECK(r.params[OR_TURNER_PARAM_GOT] == 270 && r.params[OR_TURNER_PARAM_D] == 2);
        dgx_ge_actor_t other = run(anim, OR_TURNER_SCRIPT_STAND, 0, 0);
        dgx_ge_actor_set_rotation(&other, 180);
        seen = &other, seen_dx = 0, seen_dy = 10; /* below it */
        dgx_ge_actor_t l = run(anim, OR_TURNER_SCRIPT_LOOKS, 0, 0);
        CHECK(l.params[OR_TURNER_PARAM_D] == 10 && l.params[OR_TURNER_PARAM_DX] == 10 && l.params[OR_TURNER_PARAM_GOT] == 180); /* ahead of it */
        dgx_ge_actor_set_rotation(&other, 90);
        l = run(anim, OR_TURNER_SCRIPT_LOOKS, 0, 0);
        CHECK(l.params[OR_TURNER_PARAM_D] == -1 && l.params[OR_TURNER_PARAM_GOT] == 0); /* not the one it looks for */
        seen = NULL;
        /* an expression of the actor, as a dialog's condition is worked out for the one choosing: rotation == 90 */
        static const uint8_t is_90[] = {7, 55, 1, 0, 0, 0xB4, 0x42, 23};
        CHECK(dgx_ge_actor_eval(&other, is_90) == 1 && dgx_ge_actor_eval(&l, is_90) == 1 && dgx_ge_actor_eval(&r, is_90) == 0);
    }

    /* a mask watched in front: as it faces, the floor below its feet is in front only when it faces down */
    {
        floor_now      = false;
        dgx_ge_actor_t a = run(anim, OR_TURNER_SCRIPT_WATCHES, 0, 50), b = run(anim, OR_TURNER_SCRIPT_WATCHES_DOWN, 0, 50);
        floor_now = true;
        dgx_ge_actor_advance(&a, 350, NULL, NULL);
        dgx_ge_actor_advance(&b, 350, NULL, NULL);
        CHECK(a.script == OR_TURNER_SCRIPT_WATCHES && a.params[OR_TURNER_PARAM_GOT] == 0);
        CHECK(b.script == OR_TURNER_SCRIPT_GOT_TOUCHED && b.params[OR_TURNER_PARAM_GOT] == 1);
        floor_now = false;
    }

    /* records of the world: the turn it entered with, kept and given back; laid turned; saved and loaded */
    {
        dgx_ge_actor_t a = run(anim, OR_TURNER_SCRIPT_STAND, 0, 0);
        a.flip = a.entered_flip = true;
        a.turn = a.entered_turn = 3;
        dgx_ge_object_t o = {.scene = 4};
        dgx_ge_object_of(&o, &a);
        CHECK(o.flip && o.turn == 3);
        dgx_ge_actor_t back;
        dgx_ge_object_actor(&o, anim, &back);
        CHECK(back.flip && back.turn == 3 && back.entered_turn == 3);
        dgx_ge_world_t *w = dgx_ge_world_create(4);
        o.turn = 2;
        const int32_t h = dgx_ge_world_add(w, &o);
        uint8_t       buf[512];
        dgx_ge_save_t s;
        dgx_ge_save_begin(&s, buf, sizeof buf, 7, 0);
        dgx_ge_world_save(w, &s);
        const size_t n = dgx_ge_save_end(&s);
        CHECK(n > 0 && buf[4] == 4);
        dgx_ge_world_t *v = dgx_ge_world_create(4);
        dgx_ge_load_t   l;
        dgx_ge_object_t got;
        CHECK(dgx_ge_load_open(&l, buf, n, 7, NULL) && dgx_ge_world_load(v, &l, 0) && dgx_ge_world_get(v, h, &got) && got.flip && got.turn == 2 && got.scene == 4);
        /* a save of version 3 has no turn: its things faced as they were saved, unturned */
        o.turn = 0;
        dgx_ge_world_put(w, h, &o);
        dgx_ge_save_begin(&s, buf, sizeof buf, 7, 0);
        dgx_ge_world_save(w, &s);
        const size_t m = dgx_ge_save_end(&s);
        buf[4]         = 3;
        CHECK(dgx_ge_load_open(&l, buf, m, 7, NULL) && dgx_ge_world_load(v, &l, 0) && dgx_ge_world_get(v, h, &got) && got.flip && got.turn == 0);
        /* laid: so many things of a pocket into the world, facing so */
        dgx_ge_world_clear(w);
        const dgx_ge_pocket_t thing = {.kind = OR_BLOB_TURNER, .script = OR_TURNER_SCRIPT_STAND, .count = 1};
        const dgx_ge_lay_t    how   = {.step = 8, .reach = 0, .heap = -1};
        const int32_t         laid  = dgx_ge_world_lay(w, &kinds, &thing, 1, 4, 30, 30, dgx_ge_orient(false, 1), -1, 0, &how);
        CHECK(laid >= 0 && dgx_ge_world_get(w, laid, &got) && !got.flip && got.turn == 1);
        dgx_ge_world_destroy(&w);
        dgx_ge_world_destroy(&v);
    }

    /* a show: its actors and pictures turned as laid; a show of the version before refused */
    {
        size_t         n;
        const uint8_t *blob = dgx_ge_pack_blob(&pack, OR_BLOB_SHOW, &n);
        dgx_ge_show_t  show;
        CHECK(blob && dgx_ge_show_open(&show, blob, n) && dgx_ge_show_start(&show, &kinds, 0));
        CHECK(show.cast[0].flip && show.cast[0].turn == 3 && !show.cast[1].flip && show.cast[1].turn == 0);
        enum { W = 64, H = 32 };
        dgx_screen_t   *display = dgx_vscreen_init(W, H, 16, DgxScreenRGB);
        dgx_ge_frame_t *frame   = dgx_ge_frame_create(display, 0, 0, W, H);
        dgx_ge_scene_t *scene   = dgx_ge_scene_create(frame, 1, 4, 8);
        dgx_ge_show_backdrop(&show, &pack, NULL, scene);
        CHECK(scene->set_count == 2 && scene->set[0].turn == 1 && scene->set[1].turn == 0);
        dgx_ge_show_play(&show, 10, NULL, NULL);
        dgx_ge_scene_actors_begin(scene);
        dgx_ge_show_items(&show, &pack, NULL, scene, NULL, NULL);
        CHECK(scene->actor_count == 2 && scene->actors[0].turn == 3 && (scene->actors[0].flags & DGX_GE_BLIT_FLIP_X) && scene->actors[1].turn == 0);
        dgx_ge_show_stop(&show);
        uint8_t *old = malloc(n);
        memcpy(old, blob, n);
        old[4] = 1;
        CHECK(!dgx_ge_show_open(&show, old, n));
        free(old);

        /* a crowd: a group turned 180, one of any turn; a crowd of the version before refused */
        blob = dgx_ge_pack_blob(&pack, OR_BLOB_CROWD, &n);
        dgx_ge_crowd_t crowd;
        CHECK(blob && dgx_ge_crowd_open(&crowd, blob, n) && dgx_ge_crowd_start(&crowd, &pack, &kinds, NULL, 0));
        int half = 0, any = 0;
        for (int i = 0; i < crowd.places; ++i) {
            if (crowd.members[i].kind == DGX_GE_CROWD_FREE) continue;
            if (dgx_ge_actor_x(&crowd.members[i].actor) < 100) half += crowd.members[i].actor.turn == 2;
            else any |= 1 << crowd.members[i].actor.turn;
        }
        CHECK(half == 2 && any > 1); /* the four strewn by the dice not all one way */
        dgx_ge_scene_actors_begin(scene);
        CHECK(dgx_ge_crowd_items(&crowd, NULL, scene));
        int turned = 0;
        for (int i = 0; i < scene->actor_count; ++i) turned += scene->actors[i].turn == 2;
        CHECK(scene->actor_count == 6 && turned >= 2);
        dgx_ge_crowd_stop(&crowd);
        old = malloc(n);
        memcpy(old, blob, n);
        old[4] = 1;
        CHECK(!dgx_ge_crowd_open(&crowd, old, n));
        free(old);
        dgx_ge_scene_destroy(&scene);
        dgx_ge_frame_destroy(&frame);
        dgx_screen_destroy(&display);
    }

    /* scripts of the version before (no rotate there) refused */
    {
        size_t         n;
        const uint8_t *blob = dgx_ge_pack_blob(&pack, OR_BLOB_TURNER, &n);
        uint8_t       *old  = malloc(n);
        memcpy(old, blob, n);
        dgx_ge_anim_t opened;
        CHECK(dgx_ge_anim_open(&opened, old, n));
        old[4] = 10;
        CHECK(!dgx_ge_anim_open(&opened, old, n));
        free(old);
    }
    dgx_ge_vars_free(&vars);
    dgx_ge_kinds_free(&kinds);
    free(data);
    CHECK_DONE();
}
