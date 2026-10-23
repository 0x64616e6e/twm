#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "draw.h"
#include "outline.h"
#include "pixbuf.h"
#include "text.h"

/* §2.2 roles used here. */
#define LABEL_SIZE 11
#define READOUT_SIZE 15
#define READOUT_TRACKING 0.5f
#define HINT_SIZE 11
#define READOUT_PAD_X 16
#define READOUT_PAD_Y 10
#define READOUT_GAP 4
#define READOUT_BELOW 8 /* OUT-3: gap when the readout doesn't fit inside */

static void set_rect(struct wlr_scene_rect *r, int x, int y, int w, int h) {
	wlr_scene_rect_set_size(r, w > 0 ? w : 0, h > 0 ? h : 0);
	wlr_scene_node_set_position(&r->node, x, y);
}

void outline_init(struct twm_server *server) {
	struct twm_outline *o = calloc(1, sizeof(*o));
	if (!o) {
		return;
	}
	const struct twm_theme *t = &server->theme;
	o->server = server;
	o->tree = wlr_scene_tree_create(server->layers[TWM_LAYER_OVERLAY]);
	float ink[4], thirds[4];
	rgba_premul(rgba_hex(t->ink, 0.85f), ink);    /* outline-ink */
	rgba_premul(rgba_hex(t->ink, 0.35f), thirds); /* outline-thirds */
	for (int i = 0; i < 4; i++) {
		o->edge[i] = wlr_scene_rect_create(o->tree, 0, 0, ink);
		o->third[i] = wlr_scene_rect_create(o->tree, 0, 0, thirds);
	}
	o->title_line = wlr_scene_rect_create(o->tree, 0, 0, ink);
	o->label = wlr_scene_buffer_create(o->tree, NULL);
	o->readout = wlr_scene_buffer_create(o->tree, NULL);
	wlr_scene_node_set_enabled(&o->tree->node, false);

	/* Guides sit below the outline in the same layer. */
	o->guides = wlr_scene_tree_create(server->layers[TWM_LAYER_OVERLAY]);
	wlr_scene_node_lower_to_bottom(&o->guides->node);
	for (int i = 0; i < 4; i++) {
		o->faint[i] = wlr_scene_buffer_create(o->guides, NULL);
	}
	for (int i = 0; i < 2; i++) {
		o->line[i] = wlr_scene_buffer_create(o->guides, NULL);
		o->line_label[i] = wlr_scene_buffer_create(o->guides, NULL);
	}
	o->output_label = wlr_scene_buffer_create(o->guides, NULL);
	wlr_scene_node_set_enabled(&o->guides->node, false);
	server->outline = o;
}

void outline_finish(struct twm_server *server) {
	if (server->outline) {
		free(server->outline->label_text);
		free(server->outline);
		server->outline = NULL;
	}
}

static void draw_label(struct twm_outline *o, const char *label, float scale) {
	if (o->label_text && strcmp(o->label_text, label) == 0 && o->label_scale == scale) {
		return;
	}
	free(o->label_text);
	o->label_text = strdup(label);
	o->label_scale = scale;
	const struct twm_theme *t = &o->server->theme;
	struct twm_font *font = font_get(t->ui_font, LABEL_SIZE, TWM_WEIGHT_REGULAR, scale);
	int w = text_width(font, label, 0);
	int h = font ? font->height : 1;
	struct twm_pixbuf *buf = pixbuf_create(w > 0 ? w : 1, h > 0 ? h : 1);
	if (!buf) {
		return;
	}
	text_draw(buf->image, font, label, 0, font ? font->ascent : 0,
		rgba_argb32(rgba_hex(t->ink, 1)), 0, 0);
	wlr_scene_buffer_set_buffer(o->label, &buf->base);
	pixbuf_drop(buf);
	wlr_scene_buffer_set_dest_size(o->label, (int)ceilf(w / scale), (int)ceilf(h / scale));
}

