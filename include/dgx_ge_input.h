#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/*
 * The buttons of a game, whatever gives them: keys, a touch screen, a pad. Their layout is the standard one of a game
 * pad (SDL's GameController, the W3C Gamepad API): the arrows are its cross, ACTION its A, SELECT its BACK; a game of
 * fewer buttons takes the first ones and plays the same on a pad, a keyboard or a script of buttons.
 */
enum {
    DGX_GE_KEY_LEFT   = 1u << 0,
    DGX_GE_KEY_RIGHT  = 1u << 1,
    DGX_GE_KEY_UP     = 1u << 2,
    DGX_GE_KEY_DOWN   = 1u << 3,
    DGX_GE_KEY_ACTION = 1u << 4, /* A; Shift of Prince of Persia: a careful step, grab, drink */
    DGX_GE_KEY_START  = 1u << 5,
    DGX_GE_KEY_SELECT = 1u << 6, /* BACK */
    DGX_GE_KEY_B      = 1u << 7,
    DGX_GE_KEY_X      = 1u << 8,
    DGX_GE_KEY_Y      = 1u << 9,
    DGX_GE_KEY_HOME   = 1u << 10,
    DGX_GE_KEY_LB     = 1u << 11, /* the shoulders */
    DGX_GE_KEY_RB     = 1u << 12,
    DGX_GE_KEY_LT     = 1u << 13, /* the triggers as buttons: as the pad says, or by DGX_GE_BRIDGE_TRIGGER_KEYS */
    DGX_GE_KEY_RT     = 1u << 14,
    DGX_GE_KEY_L3     = 1u << 15, /* a stick pressed */
    DGX_GE_KEY_R3     = 1u << 16,
    DGX_GE_KEY_A      = DGX_GE_KEY_ACTION,
    DGX_GE_KEY_BACK   = DGX_GE_KEY_SELECT,
};

/*
 * The axes of a pad: the sticks, -DGX_GE_AXIS_MAX .. DGX_GE_AXIS_MAX (0 the middle, x to the right, y down as the scene
 * goes), the triggers 0 .. DGX_GE_AXIS_MAX. The platform gives them calibrated (its middle at 0, its whole travel the
 * whole range: knowing the stick is its); the engine takes the dead zone off (dgx_ge_input_axes).
 */
enum {
    DGX_GE_AXIS_LX,
    DGX_GE_AXIS_LY,
    DGX_GE_AXIS_RX,
    DGX_GE_AXIS_RY,
    DGX_GE_AXIS_LT,
    DGX_GE_AXIS_RT,
    DGX_GE_AXES,
};
#define DGX_GE_AXIS_MAX  32767
#define DGX_GE_AXIS_DEAD 2621  /* the dead zone unless the game gives its own: about 8 % */
#define DGX_GE_AXIS_ON   16384 /* a bridge holds a button from half the travel on */
#define DGX_GE_AXIS_OFF  12288 /* and lets it go below three eighths: no flicker at the edge */

/* What a game lets go between axes and buttons (dgx_ge_input_t.bridge, added together; none at first). */
enum {
    DGX_GE_BRIDGE_STICK_KEYS   = 1, /* the left stick past half its travel holds the arrows: menus, talks, games of buttons */
    DGX_GE_BRIDGE_TRIGGER_KEYS = 2, /* a trigger past half its travel holds LT, RT */
    DGX_GE_BRIDGE_KEYS_STICK   = 4, /* the arrows move the left stick, all the way, when it rests: a game of the stick by keys, a script */
};

/* The edges of the display a pointer may be swiped from (a panel drawn down from the top, as on a phone). */
enum {
    DGX_GE_EDGE_TOP    = 1u << 0,
    DGX_GE_EDGE_BOTTOM = 1u << 1,
    DGX_GE_EDGE_LEFT   = 1u << 2,
    DGX_GE_EDGE_RIGHT  = 1u << 3,
};
/* The ways a pointer may be swiped. */
enum {
    DGX_GE_SWIPE_UP    = 1u << 0,
    DGX_GE_SWIPE_DOWN  = 1u << 1,
    DGX_GE_SWIPE_LEFT  = 1u << 2,
    DGX_GE_SWIPE_RIGHT = 1u << 3,
};

/*
 * A pointer (a touch, a mouse): where it is on the display, in pixels; down now, went down, went up since the last
 * look; a swipe, from an edge or a plain one, as the platform tells it (the screen input dgx-di works it out:
 * dgx_ge_input_di.h); none by dgx_ge_input_pointer.
 */
typedef struct {
    int16_t x, y;
    bool    down, pressed, released;
    uint8_t edge;  /* DGX_GE_EDGE_*: swiped from that edge since the last look */
    uint8_t swipe; /* DGX_GE_SWIPE_*: swiped that way since the last look */
} dgx_ge_pointer_t;

/*
 * One player's input: the buttons held now, and those that went down and up since the last look; the pointer, when
 * the platform has one; the axes of a pad, when it has one (a second pad: a second of these). RAM: 44 bytes.
 */
typedef struct {
    uint32_t         held;
    uint32_t         pressed;
    uint32_t         released;
    uint32_t         spent; /* done something already (closed a menu, went on in a talk): unseen until let go */
    dgx_ge_pointer_t pointer;
    int16_t          axes[DGX_GE_AXES]; /* the dead zone off; the left stick moved by the arrows (DGX_GE_BRIDGE_KEYS_STICK) */
    uint16_t         dead;   /* the dead zone of the game, of DGX_GE_AXIS_MAX; 0: DGX_GE_AXIS_DEAD, 1: none to speak of */
    uint8_t          bridge; /* DGX_GE_BRIDGE_* */
    uint8_t          given;  /* the engine's: axes given since the last update; the stick moved by the arrows */
} dgx_ge_input_t;

/*
 * The axes of the pad as the platform reads them now (DGX_GE_AXES of them, calibrated), before dgx_ge_input_update:
 * the dead zone taken off — round for a stick (its two axes together: no sticking along the diagonal), from 0 for a
 * trigger — and the rest stretched to the whole range, so a stick past the dead zone starts at 0, not at a jump.
 */
void dgx_ge_input_axes(dgx_ge_input_t *input, const int16_t *axes);

/*
 * Take the buttons held now; pressed and released are worked out from the last time. A spent button let go is spent no
 * more. The bridges the game lets go are crossed here, before: the arrows into the stick, the stick and the triggers
 * into buttons.
 */
void dgx_ge_input_update(dgx_ge_input_t *input, uint32_t held);

/*
 * These buttons have done what they were for (a menu, a talk took them): from now until they are let go they are
 * not held, nor pressed, nor released, so that the hero does not take the same press (a jump as the menu closes).
 */
void dgx_ge_input_spend(dgx_ge_input_t *input, uint32_t keys);

/* Take the pointer as it is now: where, and whether it is down; pressed and released are worked out from the last time. */
void dgx_ge_input_pointer(dgx_ge_input_t *input, int x, int y, bool down);

/*
 * Buttons played by a script instead of a hand: from at_ms on (since the
 * script began) these are held, until the next step. A demonstration, a test,
 * a game that plays itself until a pad is found.
 */
typedef struct {
    uint32_t at_ms;
    uint32_t held;
} dgx_ge_input_step_t;

/* What a script holds at a moment; 0 before its first step, its last step after the end. */
uint32_t dgx_ge_input_scripted(const dgx_ge_input_step_t *steps, int count, uint32_t since_ms);

#ifdef __cplusplus
}
#endif
