/*
 * The touch of the display as the game's (dgx_ge_input_di.h): the board's pointer of DGX-DI, the pad and the keyboard
 * on the display, who takes which finger. Not in the engine's library, which does not depend on DGX-DI: the game that
 * has DGX-DI compiles it (dgx_ge_di.cmake). DGX_GE_DI_NONE, for a board without touch (dgx_ge_di.cmake,
 * CONFIG_BOARD_TOUCH 0): the same functions with nothing behind them, nothing of DGX-DI's pad, keyboard and pointer in
 * the firmware.
 */
#include "dgx_ge_input_di.h"

#if DGX_GE_DI_NONE

bool dgx_ge_di_start(struct dgx_di_pointer *board_pointer, int display_mm_w, int display_mm_h)
{
    return false;
}

bool dgx_ge_di_read(uint32_t now_ms, int *x, int *y)
{
    return false;
}

void dgx_ge_di_turned(void)
{
}

bool dgx_ge_pad_show(dgx_screen_t *display, const uint8_t *layout, size_t size, uint8_t alpha)
{
    return false;
}

void dgx_ge_pad_hide(dgx_screen_t *display)
{
}

uint32_t dgx_ge_pad_buttons(void)
{
    return 0;
}

bool dgx_ge_keyboard_open(dgx_screen_t *display, const uint8_t *layout, size_t size, dgx_font_t *font, dgx_ge_rect_t field, dgx_ge_rect_t *band)
{
    return false;
}

void dgx_ge_keyboard_close(void)
{
}

int dgx_ge_keyboard_typed(uint32_t *typed, int max)
{
    return 0;
}

bool dgx_ge_keyboard_shown(void)
{
    return false;
}

void dgx_ge_keyboard_type(uint32_t c)
{
}

#else

#include <stdlib.h>
#include <string.h>

#include "dgx_di.h"
#include "dgx_di_keyboard.h"
#include "dgx_di_pad.h"
#include "esp_log.h"

static dgx_di_pointer_t *pointer;    /* the board's: NULL, no touch */
static int               mm_w, mm_h; /* the display's, as it is shown */

bool dgx_ge_di_start(struct dgx_di_pointer *board_pointer, int display_mm_w, int display_mm_h)
{
    pointer = board_pointer, mm_w = display_mm_w, mm_h = display_mm_h;
    return pointer != NULL;
}

/* ---- the pad ---- */

_Static_assert((unsigned)DGX_DI_PAD_LEFT == DGX_GE_KEY_LEFT && (unsigned)DGX_DI_PAD_RIGHT == DGX_GE_KEY_RIGHT && (unsigned)DGX_DI_PAD_UP == DGX_GE_KEY_UP &&
                   (unsigned)DGX_DI_PAD_DOWN == DGX_GE_KEY_DOWN && (unsigned)DGX_DI_PAD_A == DGX_GE_KEY_A && (unsigned)DGX_DI_PAD_START == DGX_GE_KEY_START &&
                   (unsigned)DGX_DI_PAD_BACK == DGX_GE_KEY_BACK && (unsigned)DGX_DI_PAD_B == DGX_GE_KEY_B && (unsigned)DGX_DI_PAD_X == DGX_GE_KEY_X &&
                   (unsigned)DGX_DI_PAD_Y == DGX_GE_KEY_Y,
               "the pad's buttons are the engine's");

static dgx_di_pad_t   pad;
static bool           padded;
static bool           pad_stale;  /* made for the display as it stood before a turn: made anew when shown next */
static const uint8_t *pad_layout; /* what it was made of */

void dgx_ge_di_turned(void)
{
    pad_stale = true; /* the touch turns with the screen (DGX-DI takes its orientation at every read), the pad does not */
}

bool dgx_ge_pad_show(dgx_screen_t *display, const uint8_t *layout, size_t size, uint8_t alpha)
{
    if (!pointer) return false;
    if (padded && (pad_stale || layout != pad_layout)) dgx_di_pad_free(&pad, display), padded = false; /* made anew */
    pad_stale = false;
    if (!padded) {
        if (!dgx_di_pad_open(&pad, display, mm_w, mm_h, layout, size)) return false;
        padded = true, pad_layout = layout;
    }
    if (dgx_di_pad_show(&pad, display, alpha)) return true;
    dgx_di_pad_free(&pad, display), padded = false;
    return false;
}

void dgx_ge_pad_hide(dgx_screen_t *display)
{
    if (padded) dgx_di_pad_free(&pad, display), padded = false;
}

uint32_t dgx_ge_pad_buttons(void)
{
    return padded ? pad.buttons : 0;
}

/* ---- the keyboard: its struct and what it typed for the game to take, only while it is open ---- */

enum { KEYBOARD_TYPED = 32 };
static struct {
    dgx_di_keyboard_t kb;
    uint32_t          typed[KEYBOARD_TYPED];
    uint8_t           count;
} *keyboard;

bool dgx_ge_keyboard_open(dgx_screen_t *display, const uint8_t *layout, size_t size, dgx_font_t *font, dgx_ge_rect_t field, dgx_ge_rect_t *band)
{
    dgx_ge_keyboard_close();
    if (!pointer || !(keyboard = calloc(1, sizeof *keyboard))) return false;
    const dgx_di_rect_t away = {.x = (int16_t)field.x, .y = (int16_t)field.y, .w = (int16_t)field.w, .h = (int16_t)field.h};
    if (!dgx_di_keyboard_open(&keyboard->kb, display, mm_w, mm_h, layout, size, font, &away)) {
        free(keyboard), keyboard = NULL;
        return false;
    }
    *band = dgx_ge_rect(keyboard->kb.x, keyboard->kb.y, keyboard->kb.w, keyboard->kb.h);
    return true;
}

void dgx_ge_keyboard_close(void)
{
    if (!keyboard) return;
    dgx_di_keyboard_close(&keyboard->kb);
    free(keyboard), keyboard = NULL;
}

int dgx_ge_keyboard_typed(uint32_t *typed, int max)
{
    const int n = keyboard ? (keyboard->count < max ? keyboard->count : max) : 0;
    if (n <= 0) return 0;
    memcpy(typed, keyboard->typed, (size_t)n * sizeof *typed);
    memmove(keyboard->typed, keyboard->typed + n, (size_t)(keyboard->count - n) * sizeof *typed);
    keyboard->count = (uint8_t)(keyboard->count - n);
    return n;
}

bool dgx_ge_keyboard_shown(void)
{
    return keyboard != NULL;
}

void dgx_ge_keyboard_type(uint32_t c)
{
    if (keyboard && keyboard->count < KEYBOARD_TYPED) keyboard->typed[keyboard->count++] = c;
}

/* ---- the fingers handed out ---- */

bool dgx_ge_di_read(uint32_t now_ms, int *x, int *y)
{
    static bool was; /* the first finger down at the last read */
    if (!pointer) return false;
    dgx_di_pointer_read(pointer, now_ms);
    if (keyboard) /* the keyboard's fingers its own: never the pad's, never the first */
        keyboard->count = (uint8_t)(keyboard->count + dgx_di_keyboard_take(&keyboard->kb, pointer, now_ms, keyboard->typed + keyboard->count,
                                                                         KEYBOARD_TYPED - keyboard->count));
    if (padded) dgx_di_pad_take(&pad, pointer); /* the pad's fingers its own: never the first */
    const bool down = dgx_di_pointer_first(pointer, x, y);
    if (down && !was) ESP_LOGI("touch", "down at %d, %d of the display", *x, *y);
    was = down;
    return down;
}

#endif
