#include <stdlib.h>
#include <string.h>

#include "dgx_ge_scene.h"

/*
 * The order of drawing as one number a picture: its layer, its key, set or
 * actor, and its number, from the high bits down. Numbers differ, so the
 * order is the same however it is sorted.
 */
#define ORDER_INDEX_BITS 12
#define ORDER_ACTOR      (1u << ORDER_INDEX_BITS)
#define ORDER_INDEX_MASK (ORDER_ACTOR - 1u)

dgx_ge_scene_t *dgx_ge_scene_create(dgx_ge_frame_t *frame, int layers, int set_capacity, int actor_capacity)
{
    if (layers < 1 || layers > DGX_GE_SCENE_MAX_LAYERS || set_capacity < 0 || actor_capacity < 0) return NULL;
    if (set_capacity + actor_capacity > (int)ORDER_INDEX_MASK) return NULL;
    dgx_ge_scene_t *scene = calloc(1, sizeof(*scene));
    if (!scene) return NULL;
    scene->frame          = frame;
    scene->layer_count    = (uint8_t)layers;
    scene->set_capacity   = (uint16_t)set_capacity;
    scene->actor_capacity = (uint16_t)actor_capacity;
    for (int i = 0; i < layers; ++i) scene->layers[i] = (dgx_ge_layer_t){256, DGX_GE_ORDER_ADDED, NULL, NULL, false};
    scene->set    = calloc((size_t)set_capacity + 1, sizeof(dgx_ge_item_t));
    scene->actors   = calloc((size_t)actor_capacity + 1, sizeof(dgx_ge_item_t));
    scene->actor_z  = calloc((size_t)actor_capacity + 1, 1);
    scene->shown    = calloc((size_t)actor_capacity + 1, sizeof(dgx_ge_item_t));
    scene->shown_images = calloc((size_t)actor_capacity + 1, sizeof(dgx_image_t));
    scene->order    = calloc((size_t)set_capacity + actor_capacity + 1, sizeof(uint32_t));
    if (!scene->set || !scene->actors || !scene->actor_z || !scene->shown || !scene->shown_images || !scene->order) {
        dgx_ge_scene_destroy(&scene);
        return NULL;
    }
    dgx_ge_scene_reset(scene, 0);
    return scene;
}

void dgx_ge_scene_destroy(dgx_ge_scene_t **scene)
{
    if (!*scene) return;
    free((*scene)->set);
    free((*scene)->actors);
    free((*scene)->actor_z);
    free((*scene)->shown);
    free((*scene)->shown_images);
    free((*scene)->order);
    free(*scene);
    *scene = NULL;
}

void dgx_ge_scene_reset(dgx_ge_scene_t *scene, uint8_t paper)
{
    scene->paper        = paper;
    scene->set_count    = 0;
    scene->actor_count  = 0;
    scene->group_start  = 0;
    scene->shown_count  = 0;
    scene->window_shown = false;
    scene->quiet        = false;
}

void dgx_ge_scene_window(dgx_ge_scene_t *scene, int x, int y)
{
    scene->window_x = (int16_t)x;
    scene->window_y = (int16_t)y;
}


/* an item is kept for every piece of the set and every actor: it grows by nothing that is not paid for (feet_dx sits in its padding) */
_Static_assert(sizeof(dgx_ge_item_t) == (sizeof(void *) == 4 ? 28 : 40), "dgx_ge_item_t has grown");

static void shift_item(dgx_ge_item_t *item, int dx, int dy)
{
    item->x = (int16_t)(item->x - dx);
    item->y = (int16_t)(item->y - dy);
    if (!dgx_ge_rect_empty(item->clip)) item->clip.x = (int16_t)(item->clip.x - dx), item->clip.y = (int16_t)(item->clip.y - dy);
}

