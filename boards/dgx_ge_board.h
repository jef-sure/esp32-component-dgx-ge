#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bus/dgx_bus_protocols.h"
#include "dgx_screen.h"

/*
 * Boards games of DGX-GE have run on, set up as those games use them: a convenience, not a must (the engine knows a
 * screen of DGX, not a board). A file a board, which the library of the engine does not build: a game takes one of
 * them into its own build explicitly (boards/<board>.cmake in its main/CMakeLists.txt), or copies it into its main/
 * as a start of its own. What depends on the game stays the game's: the scale of its frame, its fonts, the samples of
 * its sounds, what it keeps, the buttons of its pad, a firmware not ours to give (the touch panel's of the 10").
 * - cyd.c: the CYD, ESP32-2432S028R, an ESP32 with a 320x240 ILI9341 on SPI; no touch, no sound;
 * - jc8012p4a1c.c: the Guition JC8012P4A1C, an ESP32-P4 with a 10.1-inch 800x1280 JD9365 on MIPI-DSI, lying:
 *   1280x800; its touch panel a GSL3680 (a device of DGX-DI), its sound an ES8311 on I2S.
 * - jc1060p470.c: the Guition JC1060P470, an ESP32-P4 of revision 1.3 with a 7-inch 1024x600 JD9165 on MIPI-DSI;
 *   its touch panel a GT911 (ESP-IDF's driver as a device of DGX-DI), its sound an ES8311 on I2S.
 * Sound is I2S only; a game with hardware of its own for it gives the engine its own dgx_ge_sound_t.
 */
typedef struct {
    dgx_bus_protocols_t *bus;
    dgx_screen_t        *lcd;
} board_t;

/* Bytes the scaled frame goes to the display in at once (dgx_ge_frame_band): 0, a strip a row of the frame. */
extern const uint32_t board_band;

/* The display's size in millimetres, lying as it is shown (measured with a ruler): the gestures of a touch, the pad. */
extern const int board_mm_w, board_mm_h;

/* The display in landscape, black. false when it could not be set up. */
bool board_display_init(board_t *board);

/* Its rows the other way (a frame upside down: dgx_ge_frame_t orient), by the display itself. */
bool board_orient(dgx_screen_t *display, bool upside);

/* The board held the other way up: the picture turned round from the next board_orient on (a setting of the game). */
void board_turn(bool turned);

/*
 * The board's pointer (a touch panel, a device of DGX-DI): start sets it up under the display, false when the board
 * has none; pointer reads it at now_ms and gives its first finger, in pixels of the display as it stands (turned, a
 * frame upside down: DGX-DI takes the screen's orientation), and whether it is down.
 */
bool board_pointer_start(dgx_screen_t *display);
bool board_pointer(uint32_t now_ms, int *x, int *y);
/* The touch panel as a device of DGX-DI, for a measure of the board (dgx_di_calibrate_measure); NULL: none. */
struct dgx_di_device;
struct dgx_di_device *board_touch_device(void);

/*
 * The board's pad on its display (DGX-DI's, translucent over the game), after board_pointer_start: shown, this layout
 * (a blob of DGX-DI's pad editor, size bytes, kept: dgx_di_pad_open) at this alpha (made for the display as it stands:
 * anew after board_turn or for another layout, else only its alpha changed), false when the board has none or the
 * layout is not one; hidden, its memory given back; its buttons held now, the bits of DGX_GE_KEY_*, as board_pointer
 * last read them (its fingers are the pad's, never the pointer's).
 */
extern const bool board_pad_able; /* the board can show one: a touch of several fingers, PSRAM for its layer and mask */
bool     board_pad_show(dgx_screen_t *display, const uint8_t *layout, size_t size, uint8_t alpha);
void     board_pad_hide(dgx_screen_t *display);
uint32_t board_pad_buttons(void);

/*
 * The board's sound, by I2S: its hardware set up and a task of its own that takes the next samples from the mixer
 * (mono, 16 bits, at board_sound_rate) and sends them; false when the board has none. The game's calls into the mixer
 * go between board_sound_lock and board_sound_unlock, as the task's do.
 */
struct dgx_ge_mixer;
extern const uint32_t board_sound_rate;
extern const uint8_t  board_sound_volume; /* of the mixer, 1..255, with the game's sound on: the board's speaker, a room */
bool board_sound_start(struct dgx_ge_mixer *mixer);
void board_sound_lock(void);
void board_sound_unlock(void);
