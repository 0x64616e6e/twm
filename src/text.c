#include <fcft/fcft.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "text.h"

#define FONT_CACHE_MAX 32

static struct twm_font cache[FONT_CACHE_MAX];
static int cache_len;
static bool initialized;

bool text_init(void) {
	if (!initialized) {
		initialized = fcft_init(FCFT_LOG_COLORIZE_NEVER, false, FCFT_LOG_CLASS_ERROR);
	}
	return initialized;
}

void text_finish(void) {
	for (int i = 0; i < cache_len; i++) {
		fcft_destroy(cache[i].fcft);
	}
	cache_len = 0;
	if (initialized) {
		fcft_fini();
		initialized = false;
	}
}

static const char *weight_name(int weight) {
	if (weight >= 600) {
		return "semibold";
	}
	if (weight >= 500) {
		return "medium";
	}
	return "regular";
}

struct twm_font *font_get(const char *family, int size, int weight, float scale) {
	for (int i = 0; i < cache_len; i++) {
		struct twm_font *f = &cache[i];
		if (f->size == size && f->weight == weight && f->scale == scale &&
				strcmp(f->family, family) == 0) {
			return f;
		}
	}
	if (!text_init()) {
		return NULL;
	}
	if (cache_len == FONT_CACHE_MAX) {
		/* Drop the oldest; labels re-request what they need. */
		fcft_destroy(cache[0].fcft);
		memmove(&cache[0], &cache[1], sizeof(cache[0]) * (FONT_CACHE_MAX - 1));
		cache_len--;
	}

	/* Primary face first, then generic fallbacks for missing glyphs. */
	char primary[192];
	snprintf(primary, sizeof(primary), "%s:weight=%s", family, weight_name(weight));
	const char *names[] = {primary, "monospace", "emoji"};
	char attrs[64];
	snprintf(attrs, sizeof(attrs), "pixelsize=%.2f", size * scale);
	struct fcft_font *ff = fcft_from_name(3, names, attrs);
	if (!ff) {
		return NULL;
	}
	struct twm_font *f = &cache[cache_len++];
	memset(f, 0, sizeof(*f));
	f->fcft = ff;
	snprintf(f->family, sizeof(f->family), "%s", family);
	f->size = size;
	f->weight = weight;
	f->scale = scale;
	f->ascent = ff->ascent;
	f->descent = ff->descent;
	f->height = ff->height;
	return f;
}

/* Minimal UTF-8 decoder; invalid bytes become U+FFFD. */
static uint32_t next_cp(const char **s) {
	const unsigned char *p = (const unsigned char *)*s;
	uint32_t cp;
	int len;
	if (p[0] < 0x80) {
		cp = p[0];
		len = 1;
	} else if ((p[0] & 0xE0) == 0xC0) {
		cp = p[0] & 0x1F;
		len = 2;
	} else if ((p[0] & 0xF0) == 0xE0) {
		cp = p[0] & 0x0F;
		len = 3;
	} else if ((p[0] & 0xF8) == 0xF0) {
		cp = p[0] & 0x07;
		len = 4;
	} else {
		*s += 1;
		return 0xFFFD;
	}
	for (int i = 1; i < len; i++) {
		if ((p[i] & 0xC0) != 0x80) {
			*s += i;
			return 0xFFFD;
		}
		cp = (cp << 6) | (p[i] & 0x3F);
	}
	*s += len;
	return cp;
}

static size_t decode(const char *utf8, uint32_t **out) {
	size_t cap = strlen(utf8) + 1, n = 0;
	uint32_t *cps = malloc(cap * sizeof(*cps));
	if (!cps) {
		*out = NULL;
		return 0;
	}
	const char *s = utf8;
	while (*s) {
		uint32_t cp = next_cp(&s);
		if (cp >= 0x20 && cp != 0x7F) {
			cps[n++] = cp;
		}
	}
	*out = cps;
	return n;
}

/* Pen positions for each glyph, in device px from the start; pos[n] is the
 * total width. */
