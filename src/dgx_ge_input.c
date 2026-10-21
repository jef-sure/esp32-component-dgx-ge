#include <math.h>

#include "dgx_ge_input.h"

/* what input->given keeps */
enum {
    GIVEN_AXES  = 1, /* the platform gave axes since the last update */
    GIVEN_KEYED = 2, /* the left stick is where the arrows put it */
};

static int16_t clamp_axis(int v, int low)
{
    return (int16_t)(v < low ? low : v > DGX_GE_AXIS_MAX ? DGX_GE_AXIS_MAX : v);
}

/* A stick, its two axes together: inside the dead zone nothing, past it the rest of the way stretched to the whole. */
static void stick(int16_t *out, int x, int y, int dead)
{
    const float length = sqrtf((float)x * x + (float)y * y);
    if (length <= dead) {
        out[0] = out[1] = 0;
        return;
    }
    /* a square stick goes past the circle in its corners: no farther than the whole range */
    const float k = ((length >= DGX_GE_AXIS_MAX ? DGX_GE_AXIS_MAX : length) - dead) / (DGX_GE_AXIS_MAX - dead) * DGX_GE_AXIS_MAX / length;
    out[0] = clamp_axis((int)lroundf(x * k), -DGX_GE_AXIS_MAX);
    out[1] = clamp_axis((int)lroundf(y * k), -DGX_GE_AXIS_MAX);
}

/* A trigger: from 0, the dead zone off and the rest stretched. */
static int16_t trigger(int v, int dead)
{
    v = clamp_axis(v, 0);
    return v <= dead ? 0 : (int16_t)lroundf((float)(v - dead) / (DGX_GE_AXIS_MAX - dead) * DGX_GE_AXIS_MAX);
}

void dgx_ge_input_axes(dgx_ge_input_t *input, const int16_t *axes)
{
    const int dead = input->dead ? input->dead : DGX_GE_AXIS_DEAD;
    stick(&input->axes[DGX_GE_AXIS_LX], axes[DGX_GE_AXIS_LX], axes[DGX_GE_AXIS_LY], dead);
    stick(&input->axes[DGX_GE_AXIS_RX], axes[DGX_GE_AXIS_RX], axes[DGX_GE_AXIS_RY], dead);
    input->axes[DGX_GE_AXIS_LT] = trigger(axes[DGX_GE_AXIS_LT], dead);
    input->axes[DGX_GE_AXIS_RT] = trigger(axes[DGX_GE_AXIS_RT], dead);
    input->given = (uint8_t)((input->given | GIVEN_AXES) & ~GIVEN_KEYED);
}

/* A button an axis holds: on from DGX_GE_AXIS_ON, kept down to DGX_GE_AXIS_OFF while it was on. */
static uint32_t by_axis(int v, uint32_t key, uint32_t was)
{
    return v >= ((was & key) ? DGX_GE_AXIS_OFF : DGX_GE_AXIS_ON) ? key : 0;
}

void dgx_ge_input_update(dgx_ge_input_t *input, uint32_t held)
{
    const uint32_t arrows = DGX_GE_KEY_LEFT | DGX_GE_KEY_RIGHT | DGX_GE_KEY_UP | DGX_GE_KEY_DOWN;
    int16_t *const lx = &input->axes[DGX_GE_AXIS_LX], *const ly = &input->axes[DGX_GE_AXIS_LY];
    /* the stick the arrows moved last time rests again, unless the platform has said where it is since */
    if ((input->given & (GIVEN_KEYED | GIVEN_AXES)) == GIVEN_KEYED) *lx = *ly = 0;
    input->given = 0;
    if ((input->bridge & DGX_GE_BRIDGE_KEYS_STICK) && (held & arrows) && !*lx && !*ly) {
        const int x = (held & DGX_GE_KEY_RIGHT ? 1 : 0) - (held & DGX_GE_KEY_LEFT ? 1 : 0);
        const int y = (held & DGX_GE_KEY_DOWN ? 1 : 0) - (held & DGX_GE_KEY_UP ? 1 : 0);
        /* along the diagonal as far as along an axis: the circle of the stick, not the corner of the square */
        const int full = x && y ? 23170 : DGX_GE_AXIS_MAX;
        *lx = (int16_t)(x * full), *ly = (int16_t)(y * full);
        if (x || y) input->given = GIVEN_KEYED;
    }
    const uint32_t was = input->held | input->spent;
    if (input->bridge & DGX_GE_BRIDGE_STICK_KEYS) {
        held |= by_axis(-*lx, DGX_GE_KEY_LEFT, was) | by_axis(*lx, DGX_GE_KEY_RIGHT, was);
        held |= by_axis(-*ly, DGX_GE_KEY_UP, was) | by_axis(*ly, DGX_GE_KEY_DOWN, was);
    }
    if (input->bridge & DGX_GE_BRIDGE_TRIGGER_KEYS)
        held |= by_axis(input->axes[DGX_GE_AXIS_LT], DGX_GE_KEY_LT, was) | by_axis(input->axes[DGX_GE_AXIS_RT], DGX_GE_KEY_RT, was);
    input->spent &= held;
    held &= ~input->spent;
    input->pressed  = held & ~input->held;
    input->released = input->held & ~held;
    input->held     = held;
}

void dgx_ge_input_spend(dgx_ge_input_t *input, uint32_t keys)
{
    keys &= input->held | input->pressed;
    input->spent |= keys;
    input->held &= ~keys, input->pressed &= ~keys, input->released &= ~keys;
}

uint32_t dgx_ge_input_scripted(const dgx_ge_input_step_t *steps, int count, uint32_t since_ms)
{
    uint32_t held = 0;
    for (int i = 0; i < count && steps[i].at_ms <= since_ms; ++i) held = steps[i].held;
    return held;
}

void dgx_ge_input_pointer(dgx_ge_input_t *input, int x, int y, bool down)
{
    dgx_ge_pointer_t *p = &input->pointer;
    p->pressed  = down && !p->down;
    p->released = !down && p->down;
    p->down     = down;
    p->x = (int16_t)x, p->y = (int16_t)y;
    p->edge = 0, p->swipe = 0;
}
