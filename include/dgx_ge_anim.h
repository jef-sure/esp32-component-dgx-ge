#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dgx_ge_input.h"

/*
 * Animation: scripts and the rules of passing from one to another.
 *
 * A script is a little program of frames, written as text and put into the
 * pack by tools/dgx_ge_anim.py (see there for the language). A frame is a
 * picture shown for a time; between frames the script moves its actor, turns
 * it round, sets its speed, tells the game something. Time is real time: an
 * actor is moved on to a moment, and every step whose time has come is made,
 * so a slow frame rate shows fewer pictures but the same way travelled.
 *
 * Scripts are entry points; labels inside them are only places to go to. The
 * script an actor is in is the one it was last started at or sent to by name.
 *
 * Passing between scripts is a matrix: for a script and an event, the script
 * to go to and when: at once, at the next branch point of the script, or at
 * its end. An empty cell: the event means nothing there. Nothing else decides;
 * conditions are the game's, checked before it sends an event.
 *
 * Params: numbers every actor keeps for itself (lives, what it is doing),
 * declared with the scripts, with the values they start with; scripts set and
 * change them, the game reads them by the numbers the pack gives. What they
 * mean is the game's.
 *
 * The blob ("DGAN"), little-endian, read where it lies:
 *
 *   header   20 bytes: "DGAN", version, scripts, events, params, size of code;
 *            pockets u8 (places a character has), stack u8 (how many of the kind
 *            go into one place, 0 as 1), two bytes to spare
 *   scripts  2 bytes each: where in the code the script begins
 *   params   2 bytes each: the value it starts with
 *   matrix   scripts * events cells of 2 bytes: target script + 1 (0 empty), when
 *   code     the steps, see dgx_ge_anim.c
 */
#define DGX_GE_ANIM_MAGIC   0x4E414744u /* "DGAN" */
#define DGX_GE_ANIM_VERSION 10u

/* Params an actor has room for, 2 bytes each; a blob that declares more does not open. */
/* How many points an actor holds things at (carry): a sword and a shield. */
#ifndef DGX_GE_ANIM_CARRIED
#define DGX_GE_ANIM_CARRIED 2
#endif

/* How many masks an actor may watch for touching at once (contact <mask> <side> ...), 6 bytes each. */
#ifndef DGX_GE_ANIM_CONTACTS
#define DGX_GE_ANIM_CONTACTS 2
#endif

#ifndef DGX_GE_ANIM_PARAMS
#define DGX_GE_ANIM_PARAMS 4
#endif

typedef enum {
    DGX_GE_WHEN_NOW    = 0,
    DGX_GE_WHEN_BRANCH = 1, /* at the next branch point, or at the end */
    DGX_GE_WHEN_END    = 2, /* where the script would go on to another or hold */
} dgx_ge_when_t;

typedef struct {
    const uint8_t *base;
    uint16_t       scripts, events, params;
    uint16_t       id;      /* the kind it is: its blob in the pack (dgx_ge_kind), 0xFFFF not known */
    uint8_t        pockets; /* places for things of a character, 0 none */
    uint8_t        stack;   /* how many of a thing of this kind go into one place */
    uint32_t       code_size;
    const uint8_t *table, *initial, *matrix, *code;
    const uint8_t *tables;      /* its constant tables, after the code (table name 1 2 3): NULL none */
    uint32_t       tables_size;
} dgx_ge_anim_t;

/* Take the blob of scripts that lies at data; false when it is not one. Its kind is not known (id 0xFFFF): dgx_ge_kind knows it. */
bool dgx_ge_anim_open(dgx_ge_anim_t *anim, const void *data, size_t size);

typedef struct dgx_ge_terrain dgx_ge_terrain_t;
struct dgx_ge_sense;  /* dgx_ge_sense_t, below */
struct dgx_ge_sensed; /* dgx_ge_sensed_t */
struct dgx_ge_pockets;

