/* Unit tests for pattern drawing (§2.4). */
#include <pixman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "draw.h"

static int failures;

static void check(const char *what, int ok) {
	printf("%s %s\n", ok ? "ok  " : "FAIL", what);
	failures += !ok;
}

int main(void) {
	/* Odd sizes and fractional scales must never write outside the image:
	 * pixman's solid fills don't clip. Guard rows sit before and after. */
	const int sizes[][2] = {{1023, 767}, {7, 7}, {1, 1}, {641, 479}};
	const float scales[] = {1.0f, 1.25f, 1.5f, 2.0f};
	int clean = 1;
	for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
		for (size_t s = 0; s < sizeof(scales) / sizeof(scales[0]); s++) {
			int w = sizes[i][0], h = sizes[i][1];
			size_t guard = (size_t)w * 4;
			uint32_t *mem = malloc((size_t)w * (size_t)(h + 2) * 4);
			memset(mem, 0xAB, (size_t)w * (size_t)(h + 2) * 4);
			pixman_image_t *img = pixman_image_create_bits(PIXMAN_a8r8g8b8,
				w, h, mem + w, w * 4);
			draw_weave(img, scales[s], 6, 3, -5, 0xFF1C1D1A);
			draw_fill(img, 0xFF000000, -10, -10, w + 20, h + 20);
			pixman_image_unref(img);
			const unsigned char *bytes = (const unsigned char *)mem;
			for (size_t k = 0; k < guard; k++) {
				clean &= bytes[k] == 0xAB;
				clean &= bytes[guard + (size_t)w * (size_t)h * 4 + k] == 0xAB;
			}
			free(mem);
		}
	}
	check("draw_weave and draw_fill stay inside the image", clean);

	/* ROOT-1 pattern at scale 1: dot at (2,2)-(3,3) of each 6 px tile. */
	uint32_t px[12 * 12] = {0};
	pixman_image_t *img = pixman_image_create_bits(PIXMAN_a8r8g8b8, 12, 12, px, 48);
	draw_weave(img, 1.0f, 6, 0, 0, 0xFFFFFFFF);
	pixman_image_unref(img);
	int ok = 1;
	for (int y = 0; y < 12; y++) {
		for (int x = 0; x < 12; x++) {
			int dot = (x % 6 == 2 || x % 6 == 3) && (y % 6 == 2 || y % 6 == 3);
			ok &= (px[y * 12 + x] == 0xFFFFFFFF) == dot;
		}
	}
	check("ROOT-1 weave tile: 2x2 dot at (2,2) every 6 px", ok);
	return failures ? 1 : 0;
}
