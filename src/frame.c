#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "draw.h"
#include "frame.h"
#include "pixbuf.h"
#include "text.h"
#include "view.h"

/* §2.2 title roles: tracking for the focused title. */
#define TITLE_TRACKING_FOCUSED 0.3f

struct tab_colors {
	uint32_t bg, fg, sep, hatch, outline;
	bool has_outline;
	int weight;
	float tracking;
};

static struct tab_colors colors_for(const struct twm_theme *t, enum frame_state state) {
	struct twm_rgba accent = rgba_hex(t->accent, 1);
	struct tab_colors c = {0};
	switch (state) {
	case FRAME_FOCUSED: /* FRM-7 */
		c.bg = rgba_argb32(accent);
		c.fg = rgba_argb32(rgba_hex(t->on_accent, 1));
		c.sep = rgba_argb32(rgba_over(rgba_hex(t->on_accent, 0.30f), accent));
		c.hatch = rgba_argb32(rgba_over(rgba_hex(t->on_accent, 0.55f), accent));
		c.weight = TWM_WEIGHT_SEMIBOLD;
		c.tracking = TITLE_TRACKING_FOCUSED;
		break;
	case FRAME_URGENT: /* FRM-9 */
		c.bg = rgba_argb32(rgba_hex(t->graphite, 1));
		c.fg = rgba_argb32(accent);
		c.sep = c.fg;
		c.outline = c.fg;
		c.has_outline = true;
		c.weight = TWM_WEIGHT_SEMIBOLD;
		break;
	case FRAME_UNFOCUSED: /* FRM-8 */
		c.bg = rgba_argb32(rgba_hex(t->graphite, 1));
		c.fg = rgba_argb32(rgba_hex(t->muted, 1));
		c.sep = rgba_argb32(rgba_hex(t->hairline, 1));
		c.weight = TWM_WEIGHT_MEDIUM;
		break;
	}
	return c;
}

enum frame_state frame_state_of(struct twm_view *view) {
	if (view->server->focused == view) {
		return FRAME_FOCUSED;
	}
	return view->urgent ? FRAME_URGENT : FRAME_UNFOCUSED;
}

static bool view_resizable(struct twm_view *view) {
	struct twm_size_limits l = view->impl->get_size_limits(view);
	return !(l.min_width > 0 && l.min_height > 0 &&
		l.min_width == l.max_width && l.min_height == l.max_height);
}

static struct tab_metrics metrics_for(struct twm_frame *frame, struct twm_font *font,
		float scale) {
	const struct twm_theme *t = &frame->view->server->theme;
	return (struct tab_metrics){
		.title_height = t->title_height,
		.separator = t->button_separator,
		.pad = t->title_pad,
		.hatch_width = t->hatch_width,
		.hatch_gap = t->hatch_gap,
		.ellipsis_width = (int)ceilf(text_width(font, "\xe2\x80\xa6", 0) / scale),
	};
}

static struct twm_font *title_font(struct twm_server *server, int weight, float scale) {
	const struct twm_theme *t = &server->theme;
	return font_get(t->ui_font, t->font_size, weight, scale);
}

int frame_min_width(struct twm_frame *frame) {
	float scale = output_max_scale(frame->view->server);
	struct twm_font *font = title_font(frame->view->server, TWM_WEIGHT_SEMIBOLD, scale);
	struct tab_metrics m = metrics_for(frame, font, scale);
	return tab_min_width(&m, true, view_resizable(frame->view));
}