/* What an actor is: where it is, what it shows, what its script has set. */
typedef struct dgx_ge_actor {
    const dgx_ge_anim_t *anim;
    uint16_t             pc;     /* the next step in the code */
    uint8_t              script; /* the row of the matrix */
    uint8_t              pending; /* a transition waiting for a branch point or the end: target + 1, 0 none */
    uint8_t              pending_when;
    bool                 holding; /* the script has stopped on its last frame */
    bool                 keep;    /* its script says the world keeps it so (keep): newer things do not take its place */
    uint8_t              amount;  /* how many it is: a stack lying (1 as it starts); a frame may show so many (amount) */
    uint32_t             due_ms;  /* when the next step is made */
    uint32_t             moved_ms; /* the motion is counted up to here */
    /* set by the script */
    int16_t              image;    /* of the pack, -1 none yet */
    int8_t               image_dx; /* where the picture stands from the actor, forward */
    int8_t               image_dy;
    bool                 flip;   /* facing the other way than its pictures */
    uint16_t             masks;  /* the masks of collisions that are active */
    uint16_t             hits;   /* where what they do when they meet is in the code + 1 (hit), 0 nothing */
    /*
     * What is held, by points: the frame to show it by (carry: the hand as it
     * holds it now; -1: the thing's own), and the thing itself when it is an
     * actor of its own (a sword, a pot: dgx_ge_actor_hold), else NULL and the
     * frame is all there is.
     */
    int16_t              carried[DGX_GE_ANIM_CARRIED];
    uint8_t              carried_at[DGX_GE_ANIM_CARRIED]; /* at which point (its number in the pack) */
    struct dgx_ge_actor *items[DGX_GE_ANIM_CARRIED];
    struct dgx_ge_actor *holder;  /* a thing in a hand: whose, NULL free */
    uint8_t              held_at; /* at which of the holder's points */
    int32_t              x, y;   /* in 1/256 of a pixel of the scene */
    int32_t              vx, vy; /* 1/256 of a pixel a second */
    int32_t              ax, ay; /* 1/256 of a pixel a second a second */
    /* a path: the place a formula of the time gives, from where and when it began */
    uint16_t             path;   /* where its step is in the code + 1, 0 none */
    int8_t               path_sign; /* forward when it began: 1 or -1 */
    uint32_t             path_ms;
    int32_t              path_x, path_y;
    int16_t              params[DGX_GE_ANIM_PARAMS]; /* by the numbers the pack gives */
    /*
     * Where it entered the script it is in: when, where, which way it faced,
     * its params then. From there the script makes it what it is now, as far
     * as the script moves it (an object of the world is kept so: dgx_ge_world.h).
     */
    uint32_t             entered_ms;
    int32_t              entered_x, entered_y;
    bool                 entered_flip;
    int16_t              entered_params[DGX_GE_ANIM_PARAMS];
    /* what was let go last (release): the picture, -1 none, and its velocity, points a second (x as the scene goes) */
    struct {
        int16_t image;
        int16_t vx, vy;
    } released;
    /* what was fired last (shoot): the kind (the game's number of it), its velocity, points a second (x as the scene goes) */
    struct {
        uint16_t kind;
        int16_t  vx, vy;
    } shot;
    /* touching the terrain: set by the game after the start; looked for while the script says contact on */
    const dgx_ge_terrain_t *terrain;
    bool                 contact;  /* the script looks for contacts */
    uint8_t              contact_event; /* sent at the moment of a contact going in (contact on <event>), 0xFF none */
    bool                 touching; /* it touched at the last look: a contact is only going in */
    uint32_t             looked_ms; /* when it last looked */
    /* masks watched for touching (contact <mask> <side> on/off): what, on which sides, how near; whether it touched at the last look */
    struct {
        uint8_t mask;  /* 0xFF: nothing watched here */
        uint8_t from;  /* DGX_GE_TOUCH_* bits; 32 front, 64 back, by the way it faces */
        uint8_t reach; /* points */
        uint8_t on, off; /* the events, 0xFF none */
        uint8_t was;     /* the sides it touched on at the last look; 0xFF not looked yet (no terrain then) */
        uint8_t first;   /* begun with nothing within so many points: lost at once (at its first look with a terrain); 0 none */
    } contacts[DGX_GE_ANIM_CONTACTS];
    struct dgx_ge_pockets *pockets; /* what it has besides its hands (dgx_ge_pockets.h), NULL none; set by the game after the start */
} dgx_ge_actor_t;