/* OUT-3: the readout box; returns its logical size. */
static void draw_readout(struct twm_outline *o, const char *geometry, const char *hint,
		float scale, int *lw, int *lh) {
	const struct twm_theme *t = &o->server->theme;
	struct twm_font *gf = font_get(t->ui_font, READOUT_SIZE, TWM_WEIGHT_SEMIBOLD, scale);
	struct twm_font *hf = font_get(t->ui_font, HINT_SIZE, TWM_WEIGHT_REGULAR, scale);
	if (!gf || !hf) {
		*lw = *lh = 0;
		return;
	}
	float tracking = READOUT_TRACKING * scale;
	bool has_hint = hint && *hint;
	int gw = text_width(gf, geometry, tracking);
	int hw = has_hint ? text_width(hf, hint, 0) : 0;
	int pad_x = (int)lroundf(READOUT_PAD_X * scale);
	int pad_y = (int)lroundf(READOUT_PAD_Y * scale);
	int gap = (int)lroundf(READOUT_GAP * scale);
	int content_w = gw > hw ? gw : hw;
	int dw = content_w + 2 * pad_x;
	int dh = 2 * pad_y + gf->height + (has_hint ? gap + hf->height : 0);
	struct twm_pixbuf *buf = pixbuf_create(dw, dh);
	if (!buf) {
		*lw = *lh = 0;
		return;
	}
	draw_fill(buf->image, rgba_argb32(rgba_hex(t->ground, 1)), 0, 0, dw, dh);
	draw_box(buf->image, rgba_argb32(rgba_hex(t->hairline, 1)), 0, 0, dw, dh,
		draw_stroke(scale));
	int y = pad_y + gf->ascent;
	text_draw(buf->image, gf, geometry, (dw - gw) / 2, y,
		rgba_argb32(rgba_hex(t->accent, 1)), tracking, 0);
	if (has_hint) {
		y = pad_y + gf->height + gap + hf->ascent;
		text_draw(buf->image, hf, hint, (dw - hw) / 2, y,
			rgba_argb32(rgba_hex(t->muted, 1)), 0, 0);
	}
	*lw = (int)ceilf(dw / scale);
	*lh = (int)ceilf(dh / scale);
	wlr_scene_buffer_set_buffer(o->readout, &buf->base);
	pixbuf_drop(buf);
	wlr_scene_buffer_set_dest_size(o->readout, *lw, *lh);
}

void outline_show(struct twm_server *server, struct geo_box b,
		const char *label, const char *hint) {
	struct twm_outline *o = server->outline;
	if (!o) {
		return;
	}
	const struct twm_theme *t = &server->theme;
	float scale = output_max_scale(server);
	wlr_scene_node_set_position(&o->tree->node, b.x, b.y);

	/* OUT-2: the frame's outer box, the title line, the thirds. */
	set_rect(o->edge[0], 0, 0, b.w, 1);
	set_rect(o->edge[1], b.w - 1, 1, 1, b.h - 2);
	set_rect(o->edge[2], 0, b.h - 1, b.w, 1);
	set_rect(o->edge[3], 0, 1, 1, b.h - 2);
	set_rect(o->title_line, 1, t->title_height, b.w - 2, 1);
	int a, c;
	geo_thirds(b.w, &a, &c);
	set_rect(o->third[0], a, 1, 1, b.h - 2);
	set_rect(o->third[1], c, 1, 1, b.h - 2);
	geo_thirds(b.h, &a, &c);
	set_rect(o->third[2], 1, a, b.w - 2, 1);
	set_rect(o->third[3], 1, c, b.w - 2, 1);

	draw_label(o, label ? label : "", scale);
	wlr_scene_node_set_position(&o->label->node, 10, 5);

	/* OUT-4: relative to the output holding the top-left. */
	int ox = 0, oy = 0;
	struct twm_output *output = output_at(server, b.x, b.y);
	if (output) {
		ox = output->box.x;
		oy = output->box.y;
	}
	char geometry[64];
	geo_format(geometry, sizeof(geometry), b.w, b.h, b.x - ox, b.y - oy);
	int rw, rh;
	draw_readout(o, geometry, hint, scale, &rw, &rh);
	int rx = (b.w - rw) / 2, ry = (b.h - rh) / 2;
	if (rw + 4 > b.w || rh + t->title_height + 4 > b.h) {
		ry = b.h + READOUT_BELOW;
	}
	wlr_scene_node_set_position(&o->readout->node, rx, ry);

	wlr_scene_node_set_enabled(&o->tree->node, true);
}

void outline_hide(struct twm_server *server) {
	if (server->outline) {
		wlr_scene_node_set_enabled(&server->outline->tree->node, false);
		wlr_scene_node_set_enabled(&server->outline->guides->node, false);
	}
}

