/* The scene: layers and their order, the window, clips, and drawing only what has changed. */
#include <stdio.h>
#include <string.h>

#if defined(__has_include)
#if __has_include("dgx_overlay.h")
#include "dgx_overlay.h"
#endif
#endif
#include "check.h"
#include "dgx_ge_scene.h"
#include "dgx_draw.h"
#include "drivers/vscreen.h"

#define W 32
#define H 16

/* an open space for the test: everything right of x 12 is hidden; it tells what it was asked */
static dgx_ge_rect_t asked_body;
static int           asked_x, asked_y, asked;
static bool          asked_inside;
static dgx_ge_rect_t left_of_12(const void *space, dgx_ge_rect_t body, int feet_x, int feet_y, bool inside)
{
    (void)space;
    asked_body = body, asked_x = feet_x, asked_y = feet_y, asked_inside = inside, ++asked;
    return dgx_ge_rect(-100, -100, 112, 400);
}

static uint8_t at(dgx_ge_frame_t *f, int x, int y)
{
    return f->canvas.pixels[y * W + x];
}

int main(void)
{
    dgx_screen_t   *display = dgx_vscreen_init(W, H, 16, DgxScreenRGB);
    dgx_ge_frame_t *frame   = dgx_ge_frame_create(display, 0, 0, W, H);
    CHECK(display && frame);
    dgx_ge_scene_t *scene = dgx_ge_scene_create(frame, 3, 8, 8);
    CHECK(scene);

    /* a square of 4x4 with its origin at the bottom-left, a 1-bit one of 2x2 */
    static const uint8_t sq_px[] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11};
    static const uint8_t dot_px[] = {0xC0, 0xC0};
    dgx_image_t sq  = {4, 4, 0, 3, DGX_IMAGE_4BPP, sq_px};
    dgx_image_t dot = {2, 2, 0, 1, DGX_IMAGE_1BPP, dot_px};
    static const uint8_t red[15] = {5}, blue[15] = {6};

    /* the set: a red square in layer 0 at (2, 5); a blue one in layer 2 at (4, 5), before anything of layer 1 */
    CHECK(dgx_ge_scene_add(scene, &(dgx_ge_item_t){.image = &sq, .map = red, .x = 2, .y = 5, .layer = 0}) == 0);
    CHECK(dgx_ge_scene_add(scene, &(dgx_ge_item_t){.image = &sq, .map = blue, .x = 4, .y = 5, .layer = 2}) == 1);
    dgx_ge_scene_reset(scene, 1);
    CHECK(scene->set_count == 0);
    dgx_ge_scene_add(scene, &(dgx_ge_item_t){.image = &sq, .map = red, .x = 2, .y = 5, .layer = 0});
    int front = dgx_ge_scene_add(scene, &(dgx_ge_item_t){.image = &sq, .map = blue, .x = 4, .y = 5, .layer = 2});

    /* an actor in layer 1, between them: a dot of 7 at (5, 4) */
    dgx_ge_scene_actors_begin(scene);
    CHECK(dgx_ge_scene_actor(scene, &(dgx_ge_item_t){.image = &dot, .x = 5, .y = 4, .layer = 1, .color = 7}));
    dgx_ge_scene_render(scene);
    CHECK(at(frame, 0, 0) == 1);                     /* the paper */
    CHECK(at(frame, 2, 2) == 5 && at(frame, 3, 5) == 5); /* red, behind */
    CHECK(at(frame, 5, 3) == 6 && at(frame, 6, 4) == 6); /* blue in front of the dot */
    CHECK(at(frame, 7, 5) == 6 && at(frame, 8, 5) == 1);
    CHECK(frame->dirty_count == 0 && frame->pixels_sent == W * H);

    /* the dot moves out from behind the blue square: only where it was and is is drawn */
    uint32_t sent = frame->pixels_sent;
    dgx_ge_scene_actors_begin(scene);
    dgx_ge_scene_actor(scene, &(dgx_ge_item_t){.image = &dot, .x = 20, .y = 10, .layer = 1, .color = 7});
    dgx_ge_scene_render(scene);
    CHECK(at(frame, 20, 9) == 7 && at(frame, 21, 10) == 7);
    CHECK(at(frame, 5, 3) == 6); /* the blue square is whole where the dot was */
    CHECK(frame->pixels_sent - sent <= 2 * 4 + 100); /* two dots, merged at most */

    /* nothing changed: nothing is sent */
    sent = frame->pixels_sent;
    dgx_ge_scene_actors_begin(scene);
    dgx_ge_scene_actor(scene, &(dgx_ge_item_t){.image = &dot, .x = 20, .y = 10, .layer = 1, .color = 7});
    dgx_ge_scene_render(scene);
    CHECK(frame->pixels_sent == sent);

    /* a clip of its own: the dot cut to its right column */
    dgx_ge_scene_actors_begin(scene);
    dgx_ge_scene_actor(scene, &(dgx_ge_item_t){.image = &dot, .x = 20, .y = 10, .layer = 1, .color = 7, .clip = {21, 0, 5, 16}});
    dgx_ge_scene_render(scene);
    CHECK(at(frame, 20, 9) == 1 && at(frame, 21, 9) == 7);

    /* a piece of the set changed: the front square gone */
    dgx_ge_scene_change(scene, front, &(dgx_ge_item_t){.image = NULL, .layer = 2});
    dgx_ge_scene_actors_begin(scene);
    dgx_ge_scene_render(scene);
    CHECK(at(frame, 6, 4) == 1 && at(frame, 3, 4) == 5 && at(frame, 21, 9) == 1);

    /* by key: in a layer ordered so, a larger key is nearer */
    dgx_ge_scene_layer(scene, 1, 256, DGX_GE_ORDER_KEY);
    dgx_ge_scene_actors_begin(scene);
    dgx_ge_scene_actor(scene, &(dgx_ge_item_t){.image = &dot, .x = 10, .y = 10, .layer = 1, .color = 8, .key = 5});
    dgx_ge_scene_actor(scene, &(dgx_ge_item_t){.image = &dot, .x = 10, .y = 10, .layer = 1, .color = 9, .key = -5});
    dgx_ge_scene_render(scene);
    CHECK(at(frame, 10, 10) == 8);

    /* the window: a layer of factor 256 moves with it, one of 0 stays */
    dgx_ge_scene_layer(scene, 0, 0, DGX_GE_ORDER_ADDED);
    dgx_ge_scene_window(scene, 4, 0);
    dgx_ge_scene_actors_begin(scene);
    dgx_ge_scene_actor(scene, &(dgx_ge_item_t){.image = &dot, .x = 10, .y = 10, .layer = 1, .color = 8});
    dgx_ge_scene_render(scene);
    CHECK(at(frame, 2, 5) == 5);              /* the red square stays */
    CHECK(at(frame, 6, 10) == 8 && at(frame, 10, 10) == 1); /* the dot moved 4 left */

    /* a fill and a card: a strip of 3 over everything, the dot on a card of 4 */
    dgx_ge_scene_window(scene, 0, 0);
    dgx_ge_scene_layer(scene, 0, 256, DGX_GE_ORDER_ADDED);
    dgx_ge_scene_actors_begin(scene);
    dgx_ge_scene_actor(scene, &(dgx_ge_item_t){.flags = DGX_GE_ITEM_FILL, .clip = {0, 14, W, 2}, .layer = 2, .color = 3});
    static const uint8_t ring_px[] = {0x11, 0x10, 0x10, 0x10, 0x11, 0x10}; /* 3x3, the middle empty */
    static const uint8_t ink7[15]  = {7};
    dgx_image_t       ring      = {3, 3, 0, 2, DGX_IMAGE_4BPP, ring_px};
    dgx_ge_scene_actor(scene, &(dgx_ge_item_t){.image = &ring, .map = ink7, .x = 2, .y = 5, .layer = 1, .color = 0, .flags = DGX_GE_ITEM_CARD});
    dgx_ge_scene_render(scene);
    CHECK(at(frame, 0, 14) == 3 && at(frame, 31, 15) == 3 && at(frame, 0, 13) == 1);
    CHECK(at(frame, 2, 3) == 7 && at(frame, 3, 4) == 0 && at(frame, 4, 5) == 7); /* the hole of the ring shows the card, not the red square */

    /* the set given anew quietly: only the place marked is drawn and sent */
    dgx_ge_scene_actors_begin(scene);
    dgx_ge_scene_render(scene);
    uint32_t quiet_sent = frame->pixels_sent;
    dgx_ge_scene_set_begin(scene);
    dgx_ge_scene_add(scene, &(dgx_ge_item_t){.image = &sq, .map = blue, .x = 20, .y = 5, .layer = 0});
    dgx_ge_scene_add(scene, &(dgx_ge_item_t){.image = &sq, .map = red, .x = 26, .y = 5, .layer = 0});
    dgx_ge_scene_set_end(scene);
    dgx_ge_scene_invalidate(scene, 0, dgx_ge_rect(20, 2, 4, 4));
    dgx_ge_scene_actors_begin(scene);
    dgx_ge_scene_render(scene);
    CHECK(frame->pixels_sent - quiet_sent == 16);
    CHECK(at(frame, 20, 2) == 6 && at(frame, 26, 2) != 5); /* the unmarked square waits for its place to be drawn */

    /* a carried picture stands from the carrier as it is drawn, turns round with it, shares its layer, key and clip */
    const dgx_ge_item_t carrier = {.image = &sq, .map = red, .x = 10, .y = 20, .key = 3, .clip = dgx_ge_rect(0, 0, 15, 30), .layer = 2,
                                   .flags = DGX_GE_ITEM_CARD | DGX_GE_BLIT_SILHOUETTE, .color = 4};
    dgx_ge_item_t       held    = dgx_ge_item_child(&carrier, &ring, ink7, 5, -2);
    CHECK(held.x == 15 && held.y == 18 && held.key == 3 && held.layer == 2 && dgx_ge_rect_equal(held.clip, carrier.clip));
    CHECK(held.flags == DGX_GE_BLIT_SILHOUETTE && held.color == 4 && held.image == &ring && held.map == ink7);
    dgx_ge_item_t turned = carrier;
    turned.flags |= DGX_GE_BLIT_FLIP_X;
    held = dgx_ge_item_child(&turned, &ring, ink7, 5, -2);
    CHECK(held.x == 5 && held.y == 18 && (held.flags & DGX_GE_BLIT_FLIP_X));

    /* children next to their carrier by z, never between it and another actor */
    dgx_ge_scene_actors_begin(scene);
    const dgx_ge_item_t first = {.image = &dot, .x = 40, .y = 40, .layer = 1, .color = 1};
    const dgx_ge_item_t body  = {.image = &dot, .x = 50, .y = 40, .layer = 1, .color = 2};
    CHECK(!dgx_ge_scene_actor_child(scene, &body, 1)); /* no carrier yet */
    CHECK(dgx_ge_scene_actor(scene, &first));
    CHECK(dgx_ge_scene_actor(scene, &body));
    dgx_ge_item_t ahead = dgx_ge_item_child(&body, &dot, NULL, 1, 0), behind = dgx_ge_item_child(&body, &dot, NULL, -1, 0), nearer = ahead;
    ahead.color = 3, behind.color = 4, nearer.color = 5;
    CHECK(dgx_ge_scene_actor_child(scene, &ahead, 1) && dgx_ge_scene_actor_child(scene, &behind, -1) && dgx_ge_scene_actor_child(scene, &nearer, 2));
    CHECK(dgx_ge_scene_actor(scene, &(dgx_ge_item_t){.image = &dot, .x = 60, .y = 40, .layer = 1, .color = 6}));
    CHECK(scene->actor_count == 6);
    CHECK(scene->actors[0].color == 1 && scene->actors[1].color == 4 && scene->actors[2].color == 2 && scene->actors[3].color == 3 &&
          scene->actors[4].color == 5 && scene->actors[5].color == 6);

    /* the display and the scene: the frame at 10, 20, the window at 6, 0, a far layer at half the window */
    frame->display_x = 10, frame->display_y = 20;
    dgx_ge_scene_window(scene, 6, 0);
    dgx_ge_scene_layer(scene, 2, 128, DGX_GE_ORDER_ADDED);
    int px, py, dx, dy;
    CHECK(dgx_ge_scene_point_at(scene, 1, 13, 24, &px, &py) && px == 9 && py == 4);
    CHECK(dgx_ge_scene_point_at(scene, 2, 13, 24, &px, &py) && px == 6 && py == 4); /* the far layer has moved by 3 */
    dgx_ge_scene_pixel_at(scene, 1, 9, 4, &dx, &dy);
    CHECK(dx == 13 && dy == 24);
    CHECK(!dgx_ge_scene_point_at(scene, 1, 9, 24, &px, &py) && px == 5);   /* left of the frame */
    CHECK(!dgx_ge_scene_point_at(scene, 1, 10 + W, 24, &px, &py));          /* right of it */
    CHECK(dgx_ge_frame_scale(frame, 3));                                     /* x3: three pixels a point */
    CHECK(dgx_ge_scene_point_at(scene, 1, 17, 28, &px, &py) && px == 8 && py == 2);
    CHECK(dgx_ge_scene_point_at(scene, 1, 10 + 3 * W - 1, 20, &px, &py) && px == 6 + W - 1);
    CHECK(!dgx_ge_scene_point_at(scene, 1, 9, 20, &px, &py) && px == 5);    /* a pixel left of the frame is the point before */
    dgx_ge_scene_pixel_at(scene, 1, 8, 2, &dx, &dy);
    CHECK(dx == 16 && dy == 26);
    CHECK(dgx_ge_frame_scale(frame, 1) && !frame->scaled);

    /*
     * The window moved a little with every layer at 256: the frame scrolls — its pixels move, the display moves its
     * picture (the virtual screen is read back), only the strip that came in is marked and drawn; an actor that did
     * not move with the window is drawn anew where it was and is
     */
    {
        dgx_ge_scene_t *roll = dgx_ge_scene_create(frame, 2, 4, 4);
        CHECK(roll);
        frame->display_x = frame->display_y = 0;
        dgx_ge_frame_set_palette(frame, 2, 4, (const uint8_t[][3]){{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 0}}); /* colors seen on the display */
        CHECK(dgx_ge_scene_add(roll, &(dgx_ge_item_t){.image = &dot, .x = 10, .y = 4, .layer = 1, .color = 2}) >= 0);
        CHECK(dgx_ge_scene_add(roll, &(dgx_ge_item_t){.image = &dot, .x = 2, .y = 8, .layer = 0, .color = 3}) >= 0);
        dgx_ge_scene_actors_begin(roll);
        CHECK(dgx_ge_scene_actor(roll, &(dgx_ge_item_t){.image = &dot, .x = 20, .y = 12, .layer = 1, .color = 4}));
        dgx_ge_scene_render(roll);
        CHECK(at(frame, 10, 4) == 2 && at(frame, 2, 8) == 3 && at(frame, 20, 12) == 4 && dgx_get_pixel(display, 10, 4) == frame->lut[2]);
        dgx_ge_scene_window(roll, 3, 0); /* the window 3 to the right: the picture 3 to the left, a strip of 3 comes in at the right */
        frame->pixels_sent = 0;
        dgx_ge_scene_actors_begin(roll);
        CHECK(dgx_ge_scene_actor(roll, &(dgx_ge_item_t){.image = &dot, .x = 20, .y = 12, .layer = 1, .color = 4})); /* the same place of the scene: it moves with the window */
        dgx_ge_scene_draw(roll);
        CHECK(frame->dirty_count == 1 && frame->dirty[0].x == W - 3 && frame->dirty[0].w == 3 && frame->dirty[0].h == H);
        CHECK(at(frame, 7, 4) == 2 && at(frame, 10, 4) != 2 && at(frame, 17, 12) == 4);           /* the pixels moved in the frame */
        CHECK(dgx_get_pixel(display, 7, 4) == frame->lut[2] && dgx_get_pixel(display, 10, 4) != frame->lut[2]); /* and on the display, before anything was sent */
        dgx_ge_frame_present(frame);
        CHECK(frame->pixels_sent == 3 * H);
        /* the far corner: the dot that came in with the strip is drawn there */
        CHECK(dgx_ge_scene_add(roll, &(dgx_ge_item_t){.image = &dot, .x = W + 1, .y = 2, .layer = 1, .color = 5}) >= 0);
        dgx_ge_scene_window(roll, 4, 1);
        dgx_ge_scene_actors_begin(roll);
        dgx_ge_scene_render(roll);
        CHECK(at(frame, W - 3, 1) == 5 && at(frame, 6, 3) == 2 && dgx_get_pixel(display, 6, 3) == frame->lut[2]);
        CHECK(at(frame, 17, 12) != 4 && at(frame, 16, 11) != 4); /* the actor gone: not given this time */
        /* a layer that moves otherwise: no scroll, all of it anew */
        dgx_ge_scene_layer(roll, 0, 128, DGX_GE_ORDER_ADDED);
        dgx_ge_scene_window(roll, 6, 1);
        dgx_ge_scene_actors_begin(roll);
        dgx_ge_scene_draw(roll);
        CHECK(frame->dirty_count == 1 && frame->dirty[0].w == W && frame->dirty[0].h == H);
        dgx_ge_frame_present(frame);
        dgx_ge_scene_destroy(&roll);
    }

    /*
     * The origin moved by 256: the set, the actors as shown and the window go
     * with it, a layer of half the factor by 128; nothing is drawn anew, and
     * drawing all anew gives the same picture.
     */
    frame->display_x = frame->display_y = 0;
    dgx_ge_scene_reset(scene, 0);
    dgx_ge_scene_window(scene, 300, 0);
    CHECK(dgx_ge_scene_add(scene, &(dgx_ge_item_t){.image = &dot, .x = 305, .y = 4, .layer = 1, .color = 2}) >= 0);
    CHECK(dgx_ge_scene_add(scene, &(dgx_ge_item_t){.image = &dot, .x = 153, .y = 6, .layer = 2, .color = 3}) >= 0); /* the far layer sees 150 */
    CHECK(dgx_ge_scene_add(scene, &(dgx_ge_item_t){.flags = DGX_GE_ITEM_FILL, .clip = dgx_ge_rect(301, 9, 3, 2), .layer = 1, .color = 5}) >= 0);
    dgx_ge_scene_actors_begin(scene);
    CHECK(dgx_ge_scene_actor(scene, &(dgx_ge_item_t){.image = &dot, .x = 310, .y = 8, .layer = 1, .color = 4}));
    dgx_ge_scene_draw(scene);
    static uint8_t before[W * H];
    for (int i = 0; i < W * H; ++i) before[i] = frame->canvas.pixels[i];
    CHECK(!dgx_ge_scene_shift(scene, 101, 0)); /* 101 * 128 / 256 is no whole point */
    CHECK(scene->window_x == 300 && scene->set[0].x == 305);                       /* and nothing moved */
    CHECK(dgx_ge_scene_shift(scene, 256, 0));
    CHECK(scene->window_x == 44 && scene->set[0].x == 305 - 256 && scene->set[1].x == 153 - 128 && scene->set[2].clip.x == 301 - 256);
    frame->dirty_count = 0;
    dgx_ge_scene_actors_begin(scene);
    CHECK(dgx_ge_scene_actor(scene, &(dgx_ge_item_t){.image = &dot, .x = 310 - 256, .y = 8, .layer = 1, .color = 4}));
    dgx_ge_scene_draw(scene);
    CHECK(frame->dirty_count == 0);
    dgx_ge_scene_invalidate(scene, 1, dgx_ge_rect(44, 0, W, H));
    dgx_ge_scene_draw(scene);
    bool same = true;
    for (int i = 0; i < W * H; ++i) same = same && before[i] == frame->canvas.pixels[i];
    CHECK(same && frame->dirty_count > 0);

    /*
     * Actors seen in the open space of their layer: the scene asks with the
     * body (the box of the picture, or the clip given) and the middle of its
     * bottom, and puts the answer as the clip; a child gets its carrier's.
     */
    dgx_ge_scene_window(scene, 0, 0);
    dgx_ge_scene_space(scene, 1, left_of_12, NULL);
    dgx_ge_scene_actors_begin(scene);
    asked = 0;
    CHECK(dgx_ge_scene_actor(scene, &(dgx_ge_item_t){.image = &dot, .x = 10, .y = 8, .layer = 1, .flags = DGX_GE_ITEM_OPEN_SPACE}));
    const dgx_ge_rect_t dot_box = dgx_ge_image_box(&dot, 10, 8, false);
    CHECK(asked == 1 && dgx_ge_rect_equal(asked_body, dot_box) && asked_x == dot_box.x + dot_box.w / 2 && asked_y == dot_box.y + dot_box.h - 1);
    CHECK(scene->actors[0].clip.x == -100 && scene->actors[0].clip.w == 112 && !(scene->actors[0].flags & DGX_GE_ITEM_OPEN_SPACE));
    dgx_ge_item_t carried = dgx_ge_item_child(&(dgx_ge_item_t){.image = &dot, .x = 10, .y = 8, .layer = 1, .flags = DGX_GE_ITEM_OPEN_SPACE}, &dot, NULL, 3, 0);
    CHECK(dgx_ge_scene_actor_child(scene, &carried, -1) && asked == 1); /* not asked again */
    CHECK(scene->actors[0].clip.w == 112 && scene->actor_z[0] == -1 && !(scene->actors[0].flags & DGX_GE_ITEM_OPEN_SPACE));
    CHECK(dgx_ge_scene_actor(scene, &(dgx_ge_item_t){.image = &dot, .x = 30, .y = 8, .layer = 1, .clip = dgx_ge_rect(28, 2, 4, 6), .flags = DGX_GE_ITEM_OPEN_SPACE}));
    CHECK(asked == 2 && asked_body.x == 28 && asked_body.w == 4 && asked_x == 30 && asked_y == 7);
    CHECK(dgx_ge_scene_actor(scene, &(dgx_ge_item_t){.image = &dot, .x = 30, .y = 8, .layer = 1, .clip = dgx_ge_rect(28, 2, 4, 6), .feet_dx = -3,
                                                     .flags = DGX_GE_ITEM_OPEN_SPACE}));
    CHECK(asked == 3 && asked_x == 27 && !asked_inside); /* its feet where it stands, not at the middle of its body */
    /* gone into an opening: the space is told; the flag is spent there, a child of it takes the clip as it is */
    const dgx_ge_item_t in = {.image = &dot, .x = 30, .y = 8, .layer = 1, .flags = DGX_GE_ITEM_OPEN_SPACE | DGX_GE_ITEM_INSIDE};
    CHECK(dgx_ge_scene_actor(scene, &in) && asked == 4 && asked_inside && !(scene->actors[3].flags & (DGX_GE_ITEM_OPEN_SPACE | DGX_GE_ITEM_INSIDE)));
    carried = dgx_ge_item_child(&in, &dot, NULL, 3, 0);
    CHECK((carried.flags & DGX_GE_ITEM_INSIDE) && dgx_ge_scene_actor_child(scene, &carried, 1) && asked == 4);
    CHECK(scene->actors[4].clip.w == 112 && !(scene->actors[4].flags & (DGX_GE_ITEM_OPEN_SPACE | DGX_GE_ITEM_INSIDE)));
    scene->actor_count = 3;
    CHECK(dgx_ge_scene_actor(scene, &(dgx_ge_item_t){.image = &dot, .x = 40, .y = 8, .layer = 2, .flags = DGX_GE_ITEM_OPEN_SPACE}));
    CHECK(asked == 4 && dgx_ge_rect_empty(scene->actors[3].clip)); /* a layer of no space: nothing hides it */
    dgx_ge_scene_space(scene, 1, NULL, NULL);

    /*
     * Repeated: a picture of a point and a gap laid over its clip from its
     * origin, a step its width; begun before the clip, its steps still from
     * the origin (the clip only cuts); drawn anew where it lies.
     */
    static const uint8_t dash_px[] = {0x80};
    dgx_image_t          dash = {2, 1, 0, 0, DGX_IMAGE_1BPP, dash_px};
    dgx_ge_scene_reset(scene, 0);
    dgx_ge_scene_window(scene, 0, 0);
    CHECK(dgx_ge_scene_add(scene, &(dgx_ge_item_t){.image = &dash, .x = 3, .y = 2, .clip = dgx_ge_rect(3, 2, 7, 3), .layer = 1, .flags = DGX_GE_ITEM_REPEAT,
                                                   .color = 7}) >= 0);
    CHECK(dgx_ge_scene_add(scene, &(dgx_ge_item_t){.image = &dash, .x = 1, .y = 8, .clip = dgx_ge_rect(4, 8, 7, 1), .layer = 1, .flags = DGX_GE_ITEM_REPEAT,
                                                   .color = 7}) >= 0);
    dgx_ge_scene_actors_begin(scene);
    dgx_ge_scene_draw(scene);
    bool laid = true;
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 14; ++x) {
            const bool first = y >= 2 && y < 5 && x >= 3 && x < 10 && (x - 3) % 2 == 0, second = y == 8 && x >= 4 && x < 11 && (x - 1) % 2 == 0;
            laid = laid && (at(frame, x, y) == 7) == (first || second);
        }
    CHECK(laid);
    frame->dirty_count = 0;
    dgx_ge_scene_change(scene, 0, &(dgx_ge_item_t){.image = &dash, .x = 3, .y = 2, .clip = dgx_ge_rect(3, 2, 7, 3), .layer = 1, .flags = DGX_GE_ITEM_REPEAT,
                                                   .color = 6});
    CHECK(frame->dirty_count == 1 && frame->dirty[0].x == 3 && frame->dirty[0].w == 7 && frame->dirty[0].h == 3); /* its clip, not its picture */

    /* colors of sets in the frame */
    uint8_t map[3];
    CHECK(dgx_ge_frame_add_colors(frame, (const uint8_t[][3]){{10, 20, 30}, {1, 2, 3}, {10, 20, 30}}, 3, map));
    CHECK(map[0] == 1 && map[1] == 2 && map[2] == 1 && frame->colors_used == 2);
    CHECK(dgx_ge_frame_add_colors(frame, (const uint8_t[][3]){{1, 2, 3}, {7, 7, 7}}, 2, map) && map[0] == 2 && map[1] == 3);
    dgx_ge_frame_forget_colors(frame);
    CHECK(dgx_ge_frame_add_colors(frame, (const uint8_t[][3]){{7, 7, 7}}, 1, map) && map[0] == 1);
    /* no room: no index taken, every color the nearest the frame has (one that is there: its own) */
    {
        uint8_t many[254][3], many_map[254];
        for (int i = 0; i < 254; ++i) many[i][0] = (uint8_t)i, many[i][1] = 0, many[i][2] = 100;
        CHECK(dgx_ge_frame_add_colors(frame, many, 254, many_map) && frame->colors_used == 255 && many_map[200] == 202);
        CHECK(!dgx_ge_frame_add_colors(frame, (const uint8_t[][3]){{7, 7, 9}, {200, 1, 99}, {7, 7, 7}}, 3, map) && frame->colors_used == 255);
        CHECK(map[0] == 1 && map[1] == 202 && map[2] == 1);
        dgx_ge_frame_forget_colors(frame);
        CHECK(dgx_ge_frame_add_colors(frame, (const uint8_t[][3]){{7, 7, 7}}, 1, map) && map[0] == 1);
    }
    /* a tint: the palette on the display mixed with a color, the colors given later too; none: as they are */
    /* a frame shown x3: a point a square of 3x3 pixels of the display at the frame's place, as many pixels sent; upside down the rows mirrored */
    {
        dgx_screen_t   *big   = dgx_vscreen_init(3 * W + 2, 3 * H + 4, 16, DgxScreenRGB);
        dgx_ge_frame_t *large = dgx_ge_frame_create(big, 2, 4, W, H);
        CHECK(big && large && dgx_ge_frame_scale(large, 3) && large->scaled && large->scaled->width == 3 * W);
        dgx_ge_frame_set_palette(large, 0, 2, (const uint8_t[][3]){{0, 0, 0}, {255, 255, 255}});
        large->canvas.pixels[5 * W + 7] = 1;
        dgx_ge_frame_present(large);
        CHECK(large->pixels_sent == 9 * W * H);
        CHECK(dgx_get_pixel(big, 2 + 7 * 3, 4 + 5 * 3) == large->lut[1] && dgx_get_pixel(big, 2 + 7 * 3 + 2, 4 + 5 * 3 + 2) == large->lut[1]);
        CHECK(dgx_get_pixel(big, 2 + 7 * 3 - 1, 4 + 5 * 3) == large->lut[0] && dgx_get_pixel(big, 2 + 7 * 3 + 3, 4 + 5 * 3) == large->lut[0]);
        CHECK(dgx_get_pixel(big, 2 + 7 * 3, 4 + 5 * 3 - 1) == large->lut[0] && dgx_get_pixel(big, 2 + 7 * 3, 4 + 5 * 3 + 3) == large->lut[0]);
        dgx_ge_frame_upside_down(large, true);
        dgx_ge_frame_present(large);
        CHECK(dgx_get_pixel(big, 2 + 7 * 3, 4 + (H - 1 - 5) * 3) == large->lut[1] && dgx_get_pixel(big, 2 + 7 * 3, 4 + 5 * 3) == large->lut[0]);
        dgx_ge_frame_upside_down(large, false);
        dgx_ge_frame_present(large);
        /*
         * A layer in pixels of the display: its actors overlays of the frame, drawn at the display's resolution over
         * the scaled frame (a dot of 2x1 at display 5, 7: those two pixels alone), the frame under them marked when
         * they come, move or go; the scaled point shows again where the overlay left
         */
        dgx_ge_scene_t *over = dgx_ge_scene_create(large, 2, 4, 4);
        dgx_ge_scene_layer_pixels(over, 1, true);
        dgx_ge_frame_set_palette(large, 2, 1, (const uint8_t[][3]){{255, 0, 0}});
        CHECK(dgx_ge_scene_add(over, &(dgx_ge_item_t){.flags = DGX_GE_ITEM_FILL, .clip = dgx_ge_rect(7, 5, 1, 1), .layer = 0, .color = 1}) >= 0); /* the white point, of the scene now */
        dgx_ge_scene_actors_begin(over);
        CHECK(dgx_ge_scene_actor(over, &(dgx_ge_item_t){.image = &dot, .x = 5, .y = 7, .layer = 1, .color = 2}));
        CHECK(over->actor_count == 0 && large->overlay_count == 1);
        dgx_ge_scene_render(over);
        CHECK(dgx_get_pixel(big, 5, 7) == large->lut[2] && dgx_get_pixel(big, 6, 7) == large->lut[2]);
        CHECK(dgx_get_pixel(big, 7, 7) != large->lut[2] && dgx_get_pixel(big, 5, 8) != large->lut[2] && dgx_get_pixel(big, 4, 7) != large->lut[2]);
        CHECK(dgx_get_pixel(big, 2 + 7 * 3, 4 + 5 * 3) == large->lut[1]); /* the scaled frame around it as before */
        large->pixels_sent = 0;
        dgx_ge_scene_actors_begin(over);
        CHECK(dgx_ge_scene_actor(over, &(dgx_ge_item_t){.image = &dot, .x = 2 + 7 * 3, .y = 4 + 5 * 3, .layer = 1, .color = 2}));
        dgx_ge_scene_render(over);
        CHECK(dgx_get_pixel(big, 5, 7) != large->lut[2]);                                                       /* gone from there */
        CHECK(dgx_get_pixel(big, 2 + 7 * 3, 4 + 5 * 3) == large->lut[2] && dgx_get_pixel(big, 2 + 7 * 3 + 2, 4 + 5 * 3) == large->lut[1]); /* over the white point */
        CHECK(large->pixels_sent <= 48 * 9); /* only the points under the two places went again (the dot is 2x2: two rows of points; merged into one rectangle at most) */
        dgx_ge_scene_actors_begin(over);
        dgx_ge_scene_render(over);
        CHECK(dgx_get_pixel(big, 2 + 7 * 3, 4 + 5 * 3) == large->lut[1]); /* no overlay: the frame as it is */
        /* an overlay whose picture changes in place (a text laid anew, narrower): the box it covered is marked, not the box its picture has now */
        static const uint8_t bar_px[] = {0xF0};
        dgx_image_t          bar      = {4, 1, 0, 0, DGX_IMAGE_1BPP, bar_px};
        dgx_ge_scene_actors_begin(over);
        CHECK(dgx_ge_scene_actor(over, &(dgx_ge_item_t){.image = &bar, .x = 5, .y = 7, .layer = 1, .color = 2}));
        dgx_ge_scene_render(over);
        CHECK(dgx_get_pixel(big, 8, 7) == large->lut[2]);
        bar.width = 2;
        dgx_ge_scene_actors_begin(over);
        CHECK(dgx_ge_scene_actor(over, &(dgx_ge_item_t){.image = &bar, .x = 5, .y = 7, .layer = 1, .color = 2}));
        dgx_ge_scene_render(over);
        CHECK(dgx_get_pixel(big, 6, 7) == large->lut[2] && dgx_get_pixel(big, 8, 7) != large->lut[2]); /* the right half gone from the display */
        dgx_ge_scene_actors_begin(over);
        dgx_ge_scene_render(over);
        dgx_ge_scene_destroy(&over);
        dgx_ge_frame_destroy(&large);
        /*
         * Smoothing (Scale2x): two white points at (3,3) and (4,4), a step of a slanted line, at x2 get their inner
         * corners filled (the pixel right of the first point's bottom-right and the one left of the second's top-left
         * are white) and nothing else near; off, the same points are plain blocks
         */
        dgx_screen_t   *two = dgx_vscreen_init(2 * W, 2 * H, 16, DgxScreenRGB);
        dgx_ge_frame_t *half = dgx_ge_frame_create(two, 0, 0, W, H);
        CHECK(two && half && dgx_ge_frame_scale(half, 2));
        dgx_ge_frame_set_palette(half, 0, 2, (const uint8_t[][3]){{0, 0, 0}, {255, 255, 255}});
        half->canvas.pixels[3 * W + 3] = 1, half->canvas.pixels[4 * W + 4] = 1;
        dgx_ge_frame_present(half);
        CHECK(dgx_get_pixel(two, 7, 7) == half->lut[1] && dgx_get_pixel(two, 8, 8) == half->lut[1]);
        CHECK(dgx_get_pixel(two, 8, 7) == half->lut[0] && dgx_get_pixel(two, 7, 8) == half->lut[0]); /* blocks: the corners between them empty */
        CHECK(dgx_ge_frame_smooth(half, true) && half->smooth);
        dgx_ge_frame_present(half);
        CHECK(dgx_get_pixel(two, 7, 7) == half->lut[1] && dgx_get_pixel(two, 8, 8) == half->lut[1]);
        CHECK(dgx_get_pixel(two, 8, 7) == half->lut[1] && dgx_get_pixel(two, 7, 8) == half->lut[1]); /* the step filled in */
        CHECK(dgx_get_pixel(two, 6, 7) == half->lut[1] && dgx_get_pixel(two, 9, 8) == half->lut[1]); /* the points themselves still there */
        CHECK(dgx_get_pixel(two, 5, 7) == half->lut[0] && dgx_get_pixel(two, 6, 8) == half->lut[0] && dgx_get_pixel(two, 9, 7) == half->lut[0]); /* and no more */
        /* at x4 the same, twice: the two points joined along the slant, the far corners empty */
        dgx_screen_t   *four = dgx_vscreen_init(4 * W, 4 * H, 16, DgxScreenRGB);
        dgx_ge_frame_t *quart = dgx_ge_frame_create(four, 0, 0, W, H);
        CHECK(four && quart && dgx_ge_frame_scale(quart, 4) && dgx_ge_frame_smooth(quart, true) && quart->smooth == DGX_GE_SMOOTH_ON);
        dgx_ge_frame_set_palette(quart, 0, 2, (const uint8_t[][3]){{0, 0, 0}, {255, 255, 255}});
        quart->canvas.pixels[3 * W + 3] = 1, quart->canvas.pixels[4 * W + 4] = 1;
        dgx_ge_frame_present(quart);
        CHECK(dgx_get_pixel(four, 15, 15) == quart->lut[1] && dgx_get_pixel(four, 16, 16) == quart->lut[1] && dgx_get_pixel(four, 16, 15) == quart->lut[1]);
        CHECK(dgx_get_pixel(four, 13, 12) == quart->lut[1] && dgx_get_pixel(four, 12, 13) == quart->lut[1] && dgx_get_pixel(four, 18, 19) == quart->lut[1]);
        CHECK(dgx_get_pixel(four, 12, 12) == quart->lut[0] && dgx_get_pixel(four, 19, 19) == quart->lut[0]); /* the outer corners rounded off by the second pass */
        CHECK(dgx_get_pixel(four, 19, 12) == quart->lut[0] && dgx_get_pixel(four, 12, 19) == quart->lut[0]); /* the far corners of the blocks: empty */
        CHECK(dgx_get_pixel(four, 11, 15) == quart->lut[0] && dgx_get_pixel(four, 20, 16) == quart->lut[0]);
        dgx_ge_frame_destroy(&half), dgx_ge_frame_destroy(&quart);
        dgx_screen_destroy(&two), dgx_screen_destroy(&four);
        CHECK(!dgx_ge_frame_smooth(frame, 3) && dgx_ge_frame_smooth(frame, DGX_GE_SMOOTH_NONE)); /* no such way */
        /*
         * A part under an overlay goes through the band, one without straight to the display: at 2, 3 and 4, as
         * blocks, smoothed and SFX, the display the same outside the overlay either way. Points of three colors by
         * dice, over the edges too (the neighbours beyond them), slanted runs and lone points among them.
         */
        uint8_t  dots[W * H];
        uint32_t dice = 12345;
        for (int i = 0; i < W * H; ++i) dice = dice * 1103515245u + 12345u, dots[i] = (uint8_t)((dice >> 16) % 3);
        for (int n = 2; n <= 4; ++n)
            for (int how = DGX_GE_SMOOTH_NONE; how <= DGX_GE_SMOOTH_SFX; ++how) {
                dgx_screen_t   *straight = dgx_vscreen_init(n * W, n * H, 16, DgxScreenRGB), *banded = dgx_vscreen_init(n * W, n * H, 16, DgxScreenRGB);
                dgx_ge_frame_t *fs = dgx_ge_frame_create(straight, 0, 0, W, H), *fb = dgx_ge_frame_create(banded, 0, 0, W, H);
                CHECK(fs && fb && dgx_ge_frame_scale(fs, n) && dgx_ge_frame_scale(fb, n) && dgx_ge_frame_smooth(fs, how) && dgx_ge_frame_smooth(fb, how));
                for (dgx_ge_frame_t *f = fs; f; f = f == fs ? fb : NULL) {
                    dgx_ge_frame_set_palette(f, 0, 3, (const uint8_t[][3]){{0, 0, 0}, {255, 255, 255}, {255, 0, 0}});
                    memcpy(f->canvas.pixels, dots, sizeof dots);
                }
                const dgx_ge_rect_t over = dgx_ge_rect(n * 5, n * 3, 3, 2); /* a fill of 3 x 2 pixels of the display */
                dgx_ge_frame_overlays_begin(fb);
                CHECK(dgx_ge_frame_overlay(fb, &(dgx_ge_item_t){.flags = DGX_GE_ITEM_FILL, .clip = over, .color = 2}));
                dgx_ge_frame_overlays_end(fb);
                dgx_ge_frame_present(fs), dgx_ge_frame_present(fb);
                int differ = 0;
                for (int y = 0; y < n * H; ++y)
                    for (int x = 0; x < n * W; ++x)
                        if (!(x >= over.x && x < over.x + over.w && y >= over.y && y < over.y + over.h) &&
                            dgx_get_pixel(straight, x, y) != dgx_get_pixel(banded, x, y))
                            ++differ;
                CHECK(differ == 0 && dgx_get_pixel(banded, n * 5, n * 3) == fb->lut[2]);
                if (differ) printf("x%d, smoothing %d: %d pixels differ\n", n, how, differ);
                dgx_ge_frame_destroy(&fs), dgx_ge_frame_destroy(&fb);
                dgx_screen_destroy(&straight), dgx_screen_destroy(&banded);
            }
        dgx_screen_destroy(&big);
    }

    const uint16_t gray = frame->lut[1];
    frame->dirty_count  = 0;
    dgx_ge_frame_tint(frame, 0x000000, 255);
    CHECK(frame->lut[1] == 0 && frame->dirty_count == 1 && frame->rgb[1][0] == 7); /* shown black; the frame keeps its color */
    CHECK(dgx_ge_frame_add_colors(frame, (const uint8_t[][3]){{200, 100, 50}}, 1, map) && frame->lut[map[0]] == 0);
    dgx_ge_frame_tint(frame, 0, 0);
    CHECK(frame->lut[1] == gray && frame->lut[map[0]] != 0);

    /* upside down: a row of the frame goes to the mirrored row of the display; with no display's own way, row by row */
    memset(frame->canvas.pixels, 0, (size_t)W * H);
    frame->canvas.pixels[0 * W + 3] = 1; /* the top row */
    dgx_ge_frame_upside_down(frame, true);
    CHECK(frame->upside && frame->dirty_count == 1); /* all of it anew */
    dgx_ge_frame_present(frame);
    CHECK(display->get_pixel(display, 3, H - 1) == frame->lut[1] && display->get_pixel(display, 3, 0) == frame->lut[0]);
    dgx_ge_frame_upside_down(frame, false);
    dgx_ge_frame_present(frame);
    CHECK(display->get_pixel(display, 3, 0) == frame->lut[1] && display->get_pixel(display, 3, H - 1) == frame->lut[0]);

    /* a wipe: the new picture goes left to right, what is right of the wipe waits, marked; over, all of it goes */
    memset(frame->canvas.pixels, 0, (size_t)W * H);
    dgx_ge_frame_invalidate_all(frame);
    dgx_ge_frame_present(frame);
    memset(frame->canvas.pixels, 1, (size_t)W * H);
    dgx_ge_frame_invalidate_all(frame);
    dgx_ge_frame_wipe(frame, 10);
    dgx_ge_frame_present(frame);
    CHECK(display->get_pixel(display, 9, 5) == frame->lut[1] && display->get_pixel(display, 10, 5) == frame->lut[0] && frame->dirty_count == 1);
    dgx_ge_frame_wipe(frame, 20);
    dgx_ge_frame_present(frame); /* nothing new changed: the wipe alone goes on */
    CHECK(display->get_pixel(display, 19, 5) == frame->lut[1] && display->get_pixel(display, 20, 5) == frame->lut[0]);
    dgx_ge_frame_wipe(frame, -1);
    dgx_ge_frame_present(frame);
    CHECK(display->get_pixel(display, W - 1, H - 1) == frame->lut[1] && frame->dirty_count == 0 && !frame->wiping);

    /* a game that keeps one picture an actor and writes another into it each frame: the scene draws by what it showed */
    {
        dgx_ge_scene_t *keep = dgx_ge_scene_create(frame, 2, 0, 2);
        CHECK(keep);
        dgx_ge_scene_reset(keep, 1);
        dgx_image_t       mine  = sq;                                      /* the game's own, given by pointer */
        const dgx_image_t small = {2, 2, 0, 1, DGX_IMAGE_4BPP, sq_px};    /* the same pixels, 2 x 2 */
        dgx_ge_scene_actors_begin(keep);
        dgx_ge_scene_actor(keep, &(dgx_ge_item_t){.image = &mine, .map = red, .x = 24, .y = 12, .layer = 1});
        dgx_ge_scene_render(keep);
        CHECK(at(frame, 24, 9) == 5 && at(frame, 27, 12) == 5);
        /* the square becomes the small one in the same struct, the same place: what it covered and no more is paper */
        mine = small;
        dgx_ge_scene_actors_begin(keep);
        dgx_ge_scene_actor(keep, &(dgx_ge_item_t){.image = &mine, .map = red, .x = 24, .y = 12, .layer = 1});
        dgx_ge_scene_render(keep);
        CHECK(at(frame, 27, 9) == 1 && at(frame, 24, 9) == 1 && at(frame, 27, 12) == 1);
        CHECK(at(frame, 24, 12) == 5 && at(frame, 25, 11) == 5);
        /* and back: a change of the picture alone, at the same place, is drawn */
        mine = sq;
        dgx_ge_scene_actors_begin(keep);
        dgx_ge_scene_actor(keep, &(dgx_ge_item_t){.image = &mine, .map = red, .x = 24, .y = 12, .layer = 1});
        dgx_ge_scene_render(keep);
        CHECK(at(frame, 27, 9) == 5);
        /* the same again: nothing sent */
        const uint32_t before = frame->pixels_sent;
        dgx_ge_scene_actors_begin(keep);
        dgx_ge_scene_actor(keep, &(dgx_ge_item_t){.image = &mine, .map = red, .x = 24, .y = 12, .layer = 1});
        dgx_ge_scene_render(keep);
        CHECK(frame->pixels_sent == before);
        dgx_ge_scene_destroy(&keep);
    }
    dgx_ge_scene_destroy(&scene);
    dgx_ge_frame_destroy(&frame);
    dgx_screen_destroy(&display);
    /*
     * A picture larger (an item's scale: a picture in display pixels at the frame's scale): each pixel a square of the
     * scale, its origin where it is put, mirrored round the origin, cut by the clip, 0 transparent; 1 bit: the color, 4
     * and 8 bits: the map or the value, a silhouette: the color; repeated a step the larger size; the box as large; at
     * scale 0 and 1 drawn just as DGX draws it
     */
    {
        dgx_screen_t *s8 = dgx_vscreen_init(16, 12, 8, DgxScreenRGB);
        uint8_t      *px = ((dgx_vscreen_t *)s8)->v_array;
#define PX(x, y) px[(y) * 16 + (x)]
        static const uint8_t one_bits[] = {0xA0, 0x60}; /* 3 x 2: 101 / 011 */
        const dgx_image_t    one = {3, 2, 0, 1, 1, one_bits};
        const dgx_ge_rect_t  all = dgx_ge_rect(0, 0, 16, 12);
        memset(px, 9, 16 * 12);
        dgx_ge_item_draw(s8, &(dgx_ge_item_t){.image = &one, .color = 7, .scale = 2}, 4, 4, all, all); /* the origin (0, 1) at 4, 4: from 4, 2 */
        CHECK(PX(4, 2) == 7 && PX(5, 3) == 7 && PX(6, 2) == 9 && PX(7, 3) == 9 && PX(8, 2) == 7 && PX(9, 3) == 7);
        CHECK(PX(4, 4) == 9 && PX(5, 5) == 9 && PX(6, 4) == 7 && PX(9, 5) == 7 && PX(10, 2) == 9 && PX(4, 6) == 9 && PX(3, 2) == 9 && PX(4, 1) == 9);
        CHECK(dgx_ge_rect_equal(dgx_ge_image_box_scaled(&one, 4, 4, false, 2), dgx_ge_rect(4, 2, 6, 4)));
        memset(px, 9, 16 * 12);
        dgx_ge_item_draw(s8, &(dgx_ge_item_t){.image = &one, .color = 7, .scale = 2, .flags = DGX_GE_BLIT_FLIP_X}, 4, 4, all, all); /* mirrored: 101 / 110 */
        CHECK(dgx_ge_rect_equal(dgx_ge_image_box_scaled(&one, 4, 4, true, 2), dgx_ge_rect(0, 2, 6, 4)));
        CHECK(PX(0, 2) == 7 && PX(2, 2) == 9 && PX(4, 2) == 7 && PX(5, 3) == 7 && PX(0, 4) == 7 && PX(2, 5) == 7 && PX(4, 4) == 9 && PX(6, 2) == 9);
        memset(px, 9, 16 * 12);
        dgx_ge_item_draw(s8, &(dgx_ge_item_t){.image = &one, .color = 7, .scale = 2}, 4, 4, all, dgx_ge_rect(5, 3, 2, 2)); /* cut */
        CHECK(PX(4, 2) == 9 && PX(5, 3) == 7 && PX(6, 3) == 9 && PX(6, 4) == 7 && PX(5, 4) == 9 && PX(8, 2) == 9 && PX(7, 4) == 9);
        static const uint8_t four_bits[] = {0x30, 0x0F}; /* 2 x 2 of 4 bits: 3 0 / 0 15 */
        const dgx_image_t    four = {2, 2, 0, 0, 4, four_bits};
        static uint8_t       map[15];
        map[2] = 42, map[14] = 43;
        memset(px, 9, 16 * 12);
        dgx_ge_item_draw(s8, &(dgx_ge_item_t){.image = &four, .map = map, .scale = 3}, 0, 0, all, all);
        CHECK(PX(0, 0) == 42 && PX(2, 2) == 42 && PX(3, 0) == 9 && PX(0, 3) == 9 && PX(3, 3) == 43 && PX(5, 5) == 43 && PX(6, 6) == 9);
        dgx_ge_item_draw(s8, &(dgx_ge_item_t){.image = &four, .scale = 3, .flags = DGX_GE_BLIT_SILHOUETTE, .color = 5}, 8, 0, all, all);
        CHECK(PX(8, 0) == 5 && PX(11, 3) == 5 && PX(11, 0) == 9);
        static const uint8_t eight_bits[] = {200, 0};    /* 2 x 1 of 8 bits */
        const dgx_image_t    eight = {2, 1, 0, 0, 8, eight_bits};
        dgx_ge_item_draw(s8, &(dgx_ge_item_t){.image = &eight, .scale = 2}, 0, 8, all, all);
        CHECK(PX(0, 8) == 200 && PX(1, 9) == 200 && PX(2, 8) == 9);
        static const uint8_t dot_bits[] = {0x80};       /* 2 x 1: 10, repeated a step of 4 (twice its width) along a clip */
        const dgx_image_t    dot = {2, 1, 0, 0, 1, dot_bits};
        memset(px, 9, 16 * 12);
        dgx_ge_item_draw(s8, &(dgx_ge_item_t){.image = &dot, .color = 1, .scale = 2, .flags = DGX_GE_ITEM_REPEAT, .clip = dgx_ge_rect(0, 10, 11, 2)}, 0, 10,
                         dgx_ge_rect(0, 10, 11, 2), dgx_ge_rect(0, 10, 11, 2));
        CHECK(PX(0, 10) == 1 && PX(1, 11) == 1 && PX(2, 10) == 9 && PX(4, 10) == 1 && PX(8, 11) == 1 && PX(9, 10) == 1 && PX(10, 10) == 9 && PX(11, 10) == 9);
        /* scale 0 and 1: as DGX draws it */
        static uint8_t by_dgx[16 * 12];
        memset(px, 9, 16 * 12);
        dgx_image_blit(s8, 5, 5, &one, &(dgx_image_blit_t){.color = 7, .orientation = DgxOutputMirrorX});
        memcpy(by_dgx, px, sizeof by_dgx);
        for (int k = 0; k < 2; ++k) {
            memset(px, 9, 16 * 12);
            dgx_ge_item_draw(s8, &(dgx_ge_item_t){.image = &one, .color = 7, .scale = (uint8_t)k, .flags = DGX_GE_BLIT_FLIP_X}, 5, 5, all, all);
            CHECK(!memcmp(px, by_dgx, sizeof by_dgx));
        }
#undef PX
        dgx_screen_destroy(&s8);
    }