/*
 * Contacts with the terrain: what is solid in the scene, floors, walls,
 * bumpers. The engine looks, at the bounds of the steps of the script and at
 * now, whether the actor touches something solid; it does not know the
 * terrain, the game tells it (or a module of tiles, later). Out at one look and in at the next: a
 * contact somewhere between. Between two looks no step is made: the actor
 * moves on as it moved, and dgx_ge_actor_state_at() gives it at any moment.
 * The game decides what follows, and may put the actor where and when it
 * touched exactly (dgx_ge_actor_restart()); the engine goes on from there.
 * A wall thinner than the way an actor goes between two looks is not seen:
 * that is for the game to avoid. A contact is the going in: an actor at rest
 * on a floor, or lying in it, is not told of again. However many contacts
 * lie on the way to now, each is dealt with at its moment; only a handler
 * that puts the actor back exactly as it was (the same contact for ever) is
 * stopped: the actor rests where it touched, and the log says so once.
 *
 * Contacts by masks (contact <mask> <side> on <event> off <event>): a mask
 * of the actor watched for touching anything solid on a side (the terrain
 * answers, sides); the moment it touches, or lets go, is found between two
 * looks by halving, the actor put there and told the event then. A floor
 * lost under the feet, a wall met, a ceiling: each at its moment, however
 * the calls come.
 */
typedef struct {
    uint32_t from_ms, to_ms; /* out at from, in at to */
    int32_t  x, y;           /* where it touches, 1/256 of a point of the scene, as the terrain tells */
    float    nx, ny;         /* the normal of the surface there: a floor (0, -1), a wall on the left (1, 0) */
    int      surface;        /* what is touched, a number of the game: a kind of tile, a material */
} dgx_ge_contact_t;

struct dgx_ge_terrain {
    /*
     * Does the actor, put at x, y (1/256 of a point), touch anything solid? If
     * so, say where, the normal and what. actor NULL: a point, for a ray.
     */
    bool (*touches)(const dgx_ge_actor_t *actor, int32_t x, int32_t y, dgx_ge_contact_t *contact, void *ctx);
    /* A ray of its own, exact (a grid of tiles walks it cell by cell); NULL: dgx_ge_terrain_ray() walks it by touches. */
    bool (*ray)(int32_t x0, int32_t y0, int32_t x1, int32_t y1, dgx_ge_contact_t *hit, void *ctx);
    /* A contact: what follows is the game's: bounce, stop, an event, nothing. */
    void (*contact)(dgx_ge_actor_t *actor, const dgx_ge_contact_t *contact, void *ctx);
    /*
     * The sides (DGX_GE_TOUCH_* bits of dgx_ge_mask.h) a mask of the actor, put
     * at x, y (1/256 of a point), touches anything solid on, within reach
     * points: a floor below its feet, a wall at its side, water around it. A
     * grid of tiles and the registry of bodies answer it (dgx_ge_grid_touch_sides,
     * dgx_ge_collisions_touch_sides); the game puts them together. NULL: masks are
     * not watched (contact <mask> ... in a script does nothing).
     */
    uint8_t (*sides)(const dgx_ge_actor_t *actor, int32_t x, int32_t y, int mask, int reach, void *ctx);
    /*
     * What the actor perceives (sense in a script): the nearest one the query
     * asks for, as found; false when there is none. The registry of bodies
     * and a grid of tiles answer it (dgx_ge_sense_bodies, dgx_ge_sense_cells);
     * the game puts them together, and gives a cell its number. NULL: sense
     * never finds anything.
     */
    bool (*sense)(const dgx_ge_actor_t *actor, const struct dgx_ge_sense *query, struct dgx_ge_sensed *found, void *ctx);
    /*
     * What a look from x0, y0 to x1, y1 (points of the scene) costs, as what lies between veils it (sense ... seen):
     * points, -1 when it costs more than radius or nothing is seen through. A grid answers it (dgx_ge_grid_sight).
     * NULL: the plain distance, nothing veils.
     */
    int (*sight)(int x0, int y0, int x1, int y1, int radius, void *ctx);
    void *ctx;
};

