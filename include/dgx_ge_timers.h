#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Timers: sources of events by time. A timer is once (after a time),
 * every so often (until it is stopped), or every so often until a time. It
 * counts milliseconds from its start (an expression reads them by its name:
 * timer(clock)). It belongs to whoever started it (the game, a map, a scene,
 * an actor, a thing: the game's numbers), and is told what makes it go: its
 * reset — never (a grenade goes off whether one is there or not), on leaving
 * the scene it was started in (an alarm: one goes out, it stops), on leaving
 * its map. When it goes off, the game is called with it and the moment it
 * went off (not the moment the game asked: an event of it comes in time,
 * however late the call); the game sends its event to its owner's script, or
 * does what its own code says. Times are those of the clock of the game:
 * paused, they stand.
 *
 * A timer may be held: its time stands (the clock of PoP while the prince is
 * dead), and goes on from where it stood. One may be set as if it had gone
 * so long (from a saved game): what it would have done meanwhile is not done.
 *
 * As many timers as the game's constants say (dgx_ge_game.h); a record 32
 * bytes.
 *
 * What a timer is, by its name, the pack says (tools: pack.add_timer): the
 * blob "TIMERS", little-endian: "DGTM", version u16, count u16; 12 bytes a
 * timer, by the numbers of their names: kind u8, reset u8, event u8 (0xFF
 * none), flags u8 (bit 0: going from the start of the game, its owner the
 * game), period u32 ms, limit u32 ms.
 */
#define DGX_GE_TIMERS_MAGIC 0x4D544744u /* "DGTM" */
enum { DGX_GE_TIMER_ONCE, DGX_GE_TIMER_EVERY, DGX_GE_TIMER_UNTIL };
enum { DGX_GE_RESET_NEVER, DGX_GE_RESET_SCENE, DGX_GE_RESET_MAP };
enum { DGX_GE_OWNER_GAME, DGX_GE_OWNER_MAP, DGX_GE_OWNER_SCENE, DGX_GE_OWNER_ACTOR, DGX_GE_OWNER_THING, DGX_GE_OWNER_CELL };

typedef struct dgx_ge_timer {
    uint8_t  name;       /* the pack's number of its name */
    uint8_t  kind;       /* DGX_GE_TIMER_* */
    uint8_t  reset;      /* DGX_GE_RESET_* */
    uint8_t  event;      /* the event it sends its owner, 0xFF none (a clock) */
    uint8_t  owner_kind; /* DGX_GE_OWNER_* */
    uint8_t  map;        /* where it was started: for a reset */
    uint16_t owner;      /* the game's number of its owner */
    uint16_t scene;
    uint8_t  held;       /* its time stands since held_ms */
    uint32_t period_ms;  /* once: after so long; every: so often */
    uint32_t limit_ms;   /* until: no longer than so long from its start */
    uint32_t start_ms, next_ms, held_ms;
} dgx_ge_timer_t;

/* A timer gone off at a moment: its event to its owner, or the game's own doing. */
typedef void (*dgx_ge_timer_fn)(const dgx_ge_timer_t *timer, uint32_t at_ms, void *ctx);

typedef struct dgx_ge_timers {
    dgx_ge_timer_t *timers;
    int             count, capacity;
    const uint8_t  *defs;   /* the timers of the pack, by name (12 bytes each), NULL none */
    int             def_count;
    dgx_ge_timer_fn fire;
    void           *ctx;
} dgx_ge_timers_t;

/* Room for so many timers, gone off into fire; false when there is no memory. */
bool dgx_ge_timers_init(dgx_ge_timers_t *timers, int capacity, dgx_ge_timer_fn fire, void *ctx);
void dgx_ge_timers_free(dgx_ge_timers_t *timers);

/*
 * A timer started at a moment (its name, kind, period, limit, reset, event,
 * owner and where; start_ms and next_ms are set): one of the same name and
 * owner starts anew. False when there is no room (the game's constants are
 * wrong: told in the log) or its period is 0 where it goes again.
 */
bool dgx_ge_timers_start(dgx_ge_timers_t *timers, const dgx_ge_timer_t *timer, uint32_t now_ms);

/* The timers of the pack (its blob "TIMERS"); false when it is not one, or names more than 255 (the tools' most: a timer's name is a byte). */
bool dgx_ge_timers_define(dgx_ge_timers_t *timers, const void *blob, size_t size);

/* A timer of the pack by its name, started at a moment for an owner, in a map and a scene; false as dgx_ge_timers_start, or no such name. */
bool dgx_ge_timers_start_named(dgx_ge_timers_t *timers, int name, int owner_kind, int owner, int map, int scene, uint32_t now_ms);

/* The timers of the pack going from the start of the game, started at a moment; how many. */
int dgx_ge_timers_start_game(dgx_ge_timers_t *timers, uint32_t now_ms);

/* The timer of a name and owner stopped; false when there was none. */
bool dgx_ge_timers_stop(dgx_ge_timers_t *timers, int name, int owner_kind, int owner);

/* The time of the timer of a name and owner held (it stands) or let go (on from where it stood) at a moment; false none such. */
bool dgx_ge_timers_hold(dgx_ge_timers_t *timers, int name, int owner_kind, int owner, bool hold, uint32_t now_ms);

/*
 * The timer of a name and owner as if it had gone so long by now (a saved
 * game): it goes off next when it would have; once, past its time: at once.
 * One every-until past its limit is gone. False none such.
 */
bool dgx_ge_timers_set_elapsed(dgx_ge_timers_t *timers, int name, int owner_kind, int owner, uint32_t elapsed_ms, uint32_t now_ms);

/*
 * What a script said of a timer (dgx_ge_anim.h: DGX_GE_ANIM_TIMER_*, the
 * value its name) done for its owner, where it is, at the moment of its step;
 * false when it was not of a timer, or there was none such to stop, hold, let go.
 */
bool dgx_ge_timers_said(dgx_ge_timers_t *timers, int what, int name, int owner_kind, int owner, int map, int scene, uint32_t at_ms);

/* Every timer that goes as one leaves a scene (or a map: scene -1) stopped there. */
void dgx_ge_timers_leave(dgx_ge_timers_t *timers, int map, int scene);

/*
 * Milliseconds from the start of the first timer of a name to a moment (a
 * step of a script reads it at its own moment, not at the call); -1 none such.
 */
int32_t dgx_ge_timers_elapsed(const dgx_ge_timers_t *timers, int name, uint32_t at_ms);

/* On to now: every timer that goes off on the way, in the order of their moments, each at its own. */
void dgx_ge_timers_play(dgx_ge_timers_t *timers, uint32_t now_ms);

/*
 * Saved (a chunk TIMR: the timers as they are) and loaded, their times moved on by later_ms. Refused, nothing
 * changed: more than fit, a kind, reset or owner of none of theirs, one that comes again with no period. The check
 * alone reads and checks, changing nothing (not even l).
 */
struct dgx_ge_save;
struct dgx_ge_load;
void dgx_ge_timers_save(const dgx_ge_timers_t *timers, struct dgx_ge_save *s);
bool dgx_ge_timers_load(dgx_ge_timers_t *timers, struct dgx_ge_load *l, uint32_t later_ms);
bool dgx_ge_timers_load_check(const dgx_ge_timers_t *timers, const struct dgx_ge_load *l);

/* The timers an expression reads (timer(name)): the game points it at its own; NULL: timer() is -1. */
extern const dgx_ge_timers_t *dgx_ge_timers_seen;

#ifdef __cplusplus
}
#endif
