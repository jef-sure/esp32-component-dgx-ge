#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "dgx_screen.h"
#include "dgx_ge_image.h"
#include "dgx_ge_types.h"

#ifndef DGX_GE_FRAME_MAX_DIRTY
#define DGX_GE_FRAME_MAX_DIRTY 16
#endif

/*
 * What a window costs on the display, in pixels: two rectangles that would
 * send fewer pixels more than this as one are sent as one. Measured on a CYD
 * (ILI9341 on SPI at 40 MHz): a window ~58 us, a pixel ~0.52 us.
 */
#ifndef DGX_GE_FRAME_WINDOW_PIXELS
#define DGX_GE_FRAME_WINDOW_PIXELS 100
#endif

/*
 * The picture of the game: an 8-bit virtual screen of DGX, a palette of 256
 * colors for it and the rectangles that have changed since it was last shown.
 * Only those go to the display, through the palette, as the games of the time
 * did: a 320x200 frame takes 64 KB, its RGB565 copy would take 128 KB.
 */
typedef struct {
    dgx_screen_t *buffer;  /* 8-bit, linear */
    dgx_ge_canvas_t   canvas;  /* its pixels, clipped to the whole frame */
    dgx_screen_t *display; /* where it is shown */
    int           display_x, display_y;
    uint16_t      lut[256]; /* the palette in the format of the display */
    uint8_t       rgb[256][3]; /* the same as RGB888, what fades and flashes are counted from */
    uint16_t      colors_used; /* indices 1 .. colors_used are given to sets; 0 is the paper */
    uint32_t      tint_rgb;    /* a color mixed into the whole palette on the display (dgx_ge_frame_tint), RGB888 */
    uint8_t       tint;        /* how much of it, of 255; 0 none */
    /*
     * Upside down (a potion of the game): the picture shown with its rows the
     * other way. Where the display can do it itself (a driver: the ILI9341's
     * row order), the platform gives orient, and the rows go to their mirrored
     * places as they are; else they are sent one by one, the other way round.
     */
    bool          upside;
    bool        (*orient)(dgx_screen_t *display, bool upside); /* the display's own; NULL none */
    bool          wiping;      /* a wipe going (dgx_ge_frame_wipe): only the columns left of wipe_x go to the display */
    int16_t       wipe_x;
    dgx_ge_rect_t     dirty[DGX_GE_FRAME_MAX_DIRTY];
    int           dirty_count;
    uint32_t      pixels_sent; /* since the frame was made, for statistics */
    /*
     * The scale: pixels of the display a point of the frame, each way (dgx_ge_frame_scale). The frame stays the
     * size of the scene in RAM; a point becomes a square of scale x scale pixels as it is sent, through scaled, a
     * strip of scale rows the width of the display part (8-bit, NULL at 1), or a band of more rows of the frame at
     * once, as many as band bytes hold (dgx_ge_frame_band: the board's choice).
     */
    uint8_t       scale;
    dgx_screen_t *scaled;
    uint32_t      band;
    /*
     * Overlays: items in pixels of the display (a layer of the scene in pixels: text, a dialog), laid over the
     * frame as it is sent, inside the strip, at the display's own resolution. The lists live from the first
     * overlay on; shown is what went to the display last, for what changed to be marked under it, with the box
     * each covered then (its picture may have changed in place since: a text laid anew).
     */
    dgx_ge_item_t *overlays, *shown_overlays;
    dgx_ge_rect_t *shown_boxes;
    uint8_t        overlay_count, shown_overlay_count, overlay_capacity;
    uint8_t        smooth; /* at a scale of 2, 3 or 4: how the points are scaled, DGX_GE_SMOOTH_* (dgx_ge_frame_smooth) */
    dgx_ge_rect_t  cover;  /* pixels of the display something else draws on (dgx_ge_frame_cover): not sent; w 0 none */
} dgx_ge_frame_t;

#ifndef DGX_GE_FRAME_OVERLAYS
#define DGX_GE_FRAME_OVERLAYS 24
#endif

/*
 * The overlays of a frame: given anew every frame of the game between begin and end (as the actors of a scene are;
 * dgx_ge_scene_actors_begin and dgx_ge_scene_draw call these for a layer in pixels). x, y, clip of an item in pixels
 * of the display; a fill fills its clip. end marks the frame under every overlay that changed, came or went. False
 * when there is no room (DGX_GE_FRAME_OVERLAYS) or no memory.
 */
void dgx_ge_frame_overlays_begin(dgx_ge_frame_t *frame);
bool dgx_ge_frame_overlay(dgx_ge_frame_t *frame, const dgx_ge_item_t *item);
void dgx_ge_frame_overlays_end(dgx_ge_frame_t *frame);

/*
 * At a scale of 2, 3 or 4, the points scaled smooth instead of as blocks, on the indices of the palette, by DGX
 * (DGX_VSCREEN_SMOOTH, _SMOOTH_SFX), the same with overlays and without: DGX_GE_SMOOTH_ON, Scale2x (EPX) at 2,
 * Scale3x at 3, Scale2x twice at 4 — a corner of points that lie like a step is filled in, so a slanted edge loses its
 * stairs; DGX_GE_SMOOTH_SFX, their SFX variants (Scale2xSFX, Scale3xSFX), which keep thin slanted lines whole and cost
 * more. No new colors, no blur. The look of the game changes (it is the smoothing of the emulators), so it is a
 * choice of the game or the player, off by default (true is DGX_GE_SMOOTH_ON). At other scales it does nothing.
 * Smoothed, a square looks at the points around its own: a changed place is sent with one point more around it, two
 * with SFX or at 4 (dgx_ge_frame_invalidate); a place under an overlay, as it is.
 */