static void draw_tab(struct twm_frame *frame, const char *title, enum frame_state state,
		float scale) {
	struct twm_server *server = frame->view->server;
	const struct twm_theme *t = &server->theme;
	struct tab_colors c = colors_for(t, state);
	struct twm_font *font = title_font(server, c.weight, scale);
	struct tab_metrics m = metrics_for(frame, font, scale);
	float tracking = c.tracking * scale;

	struct tab_input in = {
		.frame_width = frame->view->width + 2 * t->border_width,
		.text_width = (int)ceilf(text_width(font, title, tracking) / scale),
		.hatch = state == FRAME_FOCUSED && t->title_highlight,
		.iconify = true,
		.resize = view_resizable(frame->view),
		.justify = TWM_JUSTIFY_LEFT, /* SqueezeTitle is configurable from M2 */
	};
	struct tab_layout *l = &frame->layout;
	tab_layout_compute(&m, &in, l);

#define X(v) ((int)lroundf((float)(v) * scale))
	int dw = X(l->width), dh = X(t->title_height);
	struct twm_pixbuf *buf = pixbuf_create(dw, dh);
	if (!buf) {
		return;
	}
	pixman_image_t *img = buf->image;
	int stroke = draw_stroke(scale);
	int glyph_off = (t->title_height - 12) / 2;

	draw_fill(img, c.bg, 0, 0, dw, dh);
	if (c.has_outline) {
		draw_box(img, c.outline, 0, 0, dw, dh, stroke);
	}
	if (l->iconify.width) {
		draw_glyph(img, TWM_GLYPH_ICONIFY, X(l->iconify.x + glyph_off), X(glyph_off),
			scale, c.fg);
		draw_fill(img, c.sep, X(l->sep_left.x), 0, stroke, dh);
	}
	if (l->text.width > 0) {
		int tx = X(l->text.x);
		int baseline = text_baseline_centered(font, 0, dh);
		text_draw(img, font, title, tx, baseline, c.fg, tracking,
			X(l->text.x + l->text.width) - tx);
	}
	if (l->hatch.width > 0) {
		int inset = t->hatch_inset;
		draw_hatch(img, X(l->hatch.x), X(inset), X(l->hatch.x + l->hatch.width) - X(l->hatch.x),
			X(t->title_height - inset) - X(inset), scale, c.hatch);
	}
	if (l->resize.width) {
		draw_fill(img, c.sep, X(l->sep_right.x), 0, stroke, dh);
		draw_glyph(img, TWM_GLYPH_RESIZE, X(l->resize.x + glyph_off), X(glyph_off),
			scale, c.fg);
	}
#undef X

	wlr_scene_buffer_set_buffer(frame->tab, &buf->base);
	pixbuf_drop(buf);
	wlr_scene_buffer_set_dest_size(frame->tab, l->width, t->title_height);
	wlr_scene_node_set_position(&frame->tab->node, l->x, 0);
}

/* Perimeter position of a pixel on the body box's border, clockwise from
 * the top-left (§2.4 dash phase). */
static int perimeter_pos(int x, int y, int w, int h, int bw) {
	if (y < bw) {
		return x;
	}
	if (x >= w - bw) {
		return (w - 1) + y;
	}
	if (y >= h - bw) {
		return (w - 1) + (h - 1) + (w - 1 - x);
	}
	return 2 * (w - 1) + (h - 1) + (h - 1 - y);
}

static void draw_dash_edge(struct twm_frame *frame, int edge, int w, int h, float scale) {
	const struct twm_theme *t = &frame->view->server->theme;
	int bw = t->border_width;
	/* Logical box of this edge inside the body box (w x h). */
	struct { int x, y, w, h; } e[4] = {
		{0, 0, w, bw},              /* top */
		{w - bw, bw, bw, h - 2 * bw}, /* right */
		{0, h - bw, w, bw},         /* bottom */
		{0, bw, bw, h - 2 * bw},    /* left */
	};
	if (e[edge].w <= 0 || e[edge].h <= 0) {
		wlr_scene_node_set_enabled(&frame->dash[edge]->node, false);
		return;
	}
	int dw = (int)lroundf(e[edge].w * scale), dh = (int)lroundf(e[edge].h * scale);
	struct twm_pixbuf *buf = pixbuf_create(dw > 0 ? dw : 1, dh > 0 ? dh : 1);
	if (!buf) {
		return;
	}
	uint32_t on = rgba_argb32(rgba_hex(t->accent, 1));
	int period = t->dash_on + t->dash_off;
	for (int dy = 0; dy < dh; dy++) {
		for (int dx = 0; dx < dw; dx++) {
			int lx = e[edge].x + (int)(dx / scale);
			int ly = e[edge].y + (int)(dy / scale);
			if (perimeter_pos(lx, ly, w, h, bw) % period < t->dash_on) {
				buf->data[dy * (buf->stride / 4) + dx] = on;
			}
		}
	}
	wlr_scene_buffer_set_buffer(frame->dash[edge], &buf->base);
	pixbuf_drop(buf);
	wlr_scene_buffer_set_dest_size(frame->dash[edge], e[edge].w, e[edge].h);
	wlr_scene_node_set_position(&frame->dash[edge]->node, e[edge].x,
		t->title_height + e[edge].y);
	wlr_scene_node_set_enabled(&frame->dash[edge]->node, true);
}

