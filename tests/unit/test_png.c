/* Unit tests for the PNG writer and its text chunks (SHOT-2, SHR-1, D-11). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "check.h"
#include "png.h"

int main(void) {
	/* a screenshot-like image: flat ground, a frame, some noise */
	int w = 640, h = 400;
	uint32_t *px = malloc((size_t)w * h * 4);
	unsigned seed = 7;
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			uint32_t c = 0xFF0D0E0B;
			if (x > 100 && x < 400 && y > 60 && y < 86) {
				c = 0xFFFFB000;
			} else if (x > 450 && y > 200) {
				seed = seed * 1103515245 + 12345;
				c = 0xFF000000 | (seed >> 8);
			}
			px[y * w + x] = c;
		}
	}
	const char *rc = "Accent #7FD1AE\nSqueezeTitle { \"*\" left }\n\"run…\" f.run\n";
	struct png_text texts[] = {{"twm:twmrc", rc}, {"twm:version", "0.1.0"}};
	size_t len = 0;
	clock_t t0 = clock();
	uint8_t *png = png_encode(px, w, h, (size_t)w * 4, texts, 2, &len);
	double ms = (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC;
	CHECK("D-11", png != NULL && png_is_png(png, len));
	printf("     %zu bytes from %d raw (%.0f ms)\n", len, w * h * 3, ms);
	CHECK("D-11", len < (size_t)(w * h * 3) / 2); /* it compresses */
	char *got = png_text_get(png, len, "twm:twmrc");
	CHECK("SHOT-2", got && strcmp(got, rc) == 0); /* UTF-8 survives */
	free(got);
	got = png_text_get(png, len, "twm:version");
	CHECK("SHOT-2", got && strcmp(got, "0.1.0") == 0);
	free(got);
	CHECK("SHR-1", png_text_get(png, len, "twm:nope") == NULL);
	CHECK("SHR-1", png_text_get(png, 40, "twm:twmrc") == NULL); /* cut short: none, no crash */
	CHECK("SHR-1", png_text_get((const uint8_t *)"not a png at all", 16, "twm:twmrc") == NULL);
	const char *outside = getenv("TWM_PNG_TEST_OUT"); /* for an outside decoder to check */
	if (outside) {
		CHECK("D-11", png_write(outside, px, w, h, (size_t)w * 4, texts, 2));
	}
	free(png);
	free(px);
	return UNIT_RESULT();
}
