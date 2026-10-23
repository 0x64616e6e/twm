#ifndef TWM_THEME_H
#define TWM_THEME_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Design tokens, DESIGN.md §2. Colors are 0xRRGGBB; alpha-derived tokens are
 * stored as (base, alpha) and blended where they are drawn, because the
 * accent is configurable.
 */

struct twm_rgba {
	float r, g, b, a; /* straight (not premultiplied) alpha, 0..1 */
};

struct twm_theme {
	/* §2.1 color */
	uint32_t ground, surface, surface_sunk, graphite, hairline,
		hairline_strong, rule_faint, guide_faint, mini, ink, ink_2,
		muted, dim, accent, on_accent, ok, alert;

	/* §2.3 metrics, logical px */
	int title_height;
	int border_width;
	int button_separator;
	int title_pad;
	int utility_title_pad;
	int hatch_width;
	int hatch_inset;
	int hatch_gap;
	int dash_on, dash_off;

	/* §2.2 type */
	char ui_font[96];
	int font_size;
	char display_font[64];

	/* feature switches (§5 registry) */
	bool title_highlight;
	bool root_weave;
	bool root_wordmark;
	int focus_delay_ms;
};

void theme_init_defaults(struct twm_theme *theme);

struct twm_rgba rgba_hex(uint32_t rgb, float alpha);
/* Opaque result of drawing `top` (with its alpha) over opaque `bottom`. */
struct twm_rgba rgba_over(struct twm_rgba top, struct twm_rgba bottom);
/* Premultiplied float[4] for wlr_scene_rect. */
void rgba_premul(struct twm_rgba c, float out[4]);
/* Premultiplied ARGB8888 for pixman / buffers. */
uint32_t rgba_argb32(struct twm_rgba c);

#endif
