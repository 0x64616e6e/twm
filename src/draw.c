#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include "draw.h"

static pixman_color_t argb_to_color(uint32_t argb) {
	/* pixman colors are 16-bit premultiplied channels. */
	return (pixman_color_t){
		.alpha = (uint16_t)(((argb >> 24) & 0xff) * 0x101),
		.red = (uint16_t)(((argb >> 16) & 0xff) * 0x101),
		.green = (uint16_t)(((argb >> 8) & 0xff) * 0x101),
		.blue = (uint16_t)((argb & 0xff) * 0x101),
	};
}

/* pixman's solid fills don't clip to the image; every caller goes through
 * this so nothing is written outside it. */
static bool clip_rect(pixman_image_t *img, int *x, int *y, int *w, int *h) {
	int iw = pixman_image_get_width(img);
	int ih = pixman_image_get_height(img);
	int x0 = *x < 0 ? 0 : *x;
	int y0 = *y < 0 ? 0 : *y;
	int x1 = *x + *w > iw ? iw : *x + *w;
	int y1 = *y + *h > ih ? ih : *y + *h;
	if (x1 <= x0 || y1 <= y0) {
		return false;
	}
	*x = x0;
	*y = y0;
	*w = x1 - x0;
	*h = y1 - y0;
	return true;
}

void draw_fill(pixman_image_t *img, uint32_t argb, int x, int y, int w, int h) {
	if (!clip_rect(img, &x, &y, &w, &h)) {
		return;
	}
	pixman_color_t color = argb_to_color(argb);
	pixman_rectangle16_t rect = {
		.x = (int16_t)x, .y = (int16_t)y,
		.width = (uint16_t)w, .height = (uint16_t)h,
	};
	pixman_image_fill_rectangles(PIXMAN_OP_SRC, img, &color, 1, &rect);
}

static int floor_mod(int a, int m) {
	int r = a % m;
	return r < 0 ? r + m : r;
}

void draw_weave(pixman_image_t *img, float scale, int pitch,
		int phase_x, int phase_y, uint32_t dot_argb) {
	int dw = pixman_image_get_width(img);
	int dh = pixman_image_get_height(img);
	int lw = (int)ceilf(dw / scale);
	int lh = (int)ceilf(dh / scale);

	/* First tile origin at or before the image's left/top edge. */
	int ox = -floor_mod(phase_x, pitch);
	int oy = -floor_mod(phase_y, pitch);
	int cols = (lw - ox) / pitch + 1;
	int rows = (lh - oy) / pitch + 1;

	pixman_rectangle16_t *rects = calloc((size_t)cols, sizeof(*rects));
	if (!rects) {
		return;
	}
	pixman_color_t color = argb_to_color(dot_argb);
	for (int r = 0; r < rows; r++) {
		int ly = oy + r * pitch + 2;
		int y0 = (int)lroundf(ly * scale);
		int y1 = (int)lroundf((ly + 2) * scale);
		int n = 0;
		for (int col = 0; col < cols; col++) {
			int lx = ox + col * pitch + 2;
			int x0 = (int)lroundf(lx * scale);
			int x1 = (int)lroundf((lx + 2) * scale);
			int rx = x0, ry = y0, rw = x1 - x0, rh = y1 - y0;
			if (!clip_rect(img, &rx, &ry, &rw, &rh)) {
				continue;
			}
			rects[n++] = (pixman_rectangle16_t){
				.x = (int16_t)rx, .y = (int16_t)ry,
				.width = (uint16_t)rw, .height = (uint16_t)rh,
			};
		}
		if (n > 0) {
			pixman_image_fill_rectangles(PIXMAN_OP_SRC, img, &color, n, rects);
		}
	}
	free(rects);
}

int draw_stroke(float scale) {
	int t = (int)floorf(scale);
	return t < 1 ? 1 : t;
}

void draw_box(pixman_image_t *img, uint32_t argb, int x, int y, int w, int h, int t) {
	draw_fill(img, argb, x, y, w, t);
	draw_fill(img, argb, x, y + h - t, w, t);
	draw_fill(img, argb, x, y, t, h);
	draw_fill(img, argb, x + w - t, y, t, h);
}

static int sc(float v, float scale) {
	return (int)lroundf(v * scale);
}

void draw_glyph(pixman_image_t *img, enum twm_glyph glyph, int x, int y,
		float scale, uint32_t argb) {
	int t = draw_stroke(scale);
	switch (glyph) {
	case TWM_GLYPH_ICONIFY:
		draw_box(img, argb, x, y, sc(12, scale), sc(12, scale), t);
		draw_fill(img, argb, x + sc(4, scale), y + sc(4, scale),
			sc(8, scale) - sc(4, scale), sc(8, scale) - sc(4, scale));
		break;
	case TWM_GLYPH_RESIZE:
		draw_box(img, argb, x, y, sc(12, scale), sc(12, scale), t);
		draw_box(img, argb, x, y, sc(8, scale), sc(8, scale), t);
		draw_box(img, argb, x, y, sc(4, scale), sc(4, scale), t);
		break;
	case TWM_GLYPH_LOCK:
		/* Square shackle over a filled body: no curves (CAP-4). */
		draw_box(img, argb, x + sc(3, scale), y, sc(9, scale) - sc(3, scale),
			sc(7, scale), t);
		draw_fill(img, argb, x + sc(1, scale), y + sc(5, scale),
			sc(11, scale) - sc(1, scale), sc(12, scale) - sc(5, scale));
		break;
	}
}

void draw_hatch(pixman_image_t *img, int x, int y, int w, int h, float scale,
		uint32_t argb) {
	int t = draw_stroke(scale);
	for (int k = 0;; k++) {
		int ly = sc(3.0f * (float)k, scale);
		if (ly >= h) {
			break;
		}
		draw_fill(img, argb, x, y + ly, w, t < h - ly ? t : h - ly);
	}
}

void draw_dot(pixman_image_t *img, uint32_t argb, int cx, int cy, int d) {
	float r = d / 2.0f;
	for (int dy = 0; dy < d; dy++) {
		float yy = dy + 0.5f - r;
		int half = (int)lroundf(sqrtf(r * r - yy * yy));
		draw_fill(img, argb, cx - half, cy - d / 2 + dy, 2 * half, 1);
	}
}
