#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "dgx_font.h"
#include "dgx_image.h"
#include "dgx_screen.h"

/*
 * A field of text: a box of so many points its text is laid into, word by
 * word, line by line, and drawn by a font of DGX into a 1-bit canvas (a
 * virtual screen of DGX: rows from a new byte, the leftmost point in the high
 * bit) whose pixels are a picture the scene shows (in a color of the style).
 *
 * What does not go in: the font is made smaller first, scale by scale down to
 * the least the style allows; still too much, an important text is shown
 * whole, its lines going up one by one (scroll), and an unimportant one is
 * cut, its last line shown ending in "...": the box does not grow.
 *
 * A handwritten font of DGX (pen paths, dgx_hw_font.h) is written by its pen,
 * letters joined; its scale is the height of a line in points (any size: a
 * smaller one tried point by point). Appearing, the pen goes through the
 * letters come: a letter is written stroke by stroke as it comes.
 *
 * A box of 8 bits a point holds an amount of ink, 0..DGX_GE_TEXT_INK: a soft
 * pen leaves a share of it on the edge of a line (dgx_draw.h, DGX_PEN_SOFT),
 * a font of pixels the whole of it; its picture is drawn through a map of
 * the amounts to colors. 8 times the memory of a box of 1 bit.
 *
 * Lines: a box is made for so many lines of text (4 bytes a line for where
 * each begins and ends), or for any number (0): then its tables grow as a
 * text needs, so a long important text is whole and scrolls to its end. A
 * box of so many lines lays no more: the text is cut there.
 */
#define DGX_GE_TEXT_INK 15

/* Where a line stands in its box. */
enum { DGX_GE_ALIGN_LEFT, DGX_GE_ALIGN_MIDDLE, DGX_GE_ALIGN_RIGHT };

typedef struct {
    dgx_font_t *font;
    uint8_t     scale_max, scale_min; /* the scales of the font tried, the largest first */
    uint16_t    scroll_ms;            /* an important text too long: a line up every so many ms (the first shown so long too); 0 by hand */
    uint8_t     align;                /* DGX_GE_ALIGN_*: each line, the whole of it (appearing, its letters where they will stand) */
} dgx_ge_text_style_t;

typedef struct {
    dgx_screen_t *canvas;
    uint8_t       bits;     /* a point: 1, or 8 (an amount of ink) */
    bool          owner;    /* the canvas is its own (else another box's, dgx_ge_textbox_share) */
    dgx_image_t   image;    /* the canvas as a picture: 1 bit, its origin top-left */
    int           width, height;
    /* the text laid */
    const char   *text;
    const dgx_ge_text_style_t *style;
    uint8_t       scale;
    uint16_t      lines, shown; /* how many lines it took, how many the box shows */
    uint16_t      top;          /* the first line shown */
    bool          scrolling, cut;
    uint16_t     *starts, *ends; /* each line, bytes of the text: capacity of them */
    uint16_t      capacity;
    bool          grow;          /* made for any number of lines: the tables grow as a text needs */
    uint32_t      since_ms;     /* when the line on top came up */
    uint16_t      letters;      /* letters shown, 0xFFFF all of them */
    uint8_t       letter_ms;    /* appearing: a letter every so many ms, 0 at once */
    uint32_t      shown_ms;     /* when it began to appear */
} dgx_ge_textbox_t;

/* How high a line of a font is at a scale, in points (a handwritten font: the scale itself). */
int dgx_ge_text_line(const dgx_font_t *font, int scale);

/* A box of so many points (its width is made a whole number of bytes) for so many lines (0: any number); false when there is no memory. */
bool dgx_ge_textbox_init(dgx_ge_textbox_t *box, int width, int height, int lines);

/* The same, of 1 or 8 bits a point (8: an amount of ink, a soft pen). */
bool dgx_ge_textbox_init_bits(dgx_ge_textbox_t *box, int width, int height, int bits, int lines);

/*
 * A box on the canvas of another, never shown at the same time (the line and
 * the answers of a talk): whichever is set draws it anew, with lines of its
 * own (0: any number). Freed, it leaves the canvas to the other. False when
 * there is no memory for its lines.
 */
bool dgx_ge_textbox_share(dgx_ge_textbox_t *box, const dgx_ge_textbox_t *with, int lines);
void dgx_ge_textbox_free(dgx_ge_textbox_t *box);

/* A text (UTF-8, kept by the caller while shown; '\n' breaks a line) laid in a style, and drawn, at a moment. */
void dgx_ge_textbox_set(dgx_ge_textbox_t *box, const char *text, const dgx_ge_text_style_t *style, bool important, uint32_t now_ms);

/* Moved on to now: a scrolling text goes up a line when its time comes; true when the picture changed. */
bool dgx_ge_textbox_play(dgx_ge_textbox_t *box, uint32_t now_ms);

/* By hand: lines up (or down when negative), as far as there are; true when the picture changed. */
bool dgx_ge_textbox_scroll(dgx_ge_textbox_t *box, int lines);

/* The text laid appears letter by letter from now, a letter every so many ms (0: at once). */
void dgx_ge_textbox_appear(dgx_ge_textbox_t *box, uint8_t letter_ms, uint32_t now_ms);

/* All of it at once (a button pressed while it appears); true when the picture changed. */
bool dgx_ge_textbox_appear_all(dgx_ge_textbox_t *box);

/* Is it still appearing? */
bool dgx_ge_textbox_appearing(const dgx_ge_textbox_t *box);

/* Is the whole text seen now (or was it cut)? An important text is read to its end before one goes on; one appearing is not seen yet. */
bool dgx_ge_textbox_at_end(const dgx_ge_textbox_t *box);

#ifdef __cplusplus
}
#endif
