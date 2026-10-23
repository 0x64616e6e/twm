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
	}
}
