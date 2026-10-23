#ifndef TWM_TEXT_H
#define TWM_TEXT_H

#include <pixman.h>
#include <stdbool.h>
#include <stdint.h>

/*
 * Single-line labels with fcft, drawn into pixman images (D-3). Sizes given
 * to font_get are logical px; fonts are rasterized at `scale`, so all metrics
 * and positions here are device pixels.
 */

struct fcft_font;

struct twm_font {
	struct fcft_font *fcft;
	char family[96];
	int size, weight;
	float scale;
	int ascent, descent, height; /* device px */
};

enum twm_weight {
	TWM_WEIGHT_REGULAR = 400,
	TWM_WEIGHT_MEDIUM = 500,
	TWM_WEIGHT_SEMIBOLD = 600,
};

bool text_init(void);
void text_finish(void);

/* Cached; never free the result. `family` may be a fontconfig family list
 * ("Instrument Serif,serif") and may carry ":style=Italic". */
struct twm_font *font_get(const char *family, int size, int weight, float scale);

/* Advance width in device px, with `tracking` device px after each glyph. */
int text_width(struct twm_font *font, const char *utf8, float tracking);

/*
 * Draw at (x, baseline). If the text is wider than max_width (device px,
 * <= 0 for no limit) it is cut and ends in "…" (FRM-5). Returns the width
 * drawn.
 */
int text_draw(pixman_image_t *dst, struct twm_font *font, const char *utf8,
	int x, int baseline, uint32_t argb, float tracking, int max_width);

/* Baseline that centers the font's line box in a band of `height` device px
 * starting at `top`. */
int text_baseline_centered(struct twm_font *font, int top, int height);

#endif