bool dgx_ge_scene_shift(dgx_ge_scene_t *scene, int dx, int dy)
{
    for (int l = 0; l < scene->layer_count; ++l)
        if (dx * scene->layers[l].factor % 256 || dy * scene->layers[l].factor % 256) return false;
    /* an item moves with its layer: by the window as that layer sees it */
    for (int i = 0; i < scene->set_count; ++i) {
        const int f = scene->set[i].layer < scene->layer_count ? scene->layers[scene->set[i].layer].factor : 256;
        shift_item(&scene->set[i], dx * f / 256, dy * f / 256);
    }
    for (int i = 0; i < scene->actor_count; ++i) {
        const int f = scene->actors[i].layer < scene->layer_count ? scene->layers[scene->actors[i].layer].factor : 256;
        shift_item(&scene->actors[i], dx * f / 256, dy * f / 256);
    }
    for (int i = 0; i < scene->shown_count; ++i) {
        const int f = scene->shown[i].layer < scene->layer_count ? scene->layers[scene->shown[i].layer].factor : 256;
        shift_item(&scene->shown[i], dx * f / 256, dy * f / 256);
    }
    scene->window_x       = (int16_t)(scene->window_x - dx);
    scene->window_y       = (int16_t)(scene->window_y - dy);
    scene->shown_window_x = (int16_t)(scene->shown_window_x - dx);
    scene->shown_window_y = (int16_t)(scene->shown_window_y - dy);
    return true;
}

/* Where an item lies in the frame now, cut by its own clip; empty when it shows nothing. */
static int floor_div(int a, int b);

static dgx_ge_rect_t item_box(const dgx_ge_scene_t *scene, const dgx_ge_item_t *item, dgx_ge_rect_t *clip)
{
    const bool fill = item->flags & DGX_GE_ITEM_FILL, over_clip = fill || (item->flags & DGX_GE_ITEM_REPEAT);
    if ((!item->image && !fill) || item->layer >= scene->layer_count) return dgx_ge_rect(0, 0, 0, 0);
    const int     factor = scene->layers[item->layer].factor;
    const int     dx = -(scene->window_x * factor >> 8), dy = -(scene->window_y * factor >> 8);
    dgx_ge_rect_t box = over_clip ? dgx_ge_rect(item->clip.x + dx, item->clip.y + dy, item->clip.w, item->clip.h)
                             : dgx_ge_image_box_scaled(item->image, item->x + dx, item->y + dy, item->flags & DGX_GE_BLIT_FLIP_X, item->scale);
    dgx_ge_rect_t own = scene->frame->canvas.clip;
    if (!dgx_ge_rect_empty(item->clip)) own = dgx_ge_rect_intersect(own, dgx_ge_rect(item->clip.x + dx, item->clip.y + dy, item->clip.w, item->clip.h));
    if (clip) *clip = own;
    return dgx_ge_rect_intersect(box, own);
}

static void invalidate_item(dgx_ge_scene_t *scene, const dgx_ge_item_t *item)
{
    dgx_ge_rect_t box = item_box(scene, item, NULL);
    if (!dgx_ge_rect_empty(box)) dgx_ge_frame_invalidate(scene->frame, box);
}

int dgx_ge_scene_add(dgx_ge_scene_t *scene, const dgx_ge_item_t *item)
{
    if (scene->set_count >= scene->set_capacity) return -1;
    scene->set[scene->set_count] = *item;
    if (scene->window_shown && !scene->quiet) invalidate_item(scene, item);
    return scene->set_count++;
}

void dgx_ge_scene_set_begin(dgx_ge_scene_t *scene)
{
    scene->set_count = 0;
    scene->quiet     = true;
}

void dgx_ge_scene_set_end(dgx_ge_scene_t *scene)
{
    scene->quiet = false;
}

