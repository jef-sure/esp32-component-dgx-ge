#pragma once
#ifdef __cplusplus
extern "C" {
#endif

/*
 * The display input DGX-DI (https://github.com/jef-sure/esp32-component-dgx-di) as the pointer of a game: a game that
 * has it includes this header and gives its touch to the input once a step, so the engine's dialogs, widgets and the
 * swipes from an edge take it as they take any pointer. The engine itself does not depend on DGX-DI: src/dgx_ge_di.c is
 * compiled by the game that names it (dgx_ge_di.cmake), and this header needs DGX-DI only for its tail, which is there
 * when DGX-DI is (a board with touch: jef-sure/boards brings it).
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dgx_font.h"
#include "dgx_ge_input.h"
#include "dgx_ge_types.h"
#include "dgx_screen.h"

/*
 * The touch of the display as the game's (src/dgx_ge_di.c): the board's pointer of DGX-DI over its display, set up and
 * calibrated by the board (jef-sure/boards: board_dgx_di_pointer of board_touch_start), given once; its fingers read once a pass and handed out, the
 * keyboard's first, then the pad's, the first finger of the rest the game's. One a game. start: false when pointer is
 * NULL (a board without touch), and then nothing of this shows (the pad, the keyboard: false) and nothing is down;
 * mm_w, mm_h the display's size in millimetres as it is shown (the size of the pad, of the keys). read: the pointer
 * read at now_ms, its first finger, in pixels of the display as it stands (turned, a frame upside down: DGX-DI takes
 * the screen's orientation), and whether it is down. turned: the display was turned (board_display_orientation), so the
 * pad is made anew for it when it is shown next.
 */
struct dgx_di_pointer; /* DGX-DI's pointer, dgx_di_pointer_t: named by its tag, so a board without touch needs no DGX-DI */
bool dgx_ge_di_start(struct dgx_di_pointer *pointer, int mm_w, int mm_h);
bool dgx_ge_di_read(uint32_t now_ms, int *x, int *y);
void dgx_ge_di_turned(void);

/*
 * The pad on the display (DGX-DI's, translucent over the game): shown, this layout (a blob of DGX-DI's pad editor,
 * size bytes, kept: dgx_di_pad_open) at this alpha (made for the display as it stands: anew after a turn or for
 * another layout, else only its alpha changed), false when there is no touch, the layout is not one or there is no room
 * for its layer and mask (PSRAM); hidden, its memory given back; its buttons held now, the bits of DGX_GE_KEY_*, as
 * dgx_ge_di_read last read them (its fingers are the pad's, never the game's).
 */
bool     dgx_ge_pad_show(dgx_screen_t *display, const uint8_t *layout, size_t size, uint8_t alpha);
void     dgx_ge_pad_hide(dgx_screen_t *display);
uint32_t dgx_ge_pad_buttons(void);

/*
 * The keyboard on the display (DGX-DI's: opaque, in a band of its own at the bottom of the display, at the top when the
 * bottom would cover the field): opened, this layout (a blob of DGX-DI's keyboard editor, size bytes, kept) with its
 * labels in font, away from field (pixels of the display: the field typed into, dgx_ge_talk_view_editing); band, where
 * it stands, for the frame not to draw there (dgx_ge_frame_cover); false when there is no touch, or the layout is not
 * one or does not fit. Its memory only while it is open. Its fingers are its own, before the pad's and the game's
 * (dgx_ge_di_read); typed gives what it typed since it was last asked, at most max code points (characters, U+0008
 * erase, U+000A done, U+001B cancel): how many. Never with the pad: hide it first. shown: whether it is open; type: a
 * code point typed otherwise (the keys of a computer) taken as if typed on it, while it is open.
 */
bool dgx_ge_keyboard_open(dgx_screen_t *display, const uint8_t *layout, size_t size, dgx_font_t *font, dgx_ge_rect_t field, dgx_ge_rect_t *band);
void dgx_ge_keyboard_close(void);
int  dgx_ge_keyboard_typed(uint32_t *typed, int max);
bool dgx_ge_keyboard_shown(void);
void dgx_ge_keyboard_type(uint32_t c);

/* With DGX-DI (a board with touch): its touch as the input's pointer, the same values (as numbers: two enums) */
#if __has_include("dgx_di.h")
#include "dgx_di.h"

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
#endif

#ifdef __cplusplus
}
#endif
