#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#include "dgx_ge_anim.h"
#include "dgx_ge_timers.h"
#include "dgx_ge_save.h"

const dgx_ge_timers_t *dgx_ge_timers_seen;

bool dgx_ge_timers_init(dgx_ge_timers_t *t, int capacity, dgx_ge_timer_fn fire, void *ctx)
{
    memset(t, 0, sizeof(*t));
    t->timers = capacity > 0 ? calloc((size_t)capacity, sizeof(dgx_ge_timer_t)) : NULL;
    if (!t->timers) return false;
    t->capacity = capacity;
    t->fire     = fire;
    t->ctx      = ctx;
    return true;
}

void dgx_ge_timers_free(dgx_ge_timers_t *t)
{
    if (dgx_ge_timers_seen == t) dgx_ge_timers_seen = NULL;
    free(t->timers);
    memset(t, 0, sizeof(*t));
}

static int find(const dgx_ge_timers_t *t, int name, int owner_kind, int owner)
{
    for (int i = 0; i < t->count; ++i)
        if (t->timers[i].name == name && t->timers[i].owner_kind == owner_kind && t->timers[i].owner == owner) return i;
    return -1;
}

static void forget(dgx_ge_timers_t *t, int i)
{
    memmove(&t->timers[i], &t->timers[i + 1], (size_t)(t->count - i - 1) * sizeof(dgx_ge_timer_t));
    --t->count;
}

bool dgx_ge_timers_start(dgx_ge_timers_t *t, const dgx_ge_timer_t *timer, uint32_t now_ms)
{
    if (timer->kind != DGX_GE_TIMER_ONCE && timer->period_ms == 0) return false;
    int i = find(t, timer->name, timer->owner_kind, timer->owner);
    if (i < 0) {
        if (t->count == t->capacity) {
            ESP_LOGE("dgx-ge", "timers: no room for timer %d, the game said %d timers", timer->name, t->capacity);
            return false;
        }
        i = t->count++;
    }
    t->timers[i]          = *timer;
    t->timers[i].start_ms = now_ms;
    t->timers[i].next_ms  = now_ms + timer->period_ms;
    t->timers[i].held     = 0;
    return true;
}

bool dgx_ge_timers_define(dgx_ge_timers_t *t, const void *blob, size_t size)
{
    const uint8_t *b = blob;
    t->defs          = NULL;
    t->def_count     = 0;
    uint32_t magic;
    if (!b || size < 8) return false;
    memcpy(&magic, b, 4);
    const int version = b[4] | b[5] << 8, count = b[6] | b[7] << 8;
    if (magic != DGX_GE_TIMERS_MAGIC || version != 1 || count > 255 || size < 8 + (size_t)count * 12) return false; /* 255 at most, as the tools make them */
    t->defs      = b + 8;
    t->def_count = count;
    return true;
}