static void update_border(struct twm_frame *frame, enum frame_state state, float scale) {
	struct twm_view *view = frame->view;
	const struct twm_theme *t = &view->server->theme;
	int bw = t->border_width, th = t->title_height;
	int w = view->width + 2 * bw, h = view->height + 2 * bw;
	if (frame->drawn_border.valid && frame->drawn_border.state == state &&
			frame->drawn_border.width == w && frame->drawn_border.height == h &&
			frame->drawn_border.scale == scale) {
		return;
	}
	frame->drawn_border.valid = true;
	frame->drawn_border.state = state;
	frame->drawn_border.width = w;
	frame->drawn_border.height = h;
	frame->drawn_border.scale = scale;

	bool dashed = state == FRAME_URGENT;
	float color[4];
	rgba_premul(rgba_hex(state == FRAME_FOCUSED ? t->accent : t->hairline, 1), color);
	struct { int x, y, w, h; } r[4] = {
		{0, th, w, bw},
		{w - bw, th + bw, bw, h - 2 * bw},
		{0, th + h - bw, w, bw},
		{0, th + bw, bw, h - 2 * bw},
	};
	for (int i = 0; i < 4; i++) {
		wlr_scene_rect_set_color(frame->border[i], color);
		wlr_scene_rect_set_size(frame->border[i], r[i].w > 0 ? r[i].w : 0,
			r[i].h > 0 ? r[i].h : 0);
		wlr_scene_node_set_position(&frame->border[i]->node, r[i].x, r[i].y);
		wlr_scene_node_set_enabled(&frame->border[i]->node, !dashed);
		if (dashed) {
			draw_dash_edge(frame, i, w, h, scale);
		} else {
			wlr_scene_node_set_enabled(&frame->dash[i]->node, false);
		}
	}
}

void frame_update(struct twm_frame *frame) {
	struct twm_view *view = frame->view;
	struct twm_server *server = view->server;
	const struct twm_theme *t = &server->theme;
	enum frame_state state = frame_state_of(view);
	float scale = output_max_scale(server);
	const char *title = view_title(view);
	int fw = view->width + 2 * t->border_width;
	bool resize = view_resizable(view);

	update_border(frame, state, scale);

	if (frame->drawn.valid && frame->drawn.state == state &&
			frame->drawn.scale == scale && frame->drawn.frame_width == fw &&
			frame->drawn.resize == resize && frame->drawn.title &&
			strcmp(frame->drawn.title, title) == 0) {
		return;
	}
	free(frame->drawn.title);
	frame->drawn.title = strdup(title);
	frame->drawn.state = state;
	frame->drawn.scale = scale;
	frame->drawn.frame_width = fw;
	frame->drawn.resize = resize;
	frame->drawn.valid = true;
	draw_tab(frame, title, state, scale);
}

struct twm_frame *frame_create(struct twm_view *view) {
	struct twm_frame *frame = calloc(1, sizeof(*frame));
	if (!frame) {
		return NULL;
	}
	frame->view = view;
	frame->tree = wlr_scene_tree_create(view->tree);
	frame->tab_hit = (struct twm_hit){.kind = TWM_HIT_TITLE, .view = view};
	frame->border_hit = (struct twm_hit){.kind = TWM_HIT_BORDER, .view = view};
	frame->tab = wlr_scene_buffer_create(frame->tree, NULL);
	frame->tab->node.data = &frame->tab_hit;
	float none[4] = {0};
	for (int i = 0; i < 4; i++) {
		frame->border[i] = wlr_scene_rect_create(frame->tree, 0, 0, none);
		frame->border[i]->node.data = &frame->border_hit;
		frame->dash[i] = wlr_scene_buffer_create(frame->tree, NULL);
		frame->dash[i]->node.data = &frame->border_hit;
		wlr_scene_node_set_enabled(&frame->dash[i]->node, false);
	}
	return frame;
}

void frame_destroy(struct twm_frame *frame) {
	if (!frame) {
		return;
	}
	wlr_scene_node_destroy(&frame->tree->node);
	free(frame->drawn.title);
	free(frame);
}

enum twm_hit_kind frame_part_at(struct twm_frame *frame, double lx, double ly) {
	int x = (int)floor(lx) - frame->view->x - frame->layout.x;
	switch (tab_part_at(&frame->layout, x)) {
	case TAB_PART_ICONIFY:
		return TWM_HIT_ICONIFY;
	case TAB_PART_RESIZE:
		return TWM_HIT_RESIZE;
	default:
		return TWM_HIT_TITLE;
	}
}
