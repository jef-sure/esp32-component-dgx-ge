#include "dgx_ge_layout.h"

#include <string.h>

enum { HEAD = 28, LAYER = 4, PICTURE = 18, ZONE = 10, ENTRY = 6, SOURCE = 6, ACTOR = 10, PARAM = 4, GRID = 14 };

static uint16_t u16_at(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static int16_t i16_at(const uint8_t *p)
{
    return (int16_t)u16_at(p);
}

static uint32_t u32_at(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* where the fixed parts begin: the layers, then the pictures, zones, entries, sources of links, the offsets of the actors */
static uint32_t pictures_at(const dgx_ge_layout_t *l)
{
    return HEAD + LAYER * l->layers;
}

static uint32_t zones_at(const dgx_ge_layout_t *l)
{
    return pictures_at(l) + PICTURE * l->pictures;
}

static uint32_t entries_at(const dgx_ge_layout_t *l)
{
    return zones_at(l) + ZONE * l->zones;
}

static uint32_t sources_at(const dgx_ge_layout_t *l)
{
    return entries_at(l) + ENTRY * l->entries;
}

static uint32_t offsets_at(const dgx_ge_layout_t *l)
{
    return sources_at(l) + SOURCE * l->sources;
}

/* a grid's size from its head at, its parts as laid; 0: it does not fit in the blob */
static uint32_t grid_size(const uint8_t *data, uint32_t at, uint32_t size)
{
    if (at + GRID > size) return 0;
    const uint8_t *g     = data + at;
    const uint32_t cells = (uint32_t)u16_at(g + 6) * u16_at(g + 8), flags = g[12], slots = g[13];
    uint32_t       end   = at + GRID + slots;
    end += end & 1;
    end += 2 * cells;
    if (flags & 1) end += cells;
    if (flags & 2) end += (4 - end % 4) % 4 + 4 * cells;
    end += (4 - end % 4) % 4;
    return end <= size ? end - at : 0;
}

bool dgx_ge_layout_open(dgx_ge_layout_t *l, const void *data, size_t size)
{
    memset(l, 0, sizeof(*l));
    const uint8_t *p = data;
    if (!p || size < HEAD || u32_at(p) != DGX_GE_LAYOUT_MAGIC || u16_at(p + 4) != DGX_GE_LAYOUT_VERSION) return false;
    dgx_ge_layout_t t = {.data = p, .size = (uint32_t)size, .layers = p[6], .grids = p[7], .width = i16_at(p + 8), .height = i16_at(p + 10),
                         .pictures = u16_at(p + 12), .actors = u16_at(p + 14), .zones = u16_at(p + 16), .entries = u16_at(p + 18),
                         .sources = u16_at(p + 20), .grids_at = u32_at(p + 24)};
    if (!t.layers || t.layers > DGX_GE_SCENE_MAX_LAYERS || offsets_at(&t) + 2u * t.actors > t.size || t.grids_at > t.size || t.grids_at % 4) return false;
    /* every actor whole, every grid whole: what is asked of them later is there */
    for (int n = 0; n < t.actors; ++n) {
        const uint32_t at = u16_at(p + offsets_at(&t) + 2 * n);
        if (at + ACTOR > t.size || at + ACTOR + PARAM * (uint32_t)p[at + 9] > t.size) return false;
    }
    uint32_t at = t.grids_at;
    for (int n = 0; n < t.grids; ++n) {
        const uint32_t s = grid_size(p, at, t.size);
        if (!s || p[at + 13] > DGX_GE_SCENE_MAX_LAYERS) return false;
        at += s;
    }
    *l = t;
    return true;
}

bool dgx_ge_layout_scene(const dgx_ge_layout_t *l, dgx_ge_scene_t *scene, const dgx_ge_pack_t *pack, const uint8_t *const *maps,
                         dgx_image_t *images)
{
    if (!scene || scene->layer_count < l->layers) return false;
    for (int i = 0; i < l->layers; ++i) {
        const uint8_t *y = l->data + HEAD + LAYER * i;
        dgx_ge_scene_layer(scene, i, i16_at(y), y[2] ? DGX_GE_ORDER_KEY : DGX_GE_ORDER_ADDED);
    }
    for (int i = 0; i < l->pictures; ++i) {
        const uint8_t *q = l->data + pictures_at(l) + PICTURE * i;
        const int      id = u16_at(q), set = dgx_ge_pack_image_set(pack, id);
        if (!dgx_ge_pack_image(pack, id, &images[i])) return false;
        const dgx_ge_item_t item = {.image = &images[i], .map = maps && set >= 0 ? maps[set] : NULL, .x = i16_at(q + 2), .y = i16_at(q + 4),
                                    .key = i16_at(q + 6), .layer = q[8], .flags = q[9] & 1 ? DGX_GE_BLIT_FLIP_X : 0,
                                    .clip = dgx_ge_rect(i16_at(q + 10), i16_at(q + 12), i16_at(q + 14), i16_at(q + 16))};
        if (dgx_ge_scene_add(scene, &item) < 0) return false;
    }
    return true;
}

bool dgx_ge_layout_zone(const dgx_ge_layout_t *l, int n, dgx_ge_rect_t *place, int *kind)
{
    if (n < 0 || n >= l->zones) return false;
    const uint8_t *z = l->data + zones_at(l) + ZONE * n;
    if (place) *place = dgx_ge_rect(i16_at(z + 2), i16_at(z + 4), i16_at(z + 6), i16_at(z + 8));
    if (kind) *kind = u16_at(z);
    return true;
}

bool dgx_ge_layout_entry(const dgx_ge_layout_t *l, int n, int *x, int *y, bool *left)
{
    if (n < 0 || n >= l->entries) return false;
    const uint8_t *e = l->data + entries_at(l) + ENTRY * n;
    if (x) *x = i16_at(e);
    if (y) *y = i16_at(e + 2);
    if (left) *left = e[4] & 1;
    return true;
}

bool dgx_ge_layout_actor(const dgx_ge_layout_t *l, int n, dgx_ge_layout_actor_t *a)
{
    if (n < 0 || n >= l->actors) return false;
    const uint8_t *r = l->data + u16_at(l->data + offsets_at(l) + 2 * n);
    *a = (dgx_ge_layout_actor_t){.kind = u16_at(r), .script = r[2], .layer = r[3], .x = i16_at(r + 4), .y = i16_at(r + 6), .flip = r[8] & 1,
                                 .params = r[9], .given = r + ACTOR};
    return true;
}

bool dgx_ge_layout_actor_start(const dgx_ge_layout_t *l, int n, dgx_ge_actor_t *actor, dgx_ge_kinds_t *kinds, uint32_t now_ms)
{
    dgx_ge_layout_actor_t a;
    const dgx_ge_anim_t  *anim = dgx_ge_layout_actor(l, n, &a) ? dgx_ge_kind(kinds, a.kind) : NULL;
    if (!anim) return false;
    dgx_ge_actor_start(actor, anim, a.script, a.x, a.y, now_ms);
    actor->flip = a.flip;
    for (int i = 0; i < a.params; ++i) {
        const uint8_t *g = a.given + PARAM * i;
        if (g[0] < DGX_GE_ANIM_PARAMS) actor->params[g[0]] = i16_at(g + 2);
    }
    return true;
}

bool dgx_ge_layout_grid(const dgx_ge_layout_t *l, int n, dgx_ge_layout_grid_t *grid)
{
    if (n < 0 || n >= l->grids) return false;
    uint32_t at = l->grids_at;
    for (int i = 0; i < n; ++i) at += grid_size(l->data, at, l->size);
    const uint8_t *g     = l->data + at;
    const uint32_t cells = (uint32_t)u16_at(g + 6) * u16_at(g + 8);
    uint32_t       p     = at + GRID + g[13];
    p += p & 1;
    *grid = (dgx_ge_layout_grid_t){.tiles = u16_at(g), .x = i16_at(g + 2), .y = i16_at(g + 4), .cols = u16_at(g + 6), .rows = u16_at(g + 8),
                                   .first = u16_at(g + 10), .slots = g[13], .layer_of = g + GRID, .cells = (const uint16_t *)(const void *)(l->data + p)};
    p += 2 * cells;
    if (g[12] & 1) grid->mods = l->data + p, p += cells;
    if (g[12] & 2) p += (4 - p % 4) % 4, grid->variants = (const uint32_t *)(const void *)(l->data + p);
    return true;
}

bool dgx_ge_layout_cell_at(const dgx_ge_layout_t *l, int number, int *grid, int *col, int *row)
{
    uint32_t at = l->grids_at;
    for (int n = 0; n < l->grids; ++n) {
        const uint8_t *g = l->data + at;
        const int      cols = u16_at(g + 6), index = number - u16_at(g + 10);
        if (cols && index >= 0 && index < cols * u16_at(g + 8)) {
            if (grid) *grid = n;
            if (col) *col = index % cols;
            if (row) *row = index / cols;
            return true;
        }
        at += grid_size(l->data, at, l->size);
    }
    return false;
}

int dgx_ge_layout_links_of(const dgx_ge_layout_t *l, int kind, int which)
{
    for (int n = 0; n < l->sources; ++n) {
        const uint8_t *src = l->data + sources_at(l) + SOURCE * n;
        if (src[0] == kind && u16_at(src + 2) == which) return u16_at(src + 4);
    }
    return -1;
}

void dgx_ge_layout_grid_set(const dgx_ge_layout_grid_t *grid, dgx_ge_grid_t *g, const dgx_ge_tileset_t *tiles, uint16_t *cells)
{
    g->tileset  = tiles;
    g->cells    = cells;
    g->cols     = grid->cols, g->rows = grid->rows;
    g->x        = grid->x, g->y = grid->y;
    g->mods     = grid->mods;
    g->variants = grid->variants;
    for (int s = 0; s < DGX_GE_SCENE_MAX_LAYERS; ++s) g->layer_of[s] = s < grid->slots ? grid->layer_of[s] : 0xFF;
}
