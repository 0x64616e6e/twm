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

#endif