void dgx_ge_scene_invalidate(dgx_ge_scene_t *scene, int layer, dgx_ge_rect_t place)
{
    const int factor = layer >= 0 && layer < scene->layer_count ? scene->layers[layer].factor : 256;
    place.x          = (int16_t)(place.x - (scene->window_x * factor >> 8));
    place.y          = (int16_t)(place.y - (scene->window_y * factor >> 8));
    dgx_ge_frame_invalidate(scene->frame, place);
}

void dgx_ge_scene_change(dgx_ge_scene_t *scene, int handle, const dgx_ge_item_t *item)
{
    if (handle < 0 || handle >= scene->set_count) return;
    if (scene->window_shown) invalidate_item(scene, &scene->set[handle]);
    scene->set[handle] = *item;
    if (scene->window_shown) invalidate_item(scene, item);
}

void dgx_ge_scene_actors_begin(dgx_ge_scene_t *scene)
{
    dgx_ge_frame_overlays_begin(scene->frame);
    scene->actor_count = 0;
    scene->group_start = 0;
}

void dgx_ge_scene_space(dgx_ge_scene_t *scene, int layer, dgx_ge_space_fn fn, const void *space)
{
    if (layer < 0 || layer >= scene->layer_count) return;
    scene->layers[layer].space_fn = fn;
    scene->layers[layer].space    = space;
}

/* An actor seen in the open space of its layer: its clip, from its body, becomes what is seen of it. */
static void seen_in_space(const dgx_ge_scene_t *scene, dgx_ge_item_t *item)
{
    if (!(item->flags & DGX_GE_ITEM_OPEN_SPACE)) return;
    const bool inside = item->flags & DGX_GE_ITEM_INSIDE;
    item->flags &= (uint8_t)~(DGX_GE_ITEM_OPEN_SPACE | DGX_GE_ITEM_INSIDE);
    const dgx_ge_layer_t *layer = item->layer < scene->layer_count ? &scene->layers[item->layer] : NULL;
    const dgx_ge_rect_t   body  = !dgx_ge_rect_empty(item->clip) ? item->clip
                                  : item->image                 ? dgx_ge_image_box_scaled(item->image, item->x, item->y, item->flags & DGX_GE_BLIT_FLIP_X, item->scale)
                                                                : dgx_ge_rect(0, 0, 0, 0);
    item->clip = layer && layer->space_fn && !dgx_ge_rect_empty(body) ? layer->space_fn(layer->space, body, body.x + body.w / 2 + item->feet_dx, body.y + body.h - 1, inside)
                                                                      : dgx_ge_rect(0, 0, 0, 0);
}

bool dgx_ge_scene_actor(dgx_ge_scene_t *scene, const dgx_ge_item_t *item)
{
    if (item->layer < scene->layer_count && scene->layers[item->layer].pixels) return dgx_ge_frame_overlay(scene->frame, item);
    if (scene->actor_count >= scene->actor_capacity) return false;
    scene->group_start                    = scene->actor_count;
    scene->actor_z[scene->actor_count]    = 0;
    scene->actors[scene->actor_count]     = *item;
    seen_in_space(scene, &scene->actors[scene->actor_count++]);
    return true;
}

bool dgx_ge_scene_actor_child(dgx_ge_scene_t *scene, const dgx_ge_item_t *item, int z)
{
    if (scene->actor_count >= scene->actor_capacity || scene->group_start >= scene->actor_count) return false;
    if (z == 0) z = 1;
    if (z < -128) z = -128;
    if (z > 127) z = 127;
    /* among the carrier and its children, kept by z: those behind before the carrier (z 0), those before it after */
    int at = scene->group_start;
    while (at < scene->actor_count && scene->actor_z[at] <= z) ++at;
    memmove(&scene->actors[at + 1], &scene->actors[at], (size_t)(scene->actor_count - at) * sizeof(dgx_ge_item_t));
    memmove(&scene->actor_z[at + 1], &scene->actor_z[at], (size_t)(scene->actor_count - at));
    scene->actors[at]  = *item;
    scene->actor_z[at] = (int8_t)z;
    ++scene->actor_count;
    /* seen where its carrier is: the carrier stands first of the group, at z 0 */
    scene->actors[at].flags &= (uint8_t)~DGX_GE_ITEM_INSIDE;
    if (item->flags & DGX_GE_ITEM_OPEN_SPACE) {
        scene->actors[at].flags &= (uint8_t)~DGX_GE_ITEM_OPEN_SPACE;
        for (int i = scene->group_start; i < scene->actor_count; ++i)
            if (scene->actor_z[i] == 0) scene->actors[at].clip = scene->actors[i].clip;
    }
    return true;
}