/* A 1 px dashed line, `len` logical px long (§2.4 dash: 3 on, 3 off). */
static void set_dashed(struct twm_server *server, struct wlr_scene_buffer *node,
		bool vertical, int x, int y, int len, uint32_t rgb, float scale) {
	const struct twm_theme *t = &server->theme;
	int dl = (int)lroundf(len * scale), thick = draw_stroke(scale);
	struct twm_pixbuf *buf = vertical ? pixbuf_create(thick, dl) : pixbuf_create(dl, thick);
	if (!buf) {
		return;
	}
	uint32_t argb = rgba_argb32(rgba_hex(rgb, 1));
	int period = t->dash_on + t->dash_off;
	for (int i = 0; i < dl; i++) {
		if ((int)(i / scale) % period >= t->dash_on) {
			continue;
		}
		if (vertical) {
			draw_fill(buf->image, argb, 0, i, thick, 1);
		} else {
			draw_fill(buf->image, argb, i, 0, 1, thick);
		}
	}
	wlr_scene_buffer_set_buffer(node, &buf->base);
	pixbuf_drop(buf);
	wlr_scene_buffer_set_dest_size(node, vertical ? 1 : len, vertical ? len : 1);
	wlr_scene_node_set_position(&node->node, x, y);
	wlr_scene_node_set_enabled(&node->node, true);
}

/* One line of small caps-ish text; returns its logical width. */
static int set_small_text(struct twm_server *server, struct wlr_scene_buffer *node,
		const char *text, uint32_t rgb, float tracking, float scale, int *lh) {
	const struct twm_theme *t = &server->theme;
	struct twm_font *font = font_get(t->ui_font, LABEL_SIZE, TWM_WEIGHT_REGULAR, scale);
	if (!font) {
		return 0;
	}
	int w = text_width(font, text, tracking * scale);
	struct twm_pixbuf *buf = pixbuf_create(w > 0 ? w : 1, font->height > 0 ? font->height : 1);
	if (!buf) {
		return 0;
	}
	text_draw(buf->image, font, text, 0, font->ascent, rgba_argb32(rgba_hex(rgb, 1)),
		tracking * scale, 0);
	wlr_scene_buffer_set_buffer(node, &buf->base);
	pixbuf_drop(buf);
	int lw = (int)ceilf(w / scale);
	*lh = (int)ceilf(font->height / scale);
	wlr_scene_buffer_set_dest_size(node, lw, *lh);
	wlr_scene_node_set_enabled(&node->node, true);
	return lw;
}

void outline_guides(struct twm_server *server, struct geo_box out,
		const struct snap_result *snap, bool thirds, const char *output_label) {
	struct twm_outline *o = server->outline;
	if (!o) {
		return;
	}
	const struct twm_theme *t = &server->theme;
	float scale = output_max_scale(server);

	/* SNP-4: faint thirds. */
	for (int i = 0; i < 4; i++) {
		wlr_scene_node_set_enabled(&o->faint[i]->node, false);
	}
	if (thirds) {
		int a, c;
		geo_thirds(out.w, &a, &c);
		set_dashed(server, o->faint[0], true, out.x + a, out.y, out.h, t->guide_faint, scale);
		set_dashed(server, o->faint[1], true, out.x + c, out.y, out.h, t->guide_faint, scale);
		geo_thirds(out.h, &a, &c);
		set_dashed(server, o->faint[2], false, out.x, out.y + a, out.w, t->guide_faint, scale);
		set_dashed(server, o->faint[3], false, out.x, out.y + c, out.w, t->guide_faint, scale);
	}

	/* SNP-3: the snapped axes, labelled at their start. */
	const struct snap_axis *axes[2] = {snap ? &snap->x : NULL, snap ? &snap->y : NULL};
	for (int i = 0; i < 2; i++) {
		bool on = axes[i] && axes[i]->target != SNAP_NONE;
		wlr_scene_node_set_enabled(&o->line[i]->node, on);
		wlr_scene_node_set_enabled(&o->line_label[i]->node, on);
		if (!on) {
			continue;
		}
		char label[64];
		snap_label(axes[i], i == 0, label, sizeof(label));
		int lh;
		if (i == 0) {
			set_dashed(server, o->line[0], true, axes[0]->coord, out.y, out.h, t->accent, scale);
			set_small_text(server, o->line_label[0], label, t->accent, 1.0f, scale, &lh);
			wlr_scene_node_set_position(&o->line_label[0]->node, axes[0]->coord + 8, out.y + 10);
		} else {
			set_dashed(server, o->line[1], false, out.x, axes[1]->coord, out.w, t->accent, scale);
			set_small_text(server, o->line_label[1], label, t->accent, 1.0f, scale, &lh);
			wlr_scene_node_set_position(&o->line_label[1]->node, out.x + 12,
				axes[1]->coord - 6 - lh);
		}
	}

	/* SNP-6: "DP-1 · 2560×1440 · scale 1.25", bottom-right. */
	int lh;
	int lw = set_small_text(server, o->output_label, output_label, t->dim, 0, scale, &lh);
	wlr_scene_node_set_position(&o->output_label->node, out.x + out.w - 12 - lw,
		out.y + out.h - 10 - lh);
	wlr_scene_node_set_enabled(&o->guides->node, true);
}
