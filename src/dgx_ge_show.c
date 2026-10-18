#include <stdlib.h>
#include <string.h>

#include "dgx_ge_show.h"

#define HEADER_BYTES  12u
#define PICTURE_BYTES 10u
#define ACTOR_BYTES   10u
#define CUE_BYTES     8u

static uint16_t u16_at(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static int16_t  i16_at(const uint8_t *p) { return (int16_t)u16_at(p); }
static uint32_t u32_at(const uint8_t *p) { return (uint32_t)u16_at(p) | (uint32_t)u16_at(p + 2) << 16; }

bool dgx_ge_show_open(dgx_ge_show_t *s, const void *data, size_t size)
{
    memset(s, 0, sizeof(*s));
    const uint8_t *p = data;
    if (!p || size < HEADER_BYTES || u32_at(p) != DGX_GE_SHOW_MAGIC || u16_at(p + 4) != 1) return false;
    const uint16_t pictures = u16_at(p + 6), actors = u16_at(p + 8), cues = u16_at(p + 10);
    if (HEADER_BYTES + pictures * PICTURE_BYTES + actors * ACTOR_BYTES + cues * CUE_BYTES > size) return false;
    s->base = p, s->pictures = pictures, s->actors = actors, s->cues = cues;
    return true;
}

static const uint8_t *picture_at(const dgx_ge_show_t *s, int i) { return s->base + HEADER_BYTES + i * PICTURE_BYTES; }
static const uint8_t *actor_at(const dgx_ge_show_t *s, int i) { return s->base + HEADER_BYTES + s->pictures * PICTURE_BYTES + i * ACTOR_BYTES; }
static const uint8_t *cue_at(const dgx_ge_show_t *s, int i)
{
    return s->base + HEADER_BYTES + s->pictures * PICTURE_BYTES + s->actors * ACTOR_BYTES + i * CUE_BYTES;
}

void dgx_ge_show_stop(dgx_ge_show_t *s)
{
    free(s->cast);
    free(s->images);
    s->cast = NULL, s->images = NULL;
}

bool dgx_ge_show_start(dgx_ge_show_t *s, dgx_ge_kinds_t *kinds, uint32_t now_ms)
{
    dgx_ge_show_stop(s);
    s->start_ms = now_ms, s->next_cue = 0, s->over = false;
    s->cast     = s->actors ? calloc(s->actors, sizeof(dgx_ge_actor_t)) : NULL;
    s->images   = calloc((size_t)s->actors + s->pictures + 1, sizeof(dgx_image_t));
    if ((s->actors && !s->cast) || !s->images) {
        dgx_ge_show_stop(s);
        return false;
    }
    for (int i = 0; i < s->actors; ++i) {
        const uint8_t       *a    = actor_at(s, i);
        const dgx_ge_anim_t *anim = dgx_ge_kind(kinds, u16_at(a));
        if (!anim) {
            dgx_ge_show_stop(s);
            return false;
        }
        dgx_ge_actor_start(&s->cast[i], anim, a[6], i16_at(a + 2), i16_at(a + 4), now_ms);
        s->cast[i].flip = a[8] != 0;
    }
    return true;
}

bool dgx_ge_show_play(dgx_ge_show_t *s, uint32_t now_ms, dgx_ge_anim_say_fn say, void *ctx)
{
    if (s->over || !s->images) return true; /* not begun (its pictures are kept from its start, actors or none) */
    /* the cues come, each at its own moment: the actors up to it first, then told */
    while (s->next_cue < s->cues) {
        const uint8_t *c  = cue_at(s, s->next_cue);
        const uint32_t at = s->start_ms + u32_at(c);
        if ((int32_t)(now_ms - at) < 0) break;
        ++s->next_cue;
        if (c[4] == DGX_GE_SHOW_END) {
            s->over = true;
            return true;
        }
        if (c[4] >= s->actors) continue;
        for (int i = 0; i < s->actors; ++i) dgx_ge_actor_advance(&s->cast[i], at, say, ctx);
        dgx_ge_actor_event(&s->cast[c[4]], c[5], at, say, ctx);
    }
    for (int i = 0; i < s->actors; ++i) dgx_ge_actor_advance(&s->cast[i], now_ms, say, ctx);
    return false;
}

void dgx_ge_show_skip(dgx_ge_show_t *s)
{
    s->over = true;
}

/* A picture of the pack as an item: by the map of its set (none: one of 1 bit, in its color). */
int dgx_ge_show_picture(const dgx_ge_show_t *s, int i)
{
    return s->base && i >= 0 && i < s->pictures ? u16_at(picture_at(s, i)) : -1;
}

static bool item_of(const dgx_ge_pack_t *pack, const uint8_t *const *maps, int id, dgx_image_t *image, dgx_ge_item_t *item)
{
    if (id < 0 || !dgx_ge_pack_image(pack, id, image)) return false;
    const int set = dgx_ge_pack_image_set(pack, id);
    item->image   = image;
    item->map     = image->format == 1 || !maps || set < 0 ? NULL : maps[set];
    /* one of 1 bit: in a color of its set, as the frame has it */
    if (image->format == 1 && item->color >= 1 && item->color <= 15 && maps && set >= 0 && maps[set]) item->color = maps[set][item->color - 1];
    return true;
}

void dgx_ge_show_backdrop(dgx_ge_show_t *s, const dgx_ge_pack_t *pack, const uint8_t *const *maps, dgx_ge_scene_t *scene)
{
    if (!s->images) return;
    for (int i = 0; i < s->pictures; ++i) {
        const uint8_t *p    = picture_at(s, i);
        dgx_ge_item_t  item = {.x = i16_at(p + 2), .y = i16_at(p + 4), .layer = p[6], .flags = p[7], .color = p[8]};
        if (item_of(pack, maps, u16_at(p), &s->images[s->actors + i], &item)) dgx_ge_scene_add(scene, &item);
    }
}

void dgx_ge_show_items(dgx_ge_show_t *s, const dgx_ge_pack_t *pack, const uint8_t *const *maps, dgx_ge_scene_t *scene, dgx_ge_show_place_fn place,
                       void *ctx)
{
    if (!s->cast) return;
    for (int i = 0; i < s->actors; ++i) {
        const dgx_ge_actor_t *a    = &s->cast[i];
        const uint8_t        *d    = actor_at(s, i);
        dgx_ge_item_t         item = {.x = (int16_t)(dgx_ge_actor_x(a) + (a->flip ? -a->image_dx : a->image_dx)),
                                      .y = (int16_t)(dgx_ge_actor_y(a) + a->image_dy), .layer = d[7], .flags = a->flip ? DGX_GE_BLIT_FLIP_X : 0,
                                      .color = d[9]};
        if (place ? place(a, u16_at(d), &item, ctx) : item_of(pack, maps, a->image, &s->images[i], &item)) dgx_ge_scene_actor(scene, &item);
    }
}