#if defined(__has_include)
#if __has_include("dgx_overlay.h")
    /*
     * The display's overlay layer (DGX 0.6.6): a still game, nothing of the frame changed, yet a layer laid shows — mixed
     * where it lies (black at half over white), nowhere else, the frame under its change sent again at x4 — and taken
     * away, the frame shows there again
     */
    {
        dgx_screen_t   *shown = dgx_vscreen_init(40, 24, 16, DgxScreenRGB);
        dgx_ge_frame_t *f     = dgx_ge_frame_create(shown, 0, 0, 10, 6);
        dgx_screen_t   *layer = dgx_vscreen_init(6, 6, 8, DgxScreenRGB);
        static const uint16_t black[256];
        CHECK(shown && f && layer && dgx_ge_frame_scale(f, 4));
        dgx_ge_frame_set_palette(f, 0, 1, (const uint8_t[][3]){{255, 255, 255}});
        dgx_ge_frame_present(f);
        const uint32_t white = f->lut[0];
        CHECK(dgx_get_pixel(shown, 9, 9) == white && !f->dirty_count);
        dgx_fill_rectangle(layer, 0, 0, 6, 6, 1);
        CHECK(dgx_overlay_set(shown, layer, 9, 9, black, 128));
        dgx_ge_frame_present(f);
        const uint32_t mixed = dgx_get_pixel(shown, 9, 9);
        CHECK(mixed != white && mixed != 0 && dgx_get_pixel(shown, 14, 14) == mixed);
        CHECK(dgx_get_pixel(shown, 8, 9) == white && dgx_get_pixel(shown, 15, 15) == white && dgx_get_pixel(shown, 30, 20) == white);
        dgx_overlay_clear(shown);
        dgx_ge_frame_present(f);
        CHECK(dgx_get_pixel(shown, 9, 9) == white && dgx_get_pixel(shown, 14, 14) == white);
        dgx_screen_destroy(&layer);
        dgx_ge_frame_destroy(&f);
        dgx_screen_destroy(&shown);
    }
#else
    printf("test_scene.c: this DGX has no overlay layer (dgx_overlay.h, 0.6.6): not checked\n");
#endif
#endif
    CHECK_DONE();
}