/* What a sense looks for. */
typedef struct dgx_ge_sense {
    uint16_t       kind;   /* bodies: actors of this kind (a blob of the pack, dgx_ge_kind), 0xFFFF any */
    bool           cells;  /* cells of a grid (not empty), not bodies */
    int            x, y;   /* where the one looking is, points of the scene */
    int            radius; /* points */
    const uint8_t *where;  /* an expression of the one looking that the one found must make not 0 (its.); NULL none */
    bool           seen;   /* only one the look reaches within radius, as the terrain's sight says (sense ... seen) */
} dgx_ge_sense_t;

/* What a sense found: from the one looking to it, points of the scene (x as the scene goes, not forward). */
typedef struct dgx_ge_sensed {
    int                        dx, dy;
    const struct dgx_ge_actor *actor;           /* a body: the actor it is */
    int                        col, row, tile, mod; /* a cell: where in the window, its tile, its modifier */
    int                        cell;            /* a cell: its number of the game (tell cell), the game's to give; row * cols + col until then */
} dgx_ge_sensed_t;

/*
 * A sense answered by the registry of bodies: the nearest actor of the kind
 * asked (its animation opened by dgx_ge_kind, so that its kind is known),
 * not of owner (the one looking, and what it holds), that makes where not 0.
 * Bodies that are no actors (dgx_ge_collisions_actor) are not seen.
 */
/* Does where of the one looking hold of one found (its. reads it: a cell when cells)? NULL where: yes. For the answers to a sense. */
bool dgx_ge_actor_sees(const dgx_ge_actor_t *looking, const uint8_t *where, const dgx_ge_sensed_t *found, bool cells);

struct dgx_ge_collisions;
bool dgx_ge_sense_bodies(const struct dgx_ge_collisions *c, uint16_t owner, const dgx_ge_actor_t *actor, const dgx_ge_sense_t *query,
                         dgx_ge_sensed_t *found);

/*
 * Is there anything solid on the way from x0, y0 to x1, y1 (1/256 of a
 * point)? A guard looking at the kid, a shot, a fast stone checking its way.
 * By the ray of the terrain if it has one, else walked by steps of so many
 * points and found to a point between the last step clear and the first not.
 * hit: where it is, the normal, the surface. A wall thinner than a step is
 * not seen.
 */
bool dgx_ge_terrain_ray(const dgx_ge_terrain_t *terrain, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int step, dgx_ge_contact_t *hit);

/* Where the actor is and how fast it goes at a moment between its last two looks (or any moment of its present motion). */
void dgx_ge_actor_state_at(const dgx_ge_actor_t *actor, uint32_t ms, int32_t *x, int32_t *y, int32_t *vx, int32_t *vy);

/*
 * The contact found more exactly, by halving its time to a millisecond:
 * from_ms becomes the last moment out, to_ms the first in, and the place,
 * normal and surface are those the terrain tells at to_ms. Where the actor was
 * at from_ms gives dgx_ge_actor_state_at().
 */
void dgx_ge_contact_refine(const dgx_ge_actor_t *actor, dgx_ge_contact_t *contact);

/* From at_ms on the actor is at x, y with this velocity (1/256 of a point, a second); its acceleration stays, a path ends. */
void dgx_ge_actor_restart(dgx_ge_actor_t *actor, uint32_t at_ms, int32_t x, int32_t y, int32_t vx, int32_t vy);

/*
 * What a mask of an actor does when it meets a mask of another (as
 * dgx_ge_collisions_look tells), as its script says (hit): the damage the
 * game takes off, an event for the one met and one for this actor itself
 * (-1 none). The game sends them, so the matrix of each decides what follows:
 * a blow on a guard that blocks, on one lying down. False: nothing.
 */
