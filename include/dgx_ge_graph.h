#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dgx_ge_anim.h"

/*
 * The graph of scenes: where one goes from a scene. A scene is a node: a
 * room, a screen of a cut scene, a menu; it belongs to a map (a cloud of the
 * graph: the scenes that share data, a level). The edges are exits, one way,
 * named:
 * - by a side (left, right, up, down): the scene beside; one goes on as one
 *   was, a coordinate across the edge;
 * - by a name (a door, stairs, a hatch, a lift): to a scene, to an entry of
 *   it, by a transition (at once, fading out and in, wiped left to right). An exit of a scene, or
 *   of all of its map (the end of a level, a death).
 * An entry is a mark of its own, no exit's pair (a door one way: over a
 * slope, falling): a place, a facing, an event the one coming is told
 * (falls, steps out of a door), and links it fires as one comes
 * (dgx_ge_links.h: back at a checkpoint, a floor that is gone there).
 * A scene may have an event its map's rules are told when one comes into it
 * (a checkpoint passed), and one for each side, told when one leaves by it (a
 * passage: a sword gone behind him); a side with no scene beside but with an
 * event is an edge one may leave the map by (falling out of a level).
 *
 * A map has its rules: an animation (dgx_ge_anim.h) whose actor lives while
 * the game is in the map, its params (deaths, a checkpoint passed) and its
 * matrix: an event of the map (death, end) goes to its script, which says
 * which exit of the map is taken by a signal (its number: the exit's name).
 *
 * A map may have a number the game names it by (a level told as another, 0:
 * none).
 *
 * The engine answers where; the game asks, may go elsewhere (a random maze, a
 * secret), loads what a map needs, and moves the actors.
 *
 * The blob "DGSG", little-endian: "DGSG", version u16, counts of scenes u16,
 * exits u16, entries u16, maps u8, 0, the start: scene u16, entry u8, 0; maps
 * 6 bytes each: data (a blob of the pack, 0xFFFF none), rules (an animation,
 * 0xFFFF none), the number it is named by u16 (0 none); scenes 16 bytes each:
 * map u8, index u8 (its number in the map), kind u8, the event of coming into
 * it u8 (0xFF none), sides left, right, up, down (u16 each, 0xFFFF none), the
 * events of leaving by them (u8 each, 0xFF none);
 * exits 8 bytes each: from u16 (a scene, or a map), to u16 (a scene), name
 * u8, entry u8 (a name), transition u8, flags u8 (bit 0: of a map); entries
 * 10 bytes each: scene u16, x i16, y i16, name u8, facing u8 (0 right, 1
 * left), arrive u8 (an event, 0xFF none), the first of the links it fires
 * u8 (0xFF none).
 */
#define DGX_GE_GRAPH_MAGIC   0x47534744u /* "DGSG" */
#define DGX_GE_GRAPH_VERSION 3u
#define DGX_GE_NO_SCENE      0xFFFFu

enum { DGX_GE_SIDE_LEFT, DGX_GE_SIDE_RIGHT, DGX_GE_SIDE_UP, DGX_GE_SIDE_DOWN };
enum { DGX_GE_SCENE_ROOM, DGX_GE_SCENE_SHOW }; /* the kinds of scenes: of a map of data (a room), a show (its map's data: dgx_ge_show.h) */
/*
 * The transitions: at once; fading out and in; the new scene over the old one, left to right (dgx_ge_frame_wipe);
 * at once and as one is (the one going is not begun anew there: a level going on into the next)
 */
enum { DGX_GE_GO_CUT, DGX_GE_GO_FADE, DGX_GE_GO_WIPE, DGX_GE_GO_SEAMLESS };

typedef struct {
    const uint8_t *base;
    uint16_t       scenes, exits, entries;
    uint8_t        maps;
    uint16_t       start_scene;
    uint8_t        start_entry;
} dgx_ge_graph_t;

typedef struct {
    uint16_t scene;      /* where to */
    uint8_t  entry;      /* by which entry of it (a name) */
    uint8_t  transition; /* DGX_GE_GO_* */
} dgx_ge_go_t;

typedef struct {
    int16_t x, y;
    bool    left;   /* facing left */
    uint8_t arrive; /* the event the one coming is told, 0xFF none */
    uint8_t fires;  /* the first of the links it fires as one comes, 0xFF none */
} dgx_ge_entry_t;

/* Take the blob of a graph; false when it is not one. */
bool dgx_ge_graph_open(dgx_ge_graph_t *graph, const void *data, size_t size);

/* A scene: its map, its number in it, its kind; false when there is none such. */
bool dgx_ge_graph_scene(const dgx_ge_graph_t *graph, int scene, int *map, int *index, int *kind);

/* The event the rules of its map are told when one comes into a scene; -1 none. */
int dgx_ge_graph_scene_event(const dgx_ge_graph_t *graph, int scene);

/* The event the rules of its map are told when one leaves a scene by a side (DGX_GE_SIDE_*); -1 none. */
int dgx_ge_graph_leave_event(const dgx_ge_graph_t *graph, int scene, int side);

/* The scene of a map by its number there; -1 none. */
int dgx_ge_graph_find(const dgx_ge_graph_t *graph, int map, int index);

/* The scene beside one by a side (DGX_GE_SIDE_*), -1 none. */
int dgx_ge_graph_side(const dgx_ge_graph_t *graph, int scene, int side);

/* An exit by its name: of the scene, else of its map; false when there is none. */
bool dgx_ge_graph_exit(const dgx_ge_graph_t *graph, int scene, int name, dgx_ge_go_t *go);

/* An entry of a scene by its name; false when there is none. */
bool dgx_ge_graph_entry(const dgx_ge_graph_t *graph, int scene, int name, dgx_ge_entry_t *entry);

/* A map: its data and its rules (blobs of the pack, -1 none). */
bool dgx_ge_graph_map(const dgx_ge_graph_t *graph, int map, int *data, int *rules);

/* The number a map is named by; 0 none (or no such map). */
int dgx_ge_graph_map_number(const dgx_ge_graph_t *graph, int map);

/*
 * The rules of a map, its actor (started in script 0 when the game comes
 * into the map: dgx_ge_actor_start), told an event of the map at a moment
 * (-1: none, only moved on to it): the exit of the map its script takes (a
 * signal: the exit's name), -1 none. What else its script says (timers,
 * sounds, the world of the map: tell cell, press, clear) goes to say, the
 * game's (NULL: nothing).
 */
int dgx_ge_graph_rule(dgx_ge_actor_t *rules, int event, uint32_t now_ms, dgx_ge_anim_say_fn say, void *ctx);

#ifdef __cplusplus
}
#endif
