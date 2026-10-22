/*
 * The frame's marks against a full redraw: random series of changes of the set, actors, overlays and moves of the
 * window, drawn by the marks into one display and drawn whole every time into another; the two displays, and the two
 * frames, the same after every frame of the series. At every scale, blocks, smoothed and SFX.
 */
#include <stdio.h>
#include <string.h>

#include "check.h"
#include "dgx_draw.h"
#include "dgx_ge_scene.h"
#include "drivers/vscreen.h"

#define W      24
#define H      16
#define WORLD_W 64
#define WORLD_H 40
#define SERIES 24
#define STEPS  30
#define CHANGES 6 /* pieces of the set that change */

/* what a series may do: the kinds of change, alone and together, to tell which one leaves old pixels */
/* EARLY: now and then the window moved before the changes of its frame, else after; UPSIDE: now and then turned over */
enum { CHANGE = 1, SCROLL = 2, OVERLAY = 4, EARLY = 8, UPSIDE = 16 };

static uint32_t dice;
static int roll(int n)
{
    dice = dice * 1103515245u + 12345u;
    return (int)((dice >> 16) % (uint32_t)n);
}

static uint8_t       noise_px[WORLD_W / 2 * WORLD_H];
static const uint8_t map3[15] = {1, 2, 3};
static const dgx_image_t noise = {WORLD_W, WORLD_H, 0, 0, DGX_IMAGE_4BPP, noise_px};
static const uint8_t     dot_px[] = {0xC0, 0x40}; /* a step: a point, and one below right of it */
static const dgx_image_t dot      = {2, 2, 0, 0, DGX_IMAGE_1BPP, dot_px};

static dgx_ge_item_t fill(int x, int y, int w, int h, int layer, int color)
{
    dgx_ge_item_t item;
    memset(&item, 0, sizeof item);
    item.flags = DGX_GE_ITEM_FILL, item.clip = dgx_ge_rect(x, y, w, h), item.layer = (uint8_t)layer, item.color = (uint8_t)color;
    return item;
}

static dgx_ge_item_t a_change(void)
{
    if (roll(2)) return fill(roll(WORLD_W), roll(WORLD_H), 1 + roll(5), 1 + roll(5), 0, 1 + roll(5));
    dgx_ge_item_t item;
    memset(&item, 0, sizeof item);
    item.image = &dot, item.x = (int16_t)roll(WORLD_W), item.y = (int16_t)roll(WORLD_H), item.color = (uint8_t)(1 + roll(5));
    return item;
}

typedef struct {
    dgx_screen_t   *display;
    dgx_ge_frame_t *frame;
    dgx_ge_scene_t *scene;
} side_t;

static bool side_make(side_t *s, int n, int how)
{
    /* the frame a little in from the corner of the display: its edges are not the display's */
    s->display = dgx_vscreen_init(n * W + 3, n * H + 2, 16, DgxScreenRGB);
    s->frame   = s->display ? dgx_ge_frame_create(s->display, 2, 1, W, H) : NULL;
    s->scene   = s->frame ? dgx_ge_scene_create(s->frame, 3, 1 + CHANGES, 4) : NULL;
    if (!s->scene || !dgx_ge_frame_scale(s->frame, n) || !dgx_ge_frame_smooth(s->frame, how)) return false;
    dgx_ge_frame_set_palette(s->frame, 0, 6, (const uint8_t[][3]){{0, 0, 0}, {255, 255, 255}, {255, 0, 0}, {0, 0, 255}, {0, 255, 0}, {255, 255, 0}});
    dgx_ge_scene_layer_pixels(s->scene, 2, true);
    dgx_ge_scene_add(s->scene, &(dgx_ge_item_t){.image = &noise, .map = map3});
    for (int i = 0; i < CHANGES; ++i) dgx_ge_scene_add(s->scene, &(dgx_ge_item_t){.layer = 0});
    return true;
}

static void side_free(side_t *s)
{
    dgx_ge_scene_destroy(&s->scene);
    dgx_ge_frame_destroy(&s->frame);
    dgx_screen_destroy(&s->display);
}

