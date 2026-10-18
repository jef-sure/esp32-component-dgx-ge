#include <string.h>

#include "dgx_ge_graph.h"

#define HEADER_BYTES 18u
#define MAP_BYTES    6u
#define SCENE_BYTES  16u
#define EXIT_BYTES   8u
#define ENTRY_BYTES  10u
#define OF_MAP       1u

static inline uint16_t u16_at(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static const uint8_t *maps_of(const dgx_ge_graph_t *g)
{
    return g->base + HEADER_BYTES;
}

static const uint8_t *scenes_of(const dgx_ge_graph_t *g)
{
    return maps_of(g) + (size_t)g->maps * MAP_BYTES;
}

static const uint8_t *exits_of(const dgx_ge_graph_t *g)
{
    return scenes_of(g) + (size_t)g->scenes * SCENE_BYTES;
}

static const uint8_t *entries_of(const dgx_ge_graph_t *g)
{
    return exits_of(g) + (size_t)g->exits * EXIT_BYTES;
}

bool dgx_ge_graph_open(dgx_ge_graph_t *g, const void *data, size_t size)
{
    memset(g, 0, sizeof(*g));
    const uint8_t *p = data;
    if (!p || size < HEADER_BYTES || (uint32_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24) != DGX_GE_GRAPH_MAGIC ||
        u16_at(p + 4) != DGX_GE_GRAPH_VERSION)
        return false;
    g->base        = p;
    g->scenes      = u16_at(p + 6);
    g->exits       = u16_at(p + 8);
    g->entries     = u16_at(p + 10);
    g->maps        = p[12];
    g->start_scene = u16_at(p + 14);
    g->start_entry = p[16];
    const size_t need = HEADER_BYTES + (size_t)g->maps * MAP_BYTES + (size_t)g->scenes * SCENE_BYTES + (size_t)g->exits * EXIT_BYTES +
                        (size_t)g->entries * ENTRY_BYTES;
    if (need > size) {
        memset(g, 0, sizeof(*g));
        return false;
    }
    return true;
}

bool dgx_ge_graph_scene(const dgx_ge_graph_t *g, int scene, int *map, int *index, int *kind)
{
    if (scene < 0 || scene >= g->scenes) return false;
    const uint8_t *s = scenes_of(g) + (size_t)scene * SCENE_BYTES;
    if (map) *map = s[0];
    if (index) *index = s[1];
    if (kind) *kind = s[2];
    return true;
}

int dgx_ge_graph_scene_event(const dgx_ge_graph_t *g, int scene)
{
    if (scene < 0 || scene >= g->scenes) return -1;
    const uint8_t e = scenes_of(g)[(size_t)scene * SCENE_BYTES + 3];
    return e == 0xFF ? -1 : e;
}

int dgx_ge_graph_leave_event(const dgx_ge_graph_t *g, int scene, int side)
{
    if (scene < 0 || scene >= g->scenes || side < 0 || side > DGX_GE_SIDE_DOWN) return -1;
    const uint8_t e = scenes_of(g)[(size_t)scene * SCENE_BYTES + 12 + side];
    return e == 0xFF ? -1 : e;
}

int dgx_ge_graph_find(const dgx_ge_graph_t *g, int map, int index)
{
    for (int i = 0; i < g->scenes; ++i) {
        const uint8_t *s = scenes_of(g) + (size_t)i * SCENE_BYTES;
        if (s[0] == map && s[1] == index) return i;
    }
    return -1;
}

int dgx_ge_graph_side(const dgx_ge_graph_t *g, int scene, int side)
{
    if (scene < 0 || scene >= g->scenes || side < 0 || side > DGX_GE_SIDE_DOWN) return -1;
    const uint16_t to = u16_at(scenes_of(g) + (size_t)scene * SCENE_BYTES + 4 + 2 * side);
    return to == DGX_GE_NO_SCENE || to >= g->scenes ? -1 : to;
}

/* An exit from a scene or a map by name; NULL none. */
static const uint8_t *find_exit(const dgx_ge_graph_t *g, int from, bool of_map, int name)
{
    for (int i = 0; i < g->exits; ++i) {
        const uint8_t *e = exits_of(g) + (size_t)i * EXIT_BYTES;
        if (u16_at(e) == from && (bool)(e[7] & OF_MAP) == of_map && e[4] == name) return e;
    }
    return NULL;
}

bool dgx_ge_graph_exit(const dgx_ge_graph_t *g, int scene, int name, dgx_ge_go_t *go)
{
    int map;
    if (!dgx_ge_graph_scene(g, scene, &map, NULL, NULL)) return false;
    const uint8_t *e = find_exit(g, scene, false, name);
    if (!e) e = find_exit(g, map, true, name);
    if (!e || u16_at(e + 2) >= g->scenes) return false;
    *go = (dgx_ge_go_t){.scene = u16_at(e + 2), .entry = e[5], .transition = e[6]};
    return true;
}

bool dgx_ge_graph_entry(const dgx_ge_graph_t *g, int scene, int name, dgx_ge_entry_t *entry)
{
    for (int i = 0; i < g->entries; ++i) {
        const uint8_t *e = entries_of(g) + (size_t)i * ENTRY_BYTES;
        if (u16_at(e) != scene || e[6] != name) continue;
        *entry = (dgx_ge_entry_t){.x = (int16_t)u16_at(e + 2), .y = (int16_t)u16_at(e + 4), .left = e[7] != 0, .arrive = e[8], .fires = e[9]};
        return true;
    }
    return false;
}

bool dgx_ge_graph_map(const dgx_ge_graph_t *g, int map, int *data, int *rules)
{
    if (map < 0 || map >= g->maps) return false;
    const uint8_t *m = maps_of(g) + (size_t)map * MAP_BYTES;
    if (data) *data = u16_at(m) == 0xFFFF ? -1 : u16_at(m);
    if (rules) *rules = u16_at(m + 2) == 0xFFFF ? -1 : u16_at(m + 2);
    return true;
}

int dgx_ge_graph_map_number(const dgx_ge_graph_t *g, int map)
{
    if (map < 0 || map >= g->maps) return 0;
    return u16_at(maps_of(g) + (size_t)map * MAP_BYTES + 4);
}

/* What the rules say: a signal is an exit of the map; the rest is the game's. */
typedef struct {
    int                exit;
    dgx_ge_anim_say_fn say;
    void              *ctx;
} heard_t;

static void heard(dgx_ge_actor_t *a, dgx_ge_anim_say_t what, uint8_t value, uint32_t at_ms, void *ctx)
{
    heard_t *h = ctx;
    if (what == DGX_GE_ANIM_SIGNAL) h->exit = value;
    else if (h->say) h->say(a, what, value, at_ms, h->ctx);
}

int dgx_ge_graph_rule(dgx_ge_actor_t *rules, int event, uint32_t now_ms, dgx_ge_anim_say_fn say, void *ctx)
{
    heard_t h = {-1, say, ctx};
    if (!rules->anim) return -1;
    if (event >= 0) dgx_ge_actor_event(rules, event, now_ms, heard, &h);
    dgx_ge_actor_advance(rules, now_ms, heard, &h);
    return h.exit;
}