static void layout(struct twm_font *font, const uint32_t *cps, size_t n,
		float tracking, float *pos) {
	float x = 0;
	for (size_t i = 0; i < n; i++) {
		pos[i] = x;
		const struct fcft_glyph *g =
			fcft_rasterize_char_utf32(font->fcft, cps[i], FCFT_SUBPIXEL_NONE);
		long kx = 0, ky = 0;
		if (i + 1 < n) {
			fcft_kerning(font->fcft, cps[i], cps[i + 1], &kx, &ky);
		}
		x += (g ? g->advance.x : 0) + kx + tracking;
	}
	pos[n] = x - (n > 0 ? tracking : 0);
}

int text_width(struct twm_font *font, const char *utf8, float tracking) {
	if (!font || !utf8) {
		return 0;
	}
	uint32_t *cps;
	size_t n = decode(utf8, &cps);
	float *pos = malloc((n + 1) * sizeof(*pos));
	if (!cps || !pos) {
		free(cps);
		free(pos);
		return 0;
	}
	layout(font, cps, n, tracking, pos);
	int w = (int)ceilf(pos[n]);
	free(cps);
	free(pos);
	return w;
}

static void draw_glyph(pixman_image_t *dst, pixman_image_t *src,
		const struct fcft_glyph *g, int x, int baseline) {
	if (g->is_color_glyph) {
		pixman_image_composite32(PIXMAN_OP_OVER, g->pix, NULL, dst,
			0, 0, 0, 0, x + g->x, baseline - g->y, g->width, g->height);
	} else {
		pixman_image_composite32(PIXMAN_OP_OVER, src, g->pix, dst,
			0, 0, 0, 0, x + g->x, baseline - g->y, g->width, g->height);
	}
}

int text_draw(pixman_image_t *dst, struct twm_font *font, const char *utf8,
		int x, int baseline, uint32_t argb, float tracking, int max_width) {
	if (!font || !utf8) {
		return 0;
	}
	uint32_t *cps;
	size_t n = decode(utf8, &cps);
	float *pos = malloc((n + 2) * sizeof(*pos));
	if (!cps || !pos) {
		free(cps);
		free(pos);
		return 0;
	}
	layout(font, cps, n, tracking, pos);

	size_t count = n;
	bool ellipsis = false;
	if (max_width > 0 && pos[n] > (float)max_width) {
		const uint32_t dots = 0x2026;
		const struct fcft_glyph *eg =
			fcft_rasterize_char_utf32(font->fcft, dots, FCFT_SUBPIXEL_NONE);
		float ew = eg ? (float)eg->advance.x : 0;
		count = 0;
		while (count < n && pos[count + 1] + tracking + ew <= (float)max_width) {
			count++;
		}
		ellipsis = true;
	}

	pixman_color_t color = {
		.alpha = (uint16_t)(((argb >> 24) & 0xff) * 0x101),
		.red = (uint16_t)(((argb >> 16) & 0xff) * 0x101),
		.green = (uint16_t)(((argb >> 8) & 0xff) * 0x101),
		.blue = (uint16_t)((argb & 0xff) * 0x101),
	};
	pixman_image_t *src = pixman_image_create_solid_fill(&color);
	float end = 0;
	for (size_t i = 0; i < count; i++) {
		const struct fcft_glyph *g =
			fcft_rasterize_char_utf32(font->fcft, cps[i], FCFT_SUBPIXEL_NONE);
		if (g) {
			draw_glyph(dst, src, g, x + (int)lroundf(pos[i]), baseline);
			end = pos[i] + (float)g->advance.x;
		}
	}
	if (ellipsis) {
		float ex = count > 0 ? end + tracking : 0;
		const struct fcft_glyph *g =
			fcft_rasterize_char_utf32(font->fcft, 0x2026, FCFT_SUBPIXEL_NONE);
		if (g) {
			draw_glyph(dst, src, g, x + (int)lroundf(ex), baseline);
			end = ex + (float)g->advance.x;
		}
	}
	pixman_image_unref(src);
	free(cps);
	free(pos);
	return (int)ceilf(end);
}

int text_baseline_centered(struct twm_font *font, int top, int height) {
	if (!font) {
		return top + height;
	}
	int box = font->ascent + font->descent;
	return top + (height - box + 1) / 2 + font->ascent;
}
