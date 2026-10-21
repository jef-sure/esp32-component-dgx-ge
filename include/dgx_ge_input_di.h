#pragma once
#ifdef __cplusplus
extern "C" {
#endif

/*
 * The display input DGX-DI (https://github.com/jef-sure/esp32-component-dgx-di) as the pointer of a game: a game that
 * has it includes this header and gives its touch to the input once a step, so the engine's dialogs, widgets and the
 * swipes from an edge take it as they take any pointer. The engine itself does not depend on DGX-DI: only who includes
 * this compiles it.
 */
#include "dgx_di.h"
#include "dgx_ge_input.h"

/* the same values (as numbers: two enums) */
_Static_assert((unsigned)DGX_GE_EDGE_TOP == DGX_DI_EDGE_TOP && (unsigned)DGX_GE_EDGE_BOTTOM == DGX_DI_EDGE_BOTTOM &&
                   (unsigned)DGX_GE_EDGE_LEFT == DGX_DI_EDGE_LEFT && (unsigned)DGX_GE_EDGE_RIGHT == DGX_DI_EDGE_RIGHT,
               "the edges of the engine are those of dgx-di");
_Static_assert((unsigned)DGX_GE_SWIPE_UP == DGX_DI_SWIPE_UP && (unsigned)DGX_GE_SWIPE_DOWN == DGX_DI_SWIPE_DOWN &&
                   (unsigned)DGX_GE_SWIPE_LEFT == DGX_DI_SWIPE_LEFT && (unsigned)DGX_GE_SWIPE_RIGHT == DGX_DI_SWIPE_RIGHT,
               "the swipes of the engine are those of dgx-di");

/* The touch as it is now (taken since the last look, dgx_di_touch_take) as the input's pointer: where, down, pressed, released, a swipe. */
static inline void dgx_ge_input_take_di(dgx_ge_input_t *input, const dgx_di_touch_t *touch)
{
    input->pointer = (dgx_ge_pointer_t){.x = touch->x, .y = touch->y, .down = touch->down, .pressed = touch->pressed,
                                        .released = touch->released, .edge = touch->edge, .swipe = touch->swipe};
}

#ifdef __cplusplus
}
#endif
