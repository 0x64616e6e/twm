#ifndef TWM_DRAW_H
#define TWM_DRAW_H

#include <pixman.h>
#include <stdint.h>

/*
 * Pattern and glyph drawing into pixman images (DESIGN.md §2.4). Colors are
 * premultiplied ARGB8888 (see rgba_argb32). Coordinates are device pixels
 * unless a function takes a scale.
 */

void draw_fill(pixman_image_t *img, uint32_t argb, int x, int y, int w, int h);

/*
 * Dot weave: a `pitch`-px logical tile with a 2x2 dot at (2,2). `phase_x/y`
 * is the layout position of the image's origin, so the weave stays continuous
 * across outputs.
 */
void draw_weave(pixman_image_t *img, float scale, int pitch,
	int phase_x, int phase_y, uint32_t dot_argb);

/* A filled round dot of diameter `d` (device px) centred at (cx, cy). */
void draw_dot(pixman_image_t *img, uint32_t argb, int cx, int cy, int d);

/* Device stroke for a 1 logical px line at `scale` (GEN-3). */
int draw_stroke(float scale);

/* Hollow rectangle, stroke `t` device px drawn inside the box. */
void draw_box(pixman_image_t *img, uint32_t argb, int x, int y, int w, int h, int t);

enum twm_glyph {
	TWM_GLYPH_ICONIFY, /* §2.4: outline square, filled centre */
	TWM_GLYPH_RESIZE,  /* §2.4: three nested squares from the top-left */
};

/* A 12 logical px glyph with its top-left at device (x, y). */
void draw_glyph(pixman_image_t *img, enum twm_glyph glyph, int x, int y,
	float scale, uint32_t argb);

/* Highlight hatch (§2.4): 1 px lines every 3 logical px, from the box's top. */
void draw_hatch(pixman_image_t *img, int x, int y, int w, int h, float scale,
	uint32_t argb);

#endif
