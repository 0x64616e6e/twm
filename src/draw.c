#include <math.h>
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

void draw_fill(pixman_image_t *img, uint32_t argb, int x, int y, int w, int h) {
	if (w <= 0 || h <= 0) {
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
			if (x1 <= 0 || x0 >= dw || y1 <= 0 || y0 >= dh) {
				continue;
			}
			rects[n++] = (pixman_rectangle16_t){
				.x = (int16_t)x0, .y = (int16_t)y0,
				.width = (uint16_t)(x1 - x0), .height = (uint16_t)(y1 - y0),
			};
		}
		if (n > 0) {
			pixman_image_fill_rectangles(PIXMAN_OP_SRC, img, &color, n, rects);
		}
	}
	free(rects);
}