static uint32_t u32_at(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

bool dgx_ge_timers_start_named(dgx_ge_timers_t *t, int name, int owner_kind, int owner, int map, int scene, uint32_t now_ms)
{
    if (name < 0 || name >= t->def_count) return false;
    const uint8_t *d  = t->defs + name * 12;
    dgx_ge_timer_t tm = {
        .name       = (uint8_t)name,
        .kind       = d[0],
        .reset      = d[1],
        .event      = d[2],
        .owner_kind = (uint8_t)owner_kind,
        .map        = (uint8_t)map,
        .owner      = (uint16_t)owner,
        .scene      = (uint16_t)scene,
        .period_ms  = u32_at(d + 4),
        .limit_ms   = u32_at(d + 8),
    };
    return dgx_ge_timers_start(t, &tm, now_ms);
}

int dgx_ge_timers_start_game(dgx_ge_timers_t *t, uint32_t now_ms)
{
    int started = 0;
    for (int i = 0; i < t->def_count; ++i)
        if (t->defs[i * 12 + 3] & 1) started += dgx_ge_timers_start_named(t, i, DGX_GE_OWNER_GAME, 0, 0, 0, now_ms);
    return started;
}

bool dgx_ge_timers_stop(dgx_ge_timers_t *t, int name, int owner_kind, int owner)
{
    const int i = find(t, name, owner_kind, owner);
    if (i < 0) return false;
    forget(t, i);
    return true;
}

bool dgx_ge_timers_hold(dgx_ge_timers_t *t, int name, int owner_kind, int owner, bool hold, uint32_t now_ms)
{
    const int i = find(t, name, owner_kind, owner);
    if (i < 0) return false;
    dgx_ge_timer_t *tm = &t->timers[i];
    if (hold && !tm->held) {
        tm->held = 1, tm->held_ms = now_ms;
    } else if (!hold && tm->held) {
        const uint32_t stood = now_ms - tm->held_ms;
        tm->held = 0, tm->start_ms += stood, tm->next_ms += stood;
    }
    return true;
}

bool dgx_ge_timers_set_elapsed(dgx_ge_timers_t *t, int name, int owner_kind, int owner, uint32_t elapsed_ms, uint32_t now_ms)
{
    const int i = find(t, name, owner_kind, owner);
    if (i < 0) return false;
    dgx_ge_timer_t *tm = &t->timers[i];
    tm->start_ms       = now_ms - elapsed_ms;
    if (tm->held) tm->held_ms = now_ms;
    if (tm->kind == DGX_GE_TIMER_ONCE) {
        tm->next_ms = elapsed_ms >= tm->period_ms ? now_ms : tm->start_ms + tm->period_ms;
        return true;
    }
    const uint32_t next = (elapsed_ms / tm->period_ms + 1) * tm->period_ms; /* from its start: the first moment after now */
    if (tm->kind == DGX_GE_TIMER_UNTIL && next > tm->limit_ms) {
        forget(t, i);
        return true;
    }
    tm->next_ms = tm->start_ms + next;
    return true;
}

bool dgx_ge_timers_said(dgx_ge_timers_t *t, int what, int name, int owner_kind, int owner, int map, int scene, uint32_t at_ms)
{
    if (!t) return false;
    switch (what) {
    case DGX_GE_ANIM_TIMER_START: return dgx_ge_timers_start_named(t, name, owner_kind, owner, map, scene, at_ms);
    case DGX_GE_ANIM_TIMER_STOP: return dgx_ge_timers_stop(t, name, owner_kind, owner);
    case DGX_GE_ANIM_TIMER_HOLD: return dgx_ge_timers_hold(t, name, owner_kind, owner, true, at_ms);
    case DGX_GE_ANIM_TIMER_GO: return dgx_ge_timers_hold(t, name, owner_kind, owner, false, at_ms);
    default: return false;
    }
}

void dgx_ge_timers_leave(dgx_ge_timers_t *t, int map, int scene)
{
    for (int i = t->count - 1; i >= 0; --i) {
        const dgx_ge_timer_t *tm = &t->timers[i];
        if ((tm->reset == DGX_GE_RESET_SCENE && scene >= 0 && tm->scene == scene) || (tm->reset == DGX_GE_RESET_MAP && tm->map == map)) forget(t, i);
    }
}

int32_t dgx_ge_timers_elapsed(const dgx_ge_timers_t *t, int name, uint32_t at_ms)
{
    for (int i = 0; t && i < t->count; ++i)
        if (t->timers[i].name == name) return (int32_t)((t->timers[i].held ? t->timers[i].held_ms : at_ms) - t->timers[i].start_ms);
    return -1;
}

void dgx_ge_timers_play(dgx_ge_timers_t *t, uint32_t now_ms)
{
    for (;;) {
        /* the one whose moment comes first, if it is not past now */
        int first = -1;
        for (int i = 0; i < t->count; ++i)
            if (!t->timers[i].held && (int32_t)(now_ms - t->timers[i].next_ms) >= 0 && (first < 0 || (int32_t)(t->timers[i].next_ms - t->timers[first].next_ms) < 0)) first = i;
        if (first < 0) break;
        dgx_ge_timer_t tm = t->timers[first];
        const uint32_t at = tm.next_ms;
        /* moved on (or gone) before the game hears it: what the game does may start or stop timers */
        const bool again = tm.kind == DGX_GE_TIMER_EVERY || (tm.kind == DGX_GE_TIMER_UNTIL && at + tm.period_ms - tm.start_ms <= tm.limit_ms);
        if (again) t->timers[first].next_ms = at + tm.period_ms;
        else forget(t, first);
        if (t->fire) t->fire(&tm, at, t->ctx);
    }
}

#define TIMER_BYTES (6 + 2 + 2 + 1 + 4 * 5)

void dgx_ge_timers_save(const dgx_ge_timers_t *t, dgx_ge_save_t *s)
{
    dgx_ge_save_chunk(s, "TIMR");
    dgx_ge_save_u16(s, (uint16_t)t->count);
    for (int i = 0; i < t->count; ++i) {
        const dgx_ge_timer_t *m = &t->timers[i];
        dgx_ge_save_u8(s, m->name), dgx_ge_save_u8(s, m->kind), dgx_ge_save_u8(s, m->reset), dgx_ge_save_u8(s, m->event);
        dgx_ge_save_u8(s, m->owner_kind), dgx_ge_save_u8(s, m->map);
        dgx_ge_save_u16(s, m->owner), dgx_ge_save_u16(s, m->scene);
        dgx_ge_save_u8(s, m->held);
        dgx_ge_save_u32(s, m->period_ms), dgx_ge_save_u32(s, m->limit_ms);
        dgx_ge_save_u32(s, m->start_ms), dgx_ge_save_u32(s, m->next_ms), dgx_ge_save_u32(s, m->held_ms);
    }
    dgx_ge_save_chunk_end(s);
}

/* The chunk read and checked; taken into the timers when into is given (they are t), else t is as it was. */
static bool load(const dgx_ge_timers_t *t, dgx_ge_timers_t *into, dgx_ge_load_t *l, uint32_t later_ms)
{
    if (!dgx_ge_load_chunk(l, "TIMR")) return false;
    const int count = dgx_ge_load_u16(l);
    if (l->failed || count > t->capacity || dgx_ge_load_left(l) < (size_t)count * TIMER_BYTES) return false;
    for (int i = 0; i < count; ++i) {
        dgx_ge_timer_t m;
        m.name = dgx_ge_load_u8(l), m.kind = dgx_ge_load_u8(l), m.reset = dgx_ge_load_u8(l), m.event = dgx_ge_load_u8(l);
        m.owner_kind = dgx_ge_load_u8(l), m.map = dgx_ge_load_u8(l);
        m.owner = dgx_ge_load_u16(l), m.scene = dgx_ge_load_u16(l);
        m.held      = dgx_ge_load_u8(l);
        m.period_ms = dgx_ge_load_u32(l), m.limit_ms = dgx_ge_load_u32(l);
        m.start_ms = dgx_ge_load_u32(l) + later_ms, m.next_ms = dgx_ge_load_u32(l) + later_ms, m.held_ms = dgx_ge_load_u32(l) + later_ms;
        /* of their kinds; one that comes again comes after a while (as dgx_ge_timers_start lets it), or it would go off for ever */
        if (m.kind > DGX_GE_TIMER_UNTIL || m.reset > DGX_GE_RESET_MAP || m.owner_kind > DGX_GE_OWNER_CELL || m.held > 1 ||
            (m.kind != DGX_GE_TIMER_ONCE && m.period_ms == 0))
            return false;
        if (into) into->timers[i] = m;
    }
    if (into) into->count = count;
    return true;
}

bool dgx_ge_timers_load_check(const dgx_ge_timers_t *t, const dgx_ge_load_t *l)
{
    dgx_ge_load_t at = *l;
    return load(t, NULL, &at, 0);
}

bool dgx_ge_timers_load(dgx_ge_timers_t *t, dgx_ge_load_t *l, uint32_t later_ms)
{
    return dgx_ge_timers_load_check(t, l) && load(t, t, l, later_ms);
}