/* pixels of the two displays that differ, and points of the two frames */
static int differ(const side_t *a, const side_t *b)
{
    int d = 0;
    for (int y = 0; y < a->display->height; ++y)
        for (int x = 0; x < a->display->width; ++x) d += dgx_get_pixel(a->display, x, y) != dgx_get_pixel(b->display, x, y);
    for (int i = 0; i < W * H; ++i) d += a->frame->canvas.pixels[i] != b->frame->canvas.pixels[i];
    return d;
}

/* One series: the same frames on both sides, the second drawn whole every time. The number of frames that differ. */
static int series(int n, int how, int kinds, uint32_t seed, uint32_t *sent, uint32_t *sent_whole)
{
    side_t marks, whole;
    CHECK(side_make(&marks, n, how) && side_make(&whole, n, how));
    int wrong = 0, wx = 0, wy = 0;
    dice = seed;
    dgx_ge_item_t actors[4], overlays[2];
    int           actor_count = 0, overlay_count = 0;
    for (int step = 0; step < STEPS; ++step) {
        /* what this frame does, by dice, the same on both sides */
        int nx = wx, ny = wy;
        if ((kinds & SCROLL) && roll(3)) {
            const int far = roll(8) ? 3 : W; /* now and then too far to move: all drawn again */
            nx += roll(2 * far + 1) - far, ny += roll(2 * far + 1) - far;
            nx = nx < 0 ? 0 : nx > WORLD_W - W ? WORLD_W - W : nx;
            ny = ny < 0 ? 0 : ny > WORLD_H - H ? WORLD_H - H : ny;
        }
        const bool    early = (kinds & EARLY) && roll(2), turn = (kinds & UPSIDE) && roll(10) == 0;
        dgx_ge_item_t changed[2];
        int           handles[2], change_count = (kinds & CHANGE) ? roll(3) : 0;
        for (int i = 0; i < change_count; ++i) handles[i] = 1 + roll(CHANGES), changed[i] = a_change();
        if (step == 0 || roll(3) == 0) { /* the actors move now and then, else they stand */
            actor_count = roll(4);
            for (int i = 0; i < actor_count; ++i) actors[i] = fill(nx + roll(W) - 2, ny + roll(H) - 2, 1 + roll(4), 1 + roll(4), 1, 1 + roll(5));
        }
        if ((kinds & OVERLAY) && (step == 0 || roll(4) == 0)) { /* the overlays mostly stand still */
            overlay_count = roll(3);
            for (int i = 0; i < overlay_count; ++i) overlays[i] = fill(roll(n * W), roll(n * H), 1 + roll(3 * n), 1 + roll(3 * n), 2, 1 + roll(5));
        }
        for (side_t *s = &marks; s; s = s == &marks ? &whole : NULL) {
            if (turn) dgx_ge_frame_upside_down(s->frame, !s->frame->upside);
            if (early) dgx_ge_scene_window(s->scene, nx, ny);
            for (int i = 0; i < change_count; ++i) dgx_ge_scene_change(s->scene, handles[i], &changed[i]);
            if (!early) dgx_ge_scene_window(s->scene, nx, ny);
            dgx_ge_scene_actors_begin(s->scene);
            for (int i = 0; i < actor_count; ++i) dgx_ge_scene_actor(s->scene, &actors[i]);
            for (int i = 0; i < overlay_count; ++i) dgx_ge_scene_actor(s->scene, &overlays[i]);
            if (s == &whole) dgx_ge_frame_invalidate_all(s->frame);
            dgx_ge_scene_render(s->scene);
        }
        wx = nx, wy = ny;
        wrong += differ(&marks, &whole) != 0;
    }
    *sent += marks.frame->pixels_sent, *sent_whole += whole.frame->pixels_sent;
    side_free(&marks), side_free(&whole);
    return wrong;
}

