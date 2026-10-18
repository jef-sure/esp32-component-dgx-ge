#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* The buttons of a game, whatever gives them: keys, a touch screen, a pad. */
enum {
    DGX_GE_KEY_LEFT   = 1u << 0,
    DGX_GE_KEY_RIGHT  = 1u << 1,
    DGX_GE_KEY_UP     = 1u << 2,
    DGX_GE_KEY_DOWN   = 1u << 3,
    DGX_GE_KEY_ACTION = 1u << 4, /* Shift of Prince of Persia: a careful step, grab, drink */
    DGX_GE_KEY_START  = 1u << 5,
    DGX_GE_KEY_SELECT = 1u << 6,
};

/* A pointer (a touch, a mouse): where it is on the display, in pixels; down now, went down, went up since the last look. */
typedef struct {
    int16_t x, y;
    bool    down, pressed, released;
} dgx_ge_pointer_t;

/* The buttons held now, and those that went down and up since the last look; the pointer, when the platform has one. */
typedef struct {
    uint32_t         held;
    uint32_t         pressed;
    uint32_t         released;
    uint32_t         spent; /* done something already (closed a menu, went on in a talk): unseen until let go */
    dgx_ge_pointer_t pointer;
} dgx_ge_input_t;

/* Take the buttons held now; pressed and released are worked out from the last time. A spent button let go is spent no more. */
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
