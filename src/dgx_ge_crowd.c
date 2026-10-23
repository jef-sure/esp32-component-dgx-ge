#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#include "dgx_ge_crowd.h"

static const char *TAG = "dgx-ge";

#define HEADER_BYTES 24u
#define KIND_BYTES   8u
#define GROUP_BYTES  16u
#define VERSION      2u
#define NO_PARAM     0xFF
#define WALL         4096 /* how thick the solid beyond the bounds is, points */

static inline uint16_t u16_at(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static inline int16_t i16_at(const uint8_t *p)
{
    return (int16_t)u16_at(p);
}

static inline uint32_t u32_at(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static const uint8_t *kind_at(const dgx_ge_crowd_t *c, int i)
{
    return c->base + HEADER_BYTES + i * KIND_BYTES;
}

static const uint8_t *group_at(const dgx_ge_crowd_t *c, int i)
{
    return c->base + HEADER_BYTES + c->kind_count * KIND_BYTES + i * GROUP_BYTES;
}

bool dgx_ge_crowd_open(dgx_ge_crowd_t *c, const void *data, size_t size)
{
    memset(c, 0, sizeof(*c));
    const uint8_t *p = data;
    if (!p || size < HEADER_BYTES || u32_at(p) != DGX_GE_CROWD_MAGIC || u16_at(p + 4) != VERSION) return false;
    const uint16_t places = u16_at(p + 6), kinds = u16_at(p + 8), groups = u16_at(p + 10);
    /* a place and a group name their kind by a byte */
    if (!places || kinds > 255 || HEADER_BYTES + (size_t)kinds * KIND_BYTES + (size_t)groups * GROUP_BYTES > size) return false;
    c->base       = p;
    c->places     = places;
    c->kind_count = kinds;
    c->groups     = groups;
    c->bounds     = dgx_ge_rect(i16_at(p + 12), i16_at(p + 14), u16_at(p + 16), u16_at(p + 18));
    c->seed       = u32_at(p + 20);
    for (int g = 0; g < groups; ++g)
        if (group_at(c, g)[0] >= kinds) return memset(c, 0, sizeof(*c)), false;
    return true;
}

/* The kind of a blob among the crowd's, -1 when it is none of them. */
static int entry_of(const dgx_ge_crowd_t *c, int kind)
{
    for (int i = 0; i < c->kind_count; ++i)
        if (u16_at(kind_at(c, i)) == kind) return i;
    return -1;
}

static int place_of(const dgx_ge_crowd_t *c, const dgx_ge_actor_t *a)
{
    return (int)((const dgx_ge_member_t *)(const void *)a - c->members);
}

/* The next number of the crowd's dice, 0..n - 1 (n > 0), as rnd casts them. */
static uint32_t cast(dgx_ge_crowd_t *c, uint32_t n)
{
    c->seed = c->seed * 214013u + 2531011u;
    return (c->seed >> 16) % n;
}

/* ---- the terrain of its actors ---- */

/* Where an actor's picture stands: as it is drawn. */
static void origin_of(const dgx_ge_actor_t *a, int32_t x, int32_t y, int *ox, int *oy)
{
    int32_t dx = a->image_dx, dy = a->image_dy;
    dgx_ge_orient_apply(dgx_ge_actor_orient(a), &dx, &dy);
    *ox = (x >> 8) + dx;
    *oy = (y >> 8) + dy;
}

/* The solid beyond the bounds: four slabs round it. */
static void walls_of(const dgx_ge_crowd_t *c, dgx_ge_rect_t w[4])
{
    const dgx_ge_rect_t b = c->bounds;
    w[0] = dgx_ge_rect(b.x - WALL, b.y - WALL, b.w + 2 * WALL, WALL); /* above */
    w[1] = dgx_ge_rect(b.x - WALL, b.y + b.h, b.w + 2 * WALL, WALL);  /* below */
    w[2] = dgx_ge_rect(b.x - WALL, b.y, WALL, b.h);                   /* left */
    w[3] = dgx_ge_rect(b.x + b.w, b.y, WALL, b.h);                    /* right */
}

static bool touches(const dgx_ge_actor_t *a, int32_t x, int32_t y, dgx_ge_contact_t *contact, void *ctx)
{
    const dgx_ge_crowd_t *c = ctx;
    if (c->grid && dgx_ge_grid_touches(a, x, y, contact, (void *)c->grid)) return true;
    const dgx_ge_rect_t b = c->bounds;
    const int px = x >> 8, py = y >> 8;
    if (px >= b.x && py >= b.y && px < b.x + b.w && py < b.y + b.h) return false;
    if (!contact) return true; /* asked only whether */
    /* beyond the bounds: the normal of the side it went out by */
    contact->x = x, contact->y = y;
    contact->nx = px < b.x ? 1.0f : px >= b.x + b.w ? -1.0f : 0.0f;
    contact->ny = py < b.y ? 1.0f : py >= b.y + b.h ? -1.0f : 0.0f;
    contact->surface = 0;
    return true;
}

static uint8_t sides(const dgx_ge_actor_t *a, int32_t x, int32_t y, int mask, int reach, void *ctx)
{
    const dgx_ge_crowd_t *c = ctx;
    if (a->image < 0) return 0;
    int ox, oy;
    origin_of(a, x, y, &ox, &oy);
    const unsigned      o    = dgx_ge_actor_orient(a);
    const dgx_ge_body_t body = {c->pack, a->image, ox, oy, (uint8_t)o, (uint16_t)(1u << (mask & 15))};
    unsigned            s    = c->grid ? dgx_ge_grid_touch_sides(c->grid, &body, mask, reach) : 0;
    /* the bounds as walls: the mask of that number of its picture against the slabs round them */
    dgx_ge_mask_t masks[16];
    const int     n = dgx_ge_pack_masks(c->pack, a->image, masks, 16);
    for (int i = 0; i < n && i < 16; ++i) {
        if (masks[i].index != mask) continue;
        dgx_ge_rect_t       w[4];
        dgx_ge_area_t       areas[DGX_GE_TOUCH_SIDES];
        const dgx_ge_rect_t place = dgx_ge_mask_place(&masks[i], ox, oy, o);
        walls_of(c, w);
        for (int k = 0; k < 4; ++k) s |= dgx_ge_mask_touch_rect_among(&masks[i], place, o, w[k], reach, w, 4, areas);
    }
    return (uint8_t)s;
}

static int sight(int x0, int y0, int x1, int y1, int radius, void *ctx)
{
    return dgx_ge_grid_sight(((const dgx_ge_crowd_t *)ctx)->grid, x0, y0, x1, y1, radius);
}

static bool sense(const dgx_ge_actor_t *a, const dgx_ge_sense_t *query, dgx_ge_sensed_t *found, void *ctx)
{
    const dgx_ge_crowd_t *c = ctx;
    if (query->cells) return c->grid && dgx_ge_sense_cells(c->grid, a, query, found);
    return dgx_ge_sense_bodies(c->bodies, (uint16_t)(place_of(c, a) + 1), a, query, found);
}

/* ---- places ---- */

/* An actor of a kind of the crowd into a free place, at x, y (points), in a script, from a moment; NULL: none free. */
static dgx_ge_member_t *add(dgx_ge_crowd_t *c, int entry, int script, int x, int y, bool flip, unsigned turn, uint32_t at_ms)
{
    const dgx_ge_anim_t *anim = dgx_ge_kind(c->kinds, u16_at(kind_at(c, entry)));
    if (!anim) return NULL;
    for (int i = 0; i < c->places; ++i) {
        dgx_ge_member_t *m = &c->members[i];
        if (m->kind != DGX_GE_CROWD_FREE) continue;
        memset(m, 0, sizeof(*m));
        dgx_ge_actor_start(&m->actor, anim, script < anim->scripts ? script : 0, x, y, at_ms);
        m->actor.flip    = flip;
        m->actor.turn    = (uint8_t)(turn & 3u);
        m->actor.terrain = &c->terrain;
        m->kind          = u16_at(kind_at(c, entry));
        m->entry         = (uint8_t)entry;
        return m;
    }
    if (!c->told_full) {
        ESP_LOGW(TAG, "crowd: no free place for one more (of %u)", (unsigned)c->places);
        c->told_full = true;
    }
    return NULL;
}

static void said(dgx_ge_actor_t *a, dgx_ge_anim_say_t what, uint8_t value, uint32_t at_ms, void *ctx)
{
    dgx_ge_crowd_t *c = ctx;
    if (what == DGX_GE_ANIM_APPEAR) {
        const int entry = entry_of(c, dgx_ge_anim_said(2));
        if (entry < 0) {
            ESP_LOGW(TAG, "crowd: kind %d is not of it, none appears", (int)dgx_ge_anim_said(2));
            return;
        }
        dgx_ge_member_t *m = add(c, entry, 0, dgx_ge_anim_said(0), dgx_ge_anim_said(1), dgx_ge_anim_said(3) != 0, 0, at_ms);
        if (!m) return;
        dgx_ge_anim_said_params(&m->actor); /* its params and those it entered its script with */
        dgx_ge_actor_event(&m->actor, value, at_ms, said, c);
        m->born = true;
        return;
    }
    if (what == DGX_GE_ANIM_VANISH) {
        ((dgx_ge_member_t *)(void *)a)->gone = true;
        return;
    }
    if (c->say) c->say(a, what, value, at_ms, c->say_ctx);
}

bool dgx_ge_crowd_start(dgx_ge_crowd_t *c, const dgx_ge_pack_t *pack, dgx_ge_kinds_t *kinds, const dgx_ge_grid_t *grid, uint32_t now_ms)
{
    if (!c->base || c->members) return false;
    for (int i = 0; i < c->kind_count; ++i)
        if (!dgx_ge_kind(kinds, u16_at(kind_at(c, i)))) return false;
    c->members = calloc(c->places, sizeof(dgx_ge_member_t));
    c->bodies  = dgx_ge_collisions_create(c->places, 4);
    if (!c->members || !c->bodies) {
        dgx_ge_crowd_stop(c);
        return false;
    }
    dgx_ge_meets_t meets;
    if (dgx_ge_pack_meets(pack, &meets)) dgx_ge_collisions_meets(c->bodies, &meets);
    for (int i = 0; i < c->places; ++i) c->members[i].kind = DGX_GE_CROWD_FREE;
    c->pack      = pack;
    c->kinds     = kinds;
    c->grid      = grid;
    c->seed      = u32_at(c->base + 20);
    c->told_full = false;
    c->terrain   = (dgx_ge_terrain_t){.touches = touches, .sides = sides, .sense = sense, .sight = grid ? sight : NULL, .ctx = c};
    /* the groups strewn over their rectangles by the dice of the seed */
    for (int g = 0; g < c->groups; ++g) {
        const uint8_t *r = group_at(c, g);
        const int      x = i16_at(r + 4), y = i16_at(r + 6), w = u16_at(r + 8), h = u16_at(r + 10);
        for (int n = 0; n < u16_at(r + 2); ++n) {
            const int px = x + (w ? (int)cast(c, (uint32_t)w) : 0), py = y + (h ? (int)cast(c, (uint32_t)h) : 0);
            const bool     flip = r[12] == 2 ? cast(c, 2) != 0 : r[12] == 1;
            const unsigned turn = r[13] == 4 ? cast(c, 4) : r[13];
            if (!add(c, r[0], r[1], px, py, flip, turn, now_ms)) break;
        }
    }
    /* their first steps, and their bodies from the start: what the first play senses is where they began */
    dgx_ge_crowd_play(c, now_ms, NULL, NULL);
    return true;
}

void dgx_ge_crowd_stop(dgx_ge_crowd_t *c)
{
    free(c->members);
    c->members = NULL;
    dgx_ge_collisions_destroy(&c->bodies);
}

/* The born moved on to now (they may bear others), the gone given up. */
static void settle(dgx_ge_crowd_t *c)
{
    for (bool any = true; any;) {
        any = false;
        for (int i = 0; i < c->places; ++i) {
            dgx_ge_member_t *m = &c->members[i];
            if (m->kind == DGX_GE_CROWD_FREE || !m->born) continue;
            m->born = false;
            any     = true;
            if (!m->gone) dgx_ge_actor_advance(&m->actor, c->now_ms, said, c);
        }
    }
    for (int i = 0; i < c->places; ++i) {
        dgx_ge_member_t *m = &c->members[i];
        if (m->kind == DGX_GE_CROWD_FREE || !m->gone) continue;
        dgx_ge_collisions_remove(c->bodies, i);
        m->kind = DGX_GE_CROWD_FREE;
    }
}

/* A hit between two of the crowd: none on one gone, or by it (it is seen until the play is over, but does nothing). */
static dgx_ge_hit_verdict_t living(int by, dgx_ge_actor_t *a, int on, dgx_ge_actor_t *b, dgx_ge_hit_t *hit, void *ctx)
{
    (void)by, (void)on, (void)hit, (void)ctx;
    return ((dgx_ge_member_t *)(void *)a)->gone || ((dgx_ge_member_t *)(void *)b)->gone ? DGX_GE_HIT_PASS : DGX_GE_HIT_SEND;
}

/* The bodies as they are now: a place showing a picture is a body, its owner the place, its side its kind's, its damage off its kind's param. */
static void bodies(dgx_ge_crowd_t *c)
{
    for (int i = 0; i < c->places; ++i) {
        dgx_ge_member_t *m = &c->members[i];
        if (m->kind == DGX_GE_CROWD_FREE) {
            dgx_ge_collisions_remove(c->bodies, i);
            continue;
        }
        const uint8_t *k = kind_at(c, m->entry);
        dgx_ge_collisions_set_actor(c->bodies, i, c->pack, &m->actor, (uint16_t)(i + 1), u16_at(k + 2));
        dgx_ge_collisions_damage(c->bodies, i, k[5] == NO_PARAM ? -1 : k[5]);
    }
}

void dgx_ge_crowd_play(dgx_ge_crowd_t *c, uint32_t now_ms, dgx_ge_anim_say_fn say, void *ctx)
{
    if (!c->members) return;
    uint32_t *dice = dgx_ge_dice;
    dgx_ge_dice    = &c->seed;
    c->say = say, c->say_ctx = ctx, c->now_ms = now_ms;
    for (int i = 0; i < c->places; ++i)
        if (c->members[i].kind != DGX_GE_CROWD_FREE && !c->members[i].gone) dgx_ge_actor_advance(&c->members[i].actor, now_ms, said, c);
    settle(c);
    bodies(c);
    dgx_ge_collisions_hits(c->bodies, now_ms, living, said, c);
    settle(c);
    bodies(c);
    c->say = NULL, c->say_ctx = NULL;
    dgx_ge_dice = dice;
}

bool dgx_ge_crowd_items(dgx_ge_crowd_t *c, const uint8_t *const *maps, dgx_ge_scene_t *scene)
{
    if (!c->members) return true;
    bool all = true;
    for (int i = 0; i < c->places; ++i) {
        dgx_ge_member_t *m = &c->members[i];
        if (m->kind == DGX_GE_CROWD_FREE || m->actor.image < 0 || !dgx_ge_pack_image(c->pack, m->actor.image, &m->image)) continue;
        const uint8_t *k = kind_at(c, m->entry);
        int            ox, oy;
        origin_of(&m->actor, m->actor.x, m->actor.y, &ox, &oy);
        const int     set  = dgx_ge_pack_image_set(c->pack, m->actor.image);
        dgx_ge_item_t item = {.image = &m->image,
                              .x     = (int16_t)ox,
                              .y     = (int16_t)oy,
                              .key   = (int16_t)(k[7] ? oy : 0),
                              .layer = k[4],
                              .flags = m->actor.flip ? DGX_GE_BLIT_FLIP_X : 0,
                              .turn  = m->actor.turn,
                              .color = k[6]};
        item.map = m->image.format == 1 || !maps || set < 0 ? NULL : maps[set];
        /* one of 1 bit: in a color of its set, as the frame has it */
        if (m->image.format == 1 && item.color >= 1 && item.color <= 15 && maps && set >= 0 && maps[set]) item.color = maps[set][item.color - 1];
        all &= dgx_ge_scene_actor(scene, &item);
    }
    return all;
}

int dgx_ge_crowd_count(const dgx_ge_crowd_t *c, uint16_t kind)
{
    int n = 0;
    for (int i = 0; c->members && i < c->places; ++i)
        if (c->members[i].kind != DGX_GE_CROWD_FREE && !c->members[i].gone && (kind == DGX_GE_CROWD_FREE || c->members[i].kind == kind)) ++n;
    return n;
}
