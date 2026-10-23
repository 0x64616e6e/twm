#include <math.h>
#include <string.h>
#include "theme.h"

void theme_init_defaults(struct twm_theme *t) {
	memset(t, 0, sizeof(*t));
	t->ground = 0x0D0E0B;
	t->surface = 0x121310;
	t->surface_sunk = 0x0F100D;
	t->graphite = 0x23251F;
	t->hairline = 0x2F312A;
	t->hairline_strong = 0x3A3D34;
	t->rule_faint = 0x1E201B;
	t->guide_faint = 0x2A2C26;
	t->mini = 0x5A5D53;
	t->ink = 0xE8E6DF;
	t->ink_2 = 0xC9C7BF;
	t->muted = 0x9A9C93;
	t->dim = 0x85887C;
	t->accent = 0xFFB000;
	t->on_accent = 0x0E0F0C;
	t->ok = 0x9FD8B8;
	t->alert = 0xFF8A65;

	t->title_height = 26;
	t->border_width = 1;
	t->button_separator = 1;
	t->title_pad = 14;
	t->utility_title_pad = 12;
	t->hatch_width = 72;
	t->hatch_inset = 6;
	t->hatch_gap = 8;
	t->dash_on = 3;
	t->dash_off = 3;

	strcpy(t->ui_font, "IBM Plex Mono");
	t->font_size = 12;
	strcpy(t->display_font, "Instrument Serif:style=Italic");

	t->title_highlight = true;
	t->root_weave = true;
	t->root_wordmark = true;
	t->focus_delay_ms = 80;
}

struct twm_rgba rgba_hex(uint32_t rgb, float alpha) {
	return (struct twm_rgba){
		.r = ((rgb >> 16) & 0xff) / 255.0f,
		.g = ((rgb >> 8) & 0xff) / 255.0f,
		.b = (rgb & 0xff) / 255.0f,
		.a = alpha,
	};
}

struct twm_rgba rgba_over(struct twm_rgba top, struct twm_rgba bottom) {
	float a = top.a;
	return (struct twm_rgba){
		.r = top.r * a + bottom.r * (1 - a),
		.g = top.g * a + bottom.g * (1 - a),
		.b = top.b * a + bottom.b * (1 - a),
		.a = 1,
	};
}

void rgba_premul(struct twm_rgba c, float out[4]) {
	out[0] = c.r * c.a;
	out[1] = c.g * c.a;
	out[2] = c.b * c.a;
	out[3] = c.a;
}

static uint32_t to8(float v) {
	if (v <= 0) {
		return 0;
	}
	if (v >= 1) {
		return 255;
	}
	return (uint32_t)lroundf(v * 255.0f);
}

uint32_t rgba_argb32(struct twm_rgba c) {
	return to8(c.a) << 24 | to8(c.r * c.a) << 16 | to8(c.g * c.a) << 8 |
		to8(c.b * c.a);
}