enum { DGX_GE_SMOOTH_NONE, DGX_GE_SMOOTH_ON, DGX_GE_SMOOTH_SFX };
bool dgx_ge_frame_smooth(dgx_ge_frame_t *frame, int how);

/*
 * The picture moved by dx, dy points (a scene whose window went the other way): the frame's own pixels move in
 * RAM, the display moves its picture itself when it can (dgx_scroll_no_fill: a panel along its own axis, a display
 * read back; else all of the frame is sent again), what was marked moves with it, and the strips that came in at
 * the edges are marked for the scene to draw. The overlays stay where they are: the frame under them is marked, and
 * where the display moved them along.
 * Upside down, wiping, or a move as large as the frame: all of it is drawn again. True when the display moved it.
 */
bool dgx_ge_frame_scroll(dgx_ge_frame_t *frame, int dx, int dy);

/*
 * A rectangle of the display (pixels) covered by something that draws there itself (a keyboard on the display, DGX-DI's):
 * the frame sends nothing into it, to the pixel, whatever the scale, overlays and the way up, and does not move the
 * display's picture itself (dgx_ge_frame_scroll draws all again). A rectangle of no size: nothing covered; what was
 * covered is marked, drawn anew as the frame has it. One at a time: another replaces it.
 */
void dgx_ge_frame_cover(dgx_ge_frame_t *frame, dgx_ge_rect_t display);

/* Mark the frame under a rectangle of the display (pixels), whatever the scale and the way up: sent again, nothing of it changed. */
void dgx_ge_frame_invalidate_pixels(dgx_ge_frame_t *frame, dgx_ge_rect_t display);

dgx_ge_frame_t *dgx_ge_frame_create(dgx_screen_t *display, int x, int y, int width, int height);
void        dgx_ge_frame_destroy(dgx_ge_frame_t **frame);

/*
 * Shown scale times larger: every point of the frame a square of scale x scale pixels of the display, from the
 * frame's place on it (a frame of 320x200 at 4 fills 1280x800). Nothing of the frame or the scene changes: the
 * pictures stay as they are, the motion goes in steps of scale pixels, as the games of the time went on larger
 * screens. 1 to 8; the whole frame is sent again. False when there is no memory for the strip (the scale stays).
 */
bool dgx_ge_frame_scale(dgx_ge_frame_t *frame, int scale);

/*
 * The scaled frame sent in bands of up to so many bytes (of 8-bit points of the display) rather than a strip of a
 * row of the frame at a time: a display that takes large pieces faster (a turned DSI panel, by DMA) wants it, at
 * that cost of RAM while the frame is scaled. 0: a strip, as it is made. False when there is no room for the band
 * (the strip stays).
 */
bool dgx_ge_frame_band(dgx_ge_frame_t *frame, uint32_t bytes);

static inline int dgx_ge_frame_width(const dgx_ge_frame_t *f)
{
    return f->buffer->width;
}

static inline int dgx_ge_frame_height(const dgx_ge_frame_t *f)
{
    return f->buffer->height;
}

/*
 * The colors of a set of pictures given indices of the frame: a color the
 * frame has already shares its index, a new one takes the next free index.
 * map gets the index of every color, as dgx_image_blit() takes it. False
 * when the 255 indices are not enough; then no index is taken and map gets,
 * for every color, the nearest the frame has (0 when it has none).
 */
bool dgx_ge_frame_add_colors(dgx_ge_frame_t *frame, const uint8_t (*rgb)[3], int count, uint8_t *map);

/* Forget the colors given to sets: a new room, a new level. The paper, index 0, stays. */
void dgx_ge_frame_forget_colors(dgx_ge_frame_t *frame);

/* A change of the palette changes every pixel on the display: the whole frame is sent again. */
void dgx_ge_frame_set_color(dgx_ge_frame_t *frame, uint8_t index, uint8_t r, uint8_t g, uint8_t b);
void dgx_ge_frame_set_palette(dgx_ge_frame_t *frame, int first, int count, const uint8_t (*rgb)[3]);

/*
 * The world in another color: so much of rgb (RGB888) mixed into every color
 * of the palette on the display, the colors given later too (a new room);
 * 0 none. The frame keeps its own colors: what is drawn does not change, only
 * how it is shown. The whole frame is sent again.
 */
void dgx_ge_frame_tint(dgx_ge_frame_t *frame, uint32_t rgb, uint8_t amount);

/*
 * Mark a rectangle as changed (smoothed, with the points around it whose squares look at it, dgx_ge_frame_smooth).
 * Close rectangles are merged, and when there are too many of them the two that cost least together become one.
 */
void dgx_ge_frame_invalidate(dgx_ge_frame_t *frame, dgx_ge_rect_t r);
void dgx_ge_frame_invalidate_all(dgx_ge_frame_t *frame);

/* Send what has changed to the display and forget it. */
void dgx_ge_frame_present(dgx_ge_frame_t *frame);

/*
 * A wipe, left to right (the games of the time: a new picture drawn over the
 * old one a strip at a time): the frame is the new picture already, and only
 * its columns left of x go to the display; what changed right of it waits,
 * marked, until the wipe gets there. The game moves x on as time goes; below
 * 0 the wipe is over and all that waits goes. Nothing is kept for it.
 */
void dgx_ge_frame_wipe(dgx_ge_frame_t *frame, int x);

/* The picture upside down, or upright again: all of it shown anew (smoothed too: the upright picture mirrored, to the pixel). */
void dgx_ge_frame_upside_down(dgx_ge_frame_t *frame, bool upside);

#ifdef __cplusplus
}
#endif