/* One picture as another: the same pixels at the same place of it (pointers aside: a game may keep one picture and write another into it). */
static bool same_picture(const dgx_image_t *a, const dgx_image_t *b)
{
    if (a == b) return true;
    return a && b && a->data == b->data && a->width == b->width && a->height == b->height && a->origin_x == b->origin_x &&
           a->origin_y == b->origin_y && a->format == b->format;
}

/* An actor drawn as it was shown last? */
static bool same_shown(const dgx_ge_item_t *shown, const dgx_ge_item_t *actor)
{
    if (!same_picture(shown->image, actor->image)) return false;
    dgx_ge_item_t a = *actor;
    a.image         = shown->image;
    return dgx_ge_item_same(shown, &a);
}

static uint32_t order_of(const dgx_ge_scene_t *scene, const dgx_ge_item_t *item, uint32_t number)
{
    const uint32_t layer = item->layer < scene->layer_count ? item->layer : 0;
    const uint32_t key   = scene->layers[layer].order == DGX_GE_ORDER_KEY ? (uint32_t)(item->key + 32768) : 0u;
    return layer << 29 | key << 13 | number;
}

static int compare_order(const void *a, const void *b)
{
    const uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

void dgx_ge_scene_draw(dgx_ge_scene_t *scene)
{
    dgx_ge_frame_t *frame = scene->frame;
    dgx_ge_frame_overlays_end(frame); /* the layers in pixels of the display: what changed there marks the frame under it */

    /* what has changed since the last time */
    if (!scene->window_shown || scene->window_x != scene->shown_window_x || scene->window_y != scene->shown_window_y) {
        /*
         * The window moved: the picture goes the other way. When every layer moves with the window (factor 256; a
         * layer in pixels stays anyway) the frame scrolls what it has and the strips that came in are drawn; a
         * layer that moves otherwise (a far one, a fixed one) needs all of it drawn anew.
         */
        bool together = scene->window_shown;
        for (int l = 0; l < scene->layer_count && together; ++l) together = scene->layers[l].pixels || scene->layers[l].factor == 256;
        if (together) dgx_ge_frame_scroll(frame, scene->shown_window_x - scene->window_x, scene->shown_window_y - scene->window_y);
        else dgx_ge_frame_invalidate_all(frame);
        scene->window_shown   = true;
        scene->shown_window_x = scene->window_x;
        scene->shown_window_y = scene->window_y;
        /* the actors as shown: those that did not move with the window are told apart below as usual */
        const int both = scene->actor_count > scene->shown_count ? scene->actor_count : scene->shown_count;
        for (int i = 0; together && i < both; ++i) {
            const bool was = i < scene->shown_count, is = i < scene->actor_count;
            if (was && is && same_shown(&scene->shown[i], &scene->actors[i])) continue;
            if (was) invalidate_item(scene, &scene->shown[i]);
            if (is) invalidate_item(scene, &scene->actors[i]);
        }
    } else {
        const int both = scene->actor_count > scene->shown_count ? scene->actor_count : scene->shown_count;
        for (int i = 0; i < both; ++i) {
            const bool was = i < scene->shown_count, is = i < scene->actor_count;
            if (was && is && same_shown(&scene->shown[i], &scene->actors[i])) continue;
            if (was) invalidate_item(scene, &scene->shown[i]);
            if (is) invalidate_item(scene, &scene->actors[i]);
        }
    }
    /* the actors as drawn now, each with its picture as it is now: what the game writes into its own later is not this */
    memcpy(scene->shown, scene->actors, scene->actor_count * sizeof(dgx_ge_item_t));
    for (int i = 0; i < scene->actor_count; ++i)
        if (scene->shown[i].image) scene->shown_images[i] = *scene->shown[i].image, scene->shown[i].image = &scene->shown_images[i];
    scene->shown_count = scene->actor_count;
    if (!frame->dirty_count) return;

    /* the order of drawing */
    int n = 0;
    for (int i = 0; i < scene->set_count; ++i) scene->order[n++] = order_of(scene, &scene->set[i], (uint32_t)i);
    for (int i = 0; i < scene->actor_count; ++i) scene->order[n++] = order_of(scene, &scene->actors[i], ORDER_ACTOR | (uint32_t)i);
    qsort(scene->order, (size_t)n, sizeof(uint32_t), compare_order);

    /* every changed place from the paper up; DGX marks what it draws in the buffer, as one batch */
    dgx_screen_progress_up(frame->buffer);
    dgx_ge_canvas_t canvas = frame->canvas;
    for (int d = 0; d < frame->dirty_count; ++d) {
        const dgx_ge_rect_t place = frame->dirty[d];
        canvas.clip               = place;
        dgx_ge_canvas_fill(&canvas, place, scene->paper);
        for (int k = 0; k < n; ++k) {
            const uint32_t       o = scene->order[k], i = o & ORDER_INDEX_MASK;
            const dgx_ge_item_t *item = o & ORDER_ACTOR ? &scene->actors[i] : &scene->set[i];
            dgx_ge_rect_t        clip;
            dgx_ge_rect_t        box = item_box(scene, item, &clip);
            if (!dgx_ge_rect_overlap(box, place)) continue;
            const int factor = scene->layers[item->layer].factor;
            dgx_ge_item_draw(frame->buffer, item, item->x - (scene->window_x * factor >> 8), item->y - (scene->window_y * factor >> 8), box,
                             dgx_ge_rect_intersect(clip, place));
        }
    }
    dgx_screen_progress_down(frame->buffer);
}

/* a / b rounded down, b > 0 */
static int floor_div(int a, int b)
{
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

/* where the window is for a layer: it moves by its factor */
static void layer_window(const dgx_ge_scene_t *scene, int layer, int *wx, int *wy)
{
    const int factor = layer >= 0 && layer < scene->layer_count ? scene->layers[layer].factor : 256;
    *wx              = scene->window_x * factor >> 8;
    *wy              = scene->window_y * factor >> 8;
}

bool dgx_ge_scene_point_at(const dgx_ge_scene_t *scene, int layer, int display_x, int display_y, int *x, int *y)
{
    const dgx_ge_frame_t *frame = scene->frame;
    const int             fx = display_x - frame->display_x, fy = display_y - frame->display_y, n = frame->scale;
    const int             w = dgx_ge_frame_width(frame) * n, h = dgx_ge_frame_height(frame) * n;
    int                   wx, wy;
    layer_window(scene, layer, &wx, &wy);
    /* pixels of the display to points: a point is n pixels */
    *x = wx + floor_div(fx, n);
    *y = wy + floor_div(fy, n);
    return fx >= 0 && fy >= 0 && fx < w && fy < h;
}

void dgx_ge_scene_pixel_at(const dgx_ge_scene_t *scene, int layer, int x, int y, int *display_x, int *display_y)
{
    int wx, wy;
    layer_window(scene, layer, &wx, &wy);
    *display_x = scene->frame->display_x + (x - wx) * scene->frame->scale;
    *display_y = scene->frame->display_y + (y - wy) * scene->frame->scale;
}