typedef struct {
    uint8_t  mask;
    uint16_t on; /* the masks of the other it does this on, a bit each */
    int16_t  damage;
    int16_t  event, recoil;
} dgx_ge_hit_t;

bool dgx_ge_actor_hit(const dgx_ge_actor_t *actor, int mask, int on_mask, dgx_ge_hit_t *hit);

/*
 * What the actor holds at a point (carry in its script): a picture of the
 * pack, a frame of what is in the hand as the hand moves; -1 nothing. Where it
 * is held is the point of the picture the actor shows (dgx_ge_item_held).
 */
int dgx_ge_actor_carried(const dgx_ge_actor_t *actor, int point);

/*
 * The frame a point shows what it holds by, or -1 (the thing's own frame), as
 * carry in a script does. False when every slot holds something else.
 */
bool dgx_ge_actor_carry(dgx_ge_actor_t *actor, int point, int picture);

/*
 * Things in hands. A thing is an actor of its own, with its own scripts: what
 * it is (masks, hits, what becomes of it) is its kind's. The holder holds it
 * at a point of its frames, how is the holder's (carry: the frame to show it
 * by); the game puts the thing where that point is every frame. The holder's
 * script lets it go (release): it is free, at the velocity and the moment of
 * the step. False when every slot holds something else.
 */
bool dgx_ge_actor_hold(dgx_ge_actor_t *holder, int point, dgx_ge_actor_t *thing);

/* What a holder holds at a point, NULL nothing (or a frame alone). */
dgx_ge_actor_t *dgx_ge_actor_held(const dgx_ge_actor_t *holder, int point);

/* A thing out of its holder's hand where it is, not thrown (the holder fell, died). */
void dgx_ge_actor_drop(dgx_ge_actor_t *thing);

/* The picture a thing shows: the frame its holder holds it by, else its own. */
int dgx_ge_held_image(const dgx_ge_actor_t *thing);

/* The masks of a thing that are active: its own; in a hand, only those the holder's move has active too. */
uint16_t dgx_ge_held_masks(const dgx_ge_actor_t *thing);

/* The origin of the scene moved by dx, dy points (dgx_ge_scene_shift): the actor and a path it is on go with it, exactly. */
void dgx_ge_actor_shift(dgx_ge_actor_t *actor, int dx, int dy);

typedef enum {
    DGX_GE_ANIM_SIGNAL = 0, /* something for the game: a step touched the floor, the hand let go */
    DGX_GE_ANIM_SOUND  = 1,
    DGX_GE_ANIM_RELEASE = 2, /* let go what was held at a point (the value): actor->released; a thing of its own is free */
    DGX_GE_ANIM_PARAM   = 3, /* a param (the value: its number) has changed by the script (set, add): drunk, the world turns another color */
    DGX_GE_ANIM_TELL    = 4, /* an event (the value) for the one holding this actor (tell holder): the game sends it on */
    DGX_GE_ANIM_SHOOT   = 5, /* a shot fired from a point (the value): actor->shot; the game fires it (dgx_ge_shots_fire) */
    /* a timer of the pack (the value: its name) started, stopped, held, let go, its owner the one saying it (dgx_ge_timers_said) */
    DGX_GE_ANIM_TIMER_START = 6,
    DGX_GE_ANIM_TIMER_STOP  = 7,
    DGX_GE_ANIM_TIMER_HOLD  = 8,
    DGX_GE_ANIM_TIMER_GO    = 9,
    /*
     * The world of the map, the game's to do: a cell (dgx_ge_anim_said(0), a cell of the map as dgx_ge_cells counts
     * them) told an event (the value), its own param given dgx_ge_anim_said(1) (tell cell); a button in a cell
     * pressed as if trodden on (press); what lies in a cell gone (clear)
     */
    DGX_GE_ANIM_CELL  = 10,
    DGX_GE_ANIM_PRESS = 11,
    DGX_GE_ANIM_CLEAR = 12,
    /*
     * an actor of a kind comes where the player is (appear): the value its event, dgx_ge_anim_said 0, 1 where (x, y),
     * 2 its kind (a blob of the pack), 3 facing left; where it lives is the game's
     */
    DGX_GE_ANIM_APPEAR = 13,
    DGX_GE_ANIM_VANISH = 14, /* the actor is gone from the game (vanish): the game takes it away */
} dgx_ge_anim_say_t;

