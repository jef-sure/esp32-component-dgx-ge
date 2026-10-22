#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bus/dgx_bus_protocols.h"
#include "dgx_font.h"
#include "dgx_ge_types.h"
#include "dgx_screen.h"

/*
 * Boards games of DGX-GE have run on, set up as those games use them: a convenience, not a must (the engine knows a
 * screen of DGX, not a board). A directory a board: its file, which the library of the engine does not build,
 * board.cmake, and the ESP-IDF settings it needs (sdkconfig.defaults, on an ESP32-P4 and one of its chip's revision);
 * a game takes one of them into its own build explicitly (boards/<board>/board.cmake in its main/CMakeLists.txt), or
 * copies it into its main/ as a start of its own. What depends on the game stays the game's: the scale of its frame,
 * its fonts, the samples of its sounds, what it keeps, the buttons of its pad, a firmware not ours to give (the touch
 * panel's of the 10").
 * - cyd/cyd.c: the CYD, ESP32-2432S028R, an ESP32 with a 320x240 ILI9341 on SPI; no touch, no sound;
 * - jc8012p4a1c/jc8012p4a1c.c: the Guition JC8012P4A1C, an ESP32-P4 with a 10.1-inch 800x1280 JD9365 on MIPI-DSI,
 *   lying: 1280x800; its touch panel a GSL3680 (a device of DGX-DI), its sound an ES8311 on I2S.
 * - jc1060p470/jc1060p470.c: the Guition JC1060P470, an ESP32-P4 of revision 1.3 with a 7-inch 1024x600 JD9165 on
 *   MIPI-DSI; its touch panel a GT911 (ESP-IDF's driver as a device of DGX-DI), its sound an ES8311 on I2S.
 * - host/host.c: the computer, in a window of SDL2, as large as the board the game is built for (host.mk): the game's
 *   own app_main on a PC, the mouse a finger, the keys buttons, SDL's sound, NVS on files.
 * Sound is I2S only on the boards (the computer's: SDL); a game with hardware of its own for it gives the engine its own
 * dgx_ge_sound_t.
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
 * The board's keyboard on its display (DGX-DI's: opaque, in a band of its own at the bottom of the display, at the top
 * when the bottom would cover the field), after board_pointer_start: opened, this layout (a blob of DGX-DI's keyboard
 * editor, size bytes, kept) with its labels in font, away from field (pixels of the display: the field typed into,
 * dgx_ge_talk_view_editing); band, where it stands, for the frame not to draw there (dgx_ge_frame_cover); false when
 * the board has none, or the layout is not one or does not fit. Its memory only while it is open. Its fingers are its
 * own, before the pad's and the pointer's (board_pointer); typed gives what it typed since it was last asked, at most
 * max code points (characters, U+0008 erase, U+000A done, U+001B cancel): how many. Never with the pad: hide it first.
 */
extern const bool board_keyboard_able;
bool board_keyboard_open(dgx_screen_t *display, const uint8_t *layout, size_t size, dgx_font_t *font, dgx_ge_rect_t field, dgx_ge_rect_t *band);
void board_keyboard_close(void);
int  board_keyboard_typed(uint32_t *typed, int max);

/*
 * The board's own buttons held now, the bits of DGX_GE_KEY_* (the computer's keys: boards/host); 0 on a board that has
 * none. A game takes them with the pad's.
 */
extern const bool board_buttons_able;
uint32_t          board_buttons(void);

/*
 * The board's sound, by I2S: its hardware set up and a task of its own that takes the next samples from the mixer
 * (mono, 16 bits, at board_sound_rate) and sends them; false when the board has none. It gives the mixer the lock of
 * that task (the mixer's lock), so the game calls the mixer, and its output, as it likes. It goes on until the
 * firmware stops: the mixer lives as long.
 */
struct dgx_ge_mixer;
extern const uint32_t board_sound_rate;
extern const uint8_t  board_sound_volume; /* of the mixer, 1..255, with the game's sound on: the board's speaker, a room */
bool board_sound_start(struct dgx_ge_mixer *mixer);