/*
 * Upside down with no way of the display's own (rows one by one): the picture the upright one mirrored, to the pixel,
 * smoothed too (the rows a point is made into the other way round as well), straight and through the band (a fill
 * over it, where it is mirrored on the upright display).
 */
static void upside_down(int n, int how, bool over)
{
    side_t up, down;
    CHECK(side_make(&up, n, how) && side_make(&down, n, how));
    for (int i = 0; i < W * H; ++i) up.frame->canvas.pixels[i] = down.frame->canvas.pixels[i] = (uint8_t)(roll(3) ? roll(3) : 1);
    dgx_ge_frame_upside_down(down.frame, true);
    if (over) {
        /* on the frame's part of the display (it begins at 2, 1), and where that is mirrored */
        const dgx_ge_rect_t box = dgx_ge_rect(n * 3 + 1, n * 2 + 1, n * 4, n + 1);
        dgx_ge_item_t       a = fill(box.x, box.y, box.w, box.h, 2, 4), b = fill(box.x, 2 * 1 + n * H - box.y - box.h, box.w, box.h, 2, 4);
        dgx_ge_frame_overlays_begin(up.frame), dgx_ge_frame_overlays_begin(down.frame);
        CHECK(dgx_ge_frame_overlay(up.frame, &a) && dgx_ge_frame_overlay(down.frame, &b));
        dgx_ge_frame_overlays_end(up.frame), dgx_ge_frame_overlays_end(down.frame);
    }
    dgx_ge_frame_present(up.frame), dgx_ge_frame_present(down.frame);
    int differ = 0;
    for (int y = 0; y < n * H; ++y)
        for (int x = 0; x < n * W; ++x) differ += dgx_get_pixel(up.display, 2 + x, 1 + y) != dgx_get_pixel(down.display, 2 + x, 1 + n * H - 1 - y);
    CHECK(differ == 0);
    if (differ) printf("upside down, x%d, smoothing %d%s: %d pixels not mirrored\n", n, how, over ? ", under an overlay" : "", differ);
    side_free(&up), side_free(&down);
}

int main(void)
{
    /* the world behind: points of three colors, runs of one color now and then, for the smoothing to find steps in */
    dice = 777;
    for (int i = 0; i < (int)sizeof noise_px; ++i) {
        const int a = roll(4) ? 1 + roll(3) : 1, b = roll(2) ? a : 1 + roll(3);
        noise_px[i] = (uint8_t)(a << 4 | b);
    }
    static const struct {
        int         kinds;
        const char *name;
    } plans[] = {
        {CHANGE, "changes"},
        {CHANGE | SCROLL, "changes and scrolls"},
        {SCROLL | OVERLAY, "scrolls under overlays"},
        {CHANGE | SCROLL | EARLY, "changes after the window moved"},
        {CHANGE | SCROLL | OVERLAY | EARLY | UPSIDE, "all of it, turned over now and then"},
    };
    for (int p = 0; p < (int)(sizeof plans / sizeof plans[0]); ++p)
        for (int n = 1; n <= 4; ++n)
            for (int how = DGX_GE_SMOOTH_NONE; how <= (n == 1 ? DGX_GE_SMOOTH_NONE : DGX_GE_SMOOTH_SFX); ++how) {
                int      wrong_series = 0;
                uint32_t sent = 0, sent_whole = 0;
                for (int k = 0; k < SERIES; ++k) wrong_series += series(n, how, plans[p].kinds, 1000u * (uint32_t)p + (uint32_t)k, &sent, &sent_whole) != 0;
                CHECK(wrong_series == 0);
                if (wrong_series) printf("%s, x%d, smoothing %d: %d of %d series differ from a full redraw\n", plans[p].name, n, how, wrong_series, SERIES);
                /* the marks send less than all of it, smoothed too */
                CHECK(sent < sent_whole);
            }
    dice = 4242;
    for (int n = 1; n <= 4; ++n)
        for (int how = DGX_GE_SMOOTH_NONE; how <= DGX_GE_SMOOTH_SFX; ++how) upside_down(n, how, false), upside_down(n, how, true);
    CHECK_DONE();
}