/*
 * The numbers of the step being said (tell cell: 0 the cell, 1 the value; press, clear: 0 the cell; appear: x, y,
 * kind, left), worked out as it is made; good only in the say of it.
 */
int32_t dgx_ge_anim_said(int i);

/*
 * appear ... with <param> = <expression>, ...: in the say of that appear
 * only, the params given to the one appearing, worked out by the one that
 * made it appear at the moment of its step, written into child (its params
 * and those it entered its script with). The game calls it after it has
 * started the child and before the child hears its event. How many were
 * written; 0 outside such a say.
 */
int dgx_ge_anim_said_params(dgx_ge_actor_t *child);

/*
 * What a script tells the game, as its step is made. at_ms is the moment of
 * the step: when the actor is moved on over a long time, the steps passed are
 * made at once, and a signal may come later than its moment. A signal that
 * changes the state of the game still counts; a sound long past may be left
 * unplayed.
 */
typedef void (*dgx_ge_anim_say_fn)(dgx_ge_actor_t *actor, dgx_ge_anim_say_t what, uint8_t value, uint32_t at_ms, void *ctx);

/*
 * The dice of the game: rnd(n) of an expression casts the next of their
 * numbers, 0..n (a step of 214013 x + 2531011 on this seed, the high half of
 * it). The game points it at a seed of its own; NULL: rnd is 0.
 */
extern uint32_t *dgx_ge_dice;

/*
 * game.<name> of an expression: a question to the game, by its number (the
 * names an animation declares with game, in their order), answered by the
 * game's function with its context, at the moment of the step; NULL: 0.
 */
typedef float (*dgx_ge_ask_fn)(int what, void *ctx);
extern dgx_ge_ask_fn dgx_ge_expr_ask;
extern void         *dgx_ge_expr_ask_ctx;

/*
 * axis(lx) of an expression: an axis of the pad of this input (dgx_ge_input.h), -1..1, a trigger 0..1, as it is at
 * the moment of the step; the game gives the input of its player once (NULL: every axis 0).
 */
extern const dgx_ge_input_t *dgx_ge_expr_input;

/* Put an actor at x, y (pixels) into a script, from now on, its params as they start. */
void dgx_ge_actor_start(dgx_ge_actor_t *actor, const dgx_ge_anim_t *anim, int script, int x, int y, uint32_t now_ms);

/*
 * An event: the matrix says what follows. Returns false when it means
 * nothing in the script the actor is in; a transition at a branch point or at
 * the end waits for it, and a later event takes the place of one waiting.
 */
bool dgx_ge_actor_event(dgx_ge_actor_t *actor, int event, uint32_t now_ms, dgx_ge_anim_say_fn say, void *ctx);

/*
 * The actor as it is at now: what it shows, where it is, how fast it moves.
 * It is worked out through every step from the last time on, however many,
 * for their moves add up and their branch points may change the script; time
 * is never lost. Give it the clock of the game, which stands while the game
 * is paused.
 */
void dgx_ge_actor_advance(dgx_ge_actor_t *actor, uint32_t now_ms, dgx_ge_anim_say_fn say, void *ctx);

/* The value of an expression of the blobs (dgx_ge_anim.py, Expr) for an actor: its params, its pockets (count), now its last step. */
float dgx_ge_actor_eval(const dgx_ge_actor_t *actor, const uint8_t *expr);

/* Where the actor is, in pixels. */
static inline int dgx_ge_actor_x(const dgx_ge_actor_t *actor)
{
    return actor->x >> 8;
}

static inline int dgx_ge_actor_y(const dgx_ge_actor_t *actor)
{
    return actor->y >> 8;
}

#ifdef __cplusplus
}
#endif
