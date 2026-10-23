#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "draw.h"
#include "frame.h"
#include "group.h"
#include "pixbuf.h"
#include "settings.h"
#include "text.h"
#include "view.h"

/* CAP-1 (B6): "SCREEN" after the title: hairline, 12, a 7 px dot, 6, the
 * word (11 px, tracked 1, 600, alert), 12. */
#define MARKER_TEXT "SCREEN"
#define MARKER_SIZE 11

static int marker_width(struct twm_server *server, float scale) {
	const struct twm_theme *t = &server->theme;
	struct twm_font *f = font_get(t->ui_font, MARKER_SIZE, TWM_WEIGHT_SEMIBOLD, scale);
	return 1 + 12 + 7 + 6 + (int)ceilf(text_width(f, MARKER_TEXT, 1.0f * scale) / scale) + 12;
}

static void draw_marker_segment(pixman_image_t *img, struct twm_server *server, int x, int w,
		float scale) {
	const struct twm_theme *t = &server->theme;
#define X(v) ((int)lroundf((float)(v) * scale))
	int dh = X(t->title_height);
	uint32_t alert = rgba_argb32(rgba_hex(t->alert, 1));
	draw_fill(img, rgba_argb32(rgba_hex(t->graphite, 1)), X(x), 0, X(x + w) - X(x), dh);
	draw_fill(img, rgba_argb32(rgba_hex(t->hairline, 1)), X(x), 0, draw_stroke(scale), dh);
	draw_dot(img, alert, X(x + 1 + 12) + X(7) / 2, dh / 2, X(7));
	struct twm_font *f = font_get(t->ui_font, MARKER_SIZE, TWM_WEIGHT_SEMIBOLD, scale);
	text_draw(img, f, MARKER_TEXT, X(x + 1 + 12 + 7 + 6), text_baseline_centered(f, 0, dh),
		alert, 1.0f * scale, 0);
#undef X
}

/* §2.2: no tracking on titles, so the text doesn't shift on focus (FRM-11). */
#define TITLE_TRACKING_FOCUSED 0.0f

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
	if (view->server->focused == view || view->trusted) { /* TRU-1: accent, always */
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
		.pad = frame->view->utility ? t->utility_title_pad : t->title_pad, /* FRM-18 */
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
	bool utility = frame->view->utility;
	return tab_min_width(&m, !utility, !utility && view_resizable(frame->view));
}

/* HNT-3: a selected hint shows as the tab of B4's "j": graphite, ink text,
 * a 1 px accent outline. */
static struct tab_colors hint_selected_colors(const struct twm_theme *t) {
	struct tab_colors c = colors_for(t, FRAME_UNFOCUSED);
	c.fg = rgba_argb32(rgba_hex(t->ink, 1));
	c.outline = rgba_argb32(rgba_hex(t->accent, 1));
	c.has_outline = true;
	return c;
}

/* HNT-1: the hint letter in place of the iconify box. */
static void draw_hint_box(pixman_image_t *img, struct twm_view *view, int x, float scale) {
	const struct twm_theme *t = &view->server->theme;
#define X(v) ((int)lroundf((float)(v) * scale))
	int box = X(t->title_height);
	struct twm_font *f = font_get(t->ui_font, t->font_size + 1, TWM_WEIGHT_SEMIBOLD, scale);
	uint32_t accent = rgba_argb32(rgba_hex(t->accent, 1));
	uint32_t fg;
	if (view->hint_selected) {
		draw_fill(img, rgba_argb32(rgba_hex(t->on_accent, 1)), X(x), 0, box, box);
		draw_box(img, accent, X(x), 0, box, box, 2 * draw_stroke(scale));
		fg = accent;
	} else {
		draw_fill(img, accent, X(x), 0, box, box);
		fg = rgba_argb32(rgba_hex(t->on_accent, 1));
	}
	int w = text_width(f, view->hint, 0);
	text_draw(img, f, view->hint, X(x) + (box - w) / 2, text_baseline_centered(f, 0, box), fg, 0, 0);
#undef X
}

static void draw_group_tab(struct twm_frame *frame, enum frame_state state, float scale) {
	struct twm_view *view = frame->view;
	struct twm_server *server = view->server;
	const struct twm_theme *t = &server->theme;
	/* A lone window with a drop slot open draws as a group of one. */
	struct twm_group *g = view->group;
	int n = g ? (int)group_size(g) : 1;
	if (n > TAB_GROUP_MAX) {
		n = TAB_GROUP_MAX;
	}
	struct twm_view *members[TAB_GROUP_MAX];
	struct tab_colors colors[TAB_GROUP_MAX];
	struct tab_group_input in = {
		.frame_width = view->width + 2 * t->border_width,
		.n = n,
		.active = g ? group_index(view) : 0,
		.hatch = state == FRAME_FOCUSED && t->title_highlight && !view->hint_selected,
		.resize = view_resizable(view),
		.folded = g && g->folded,
		.slot_index = frame->slot_index,
		.slot_width = frame->slot_width,
		.justify = config_justify(server->config, view->impl->get_app_id(view),
			view->impl->get_title(view)),
	};
	struct twm_font *any = title_font(server, TWM_WEIGHT_MEDIUM, scale);
	struct tab_metrics m = metrics_for(frame, any, scale);
	for (int i = 0; i < n; i++) {
		members[i] = g ? group_member(g, i) : view;
		enum frame_state st = i == in.active ? state :
			members[i]->urgent ? FRAME_URGENT : FRAME_UNFOCUSED;
		colors[i] = i == in.active && view->hint_selected ? hint_selected_colors(t) :
			colors_for(t, st);
		struct twm_font *f = title_font(server, colors[i].weight, scale);
		in.text_width[i] = (int)ceilf(text_width(f, view_title(members[i]), 0) / scale);
	}
	struct tab_group_layout *l = &frame->glayout;
	tab_group_layout_compute(&m, &in, l);

#define X(v) ((int)lroundf((float)(v) * scale))
	int dw = X(l->width), dh = X(t->title_height);
	struct twm_pixbuf *buf = pixbuf_create(dw, dh);
	if (!buf) {
		return;
	}
	pixman_image_t *img = buf->image;
	int stroke = draw_stroke(scale);
	int glyph_off = (t->title_height - 12) / 2;
	/* GRP-3: each button takes the colors of the tab beside it. */
	int first = -1, last = -1;
	for (int i = 0; i < l->ntabs; i++) {
		if (l->tabs[i].index >= 0) {
			last = l->tabs[i].index;
			if (first < 0) {
				first = last;
			}
		}
	}
	struct tab_colors cf = colors[first >= 0 ? first : 0];
	struct tab_colors cl = colors[last >= 0 ? last : 0];
	draw_fill(img, cf.bg, 0, 0, X(l->sep_left.x + 1), dh);
	draw_glyph(img, TWM_GLYPH_ICONIFY, X(l->iconify.x + glyph_off), X(glyph_off), scale, cf.fg);
	draw_fill(img, cf.sep, X(l->sep_left.x), 0, stroke, dh);
	for (int k = 0; k < l->ntabs; k++) {
		const struct tab_group_span *s = &l->tabs[k];
		if (s->index < 0) {
			/* GRP-2: the drop slot, a dashed accent box. */
			uint32_t a = rgba_argb32(rgba_hex(t->accent, 1));
			for (int i = 0; i < s->width; i += 6) {
				int len = i + 3 <= s->width ? 3 : s->width - i;
				draw_fill(img, a, X(s->x + i), 0, X(len), stroke);
				draw_fill(img, a, X(s->x + i), dh - stroke, X(len), stroke);
			}
			for (int i = 0; i < t->title_height; i += 6) {
				int len = i + 3 <= t->title_height ? 3 : t->title_height - i;
				draw_fill(img, a, X(s->x), X(i), stroke, X(len));
				draw_fill(img, a, X(s->x + s->width) - stroke, X(i), stroke, X(len));
			}
			continue;
		}
		struct tab_colors *c = &colors[s->index];
		struct twm_font *f = title_font(server, c->weight, scale);
		draw_fill(img, c->bg, X(s->x), 0, X(s->x + s->width) - X(s->x), dh);
		if (c->has_outline) {
			draw_box(img, c->outline, X(s->x), 0, X(s->x + s->width) - X(s->x), dh, stroke);
		}
		text_draw(img, f, view_title(members[s->index]), X(s->text_x),
			text_baseline_centered(f, 0, dh), c->fg, 0, X(s->text_w));
		if (s->hatch_w) {
			int inset = t->hatch_inset;
			draw_hatch(img, X(s->hatch_x), X(inset), X(s->hatch_w), X(t->title_height - inset) - X(inset),
				scale, c->hatch);
		}
		draw_fill(img, c->sep, X(s->sep_x), 0, stroke, dh);
	}
	if (l->resize.width) {
		draw_fill(img, cl.bg, X(l->resize.x), 0, X(l->resize.width), dh);
		draw_glyph(img, TWM_GLYPH_RESIZE, X(l->resize.x + glyph_off), X(glyph_off), scale, cl.fg);
	}
	if (view->hint[0]) {
		draw_hint_box(img, view, l->iconify.x, scale);
	}
#undef X
	wlr_scene_buffer_set_buffer(frame->tab, &buf->base);
	pixbuf_drop(buf);
	wlr_scene_buffer_set_dest_size(frame->tab, l->width, t->title_height);
	wlr_scene_node_set_position(&frame->tab->node, l->x, 0);
}

static void draw_tab(struct twm_frame *frame, const char *title, enum frame_state state,
		float scale) {
	struct twm_server *server = frame->view->server;
	const struct twm_theme *t = &server->theme;
	struct tab_colors c = frame->view->hint_selected ? hint_selected_colors(t) :
		colors_for(t, state);
	struct twm_font *font = title_font(server, c.weight, scale);
	struct tab_metrics m = metrics_for(frame, font, scale);
	float tracking = c.tracking * scale;

	struct tab_input in = {
		.frame_width = frame->view->width + 2 * t->border_width,
		.text_width = (int)ceilf(text_width(font, title, tracking) / scale),
		.hatch = (state == FRAME_FOCUSED && t->title_highlight) || frame->view->trusted,
		.iconify = !frame->view->utility,
		.resize = !frame->view->utility && !frame->view->trusted && view_resizable(frame->view),
		.justify = config_justify(server->config, frame->view->impl->get_app_id(frame->view),
			frame->view->impl->get_title(frame->view)), /* FRM-4 */
		.marker_width = frame->view->capturing ? marker_width(server, scale) : 0,
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
		/* TRU-2: the lock takes the iconify slot; nothing else draws it. */
		draw_glyph(img, frame->view->trusted ? TWM_GLYPH_LOCK : TWM_GLYPH_ICONIFY,
			X(l->iconify.x + glyph_off), X(glyph_off), scale, c.fg);
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
	if (l->marker.width) {
		draw_marker_segment(img, server, l->marker.x, l->marker.width, scale);
	}
	if (frame->view->hint[0] && l->iconify.width) {
		draw_hint_box(img, frame->view, l->iconify.x, scale);
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
		view_title_height(frame->view) + e[edge].y);
	wlr_scene_node_set_enabled(&frame->dash[edge]->node, true);
}

static void update_border(struct twm_frame *frame, enum frame_state state, float scale) {
	struct twm_view *view = frame->view;
	const struct twm_theme *t = &view->server->theme;
	int bw = t->border_width, th = view_title_height(view);
	int w = view->width + 2 * bw, h = view->height + 2 * bw;
	if (frame->drawn_border.valid && frame->drawn_border.state == state &&
			frame->drawn_border.width == w && frame->drawn_border.height == h &&
			frame->drawn_border.scale == scale && frame->drawn_border.th == th &&
			frame->drawn_border.gen == view->server->theme_gen &&
			frame->drawn_border.squeezed == view->squeezed) {
		return;
	}
	frame->drawn_border.th = th;
	frame->drawn_border.squeezed = view->squeezed;
	frame->drawn_border.gen = view->server->theme_gen;
	frame->drawn_border.valid = true;
	frame->drawn_border.state = state;
	frame->drawn_border.width = w;
	frame->drawn_border.height = h;
	frame->drawn_border.scale = scale;

	bool dashed = state == FRAME_URGENT;
	bool hidden = view->squeezed && th > 0; /* FRM-14: the tab alone */
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
		wlr_scene_node_set_enabled(&frame->border[i]->node, !dashed && !hidden);
		if (dashed && !hidden) {
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

	update_border(frame, view->hint_selected ? FRAME_FOCUSED : state, scale);
	wlr_scene_node_set_enabled(&frame->tab->node, !view->notitle);

	/* Everything the tab shows, so it redraws only when that changes. */
	char sig[2048];
	size_t o = (size_t)snprintf(sig, sizeof(sig), "%s\x1f%s%d%d%d", title, view->hint,
		view->hint_selected, frame->slot_index * 10000 + frame->slot_width, view->capturing);
	bool group = view->group && group_size(view->group) > 1 && view->group->active == view;
	group = group || frame->slot_index >= 0;
	if (group && view->group) {
		o += (size_t)snprintf(sig + o, sizeof(sig) - o, "\x1eG%d", view->group->folded);
		struct twm_view *mbr;
		wl_list_for_each(mbr, &view->group->members, group_link) {
			if (o < sizeof(sig)) {
				o += (size_t)snprintf(sig + o, sizeof(sig) - o, "\x1d%s%d%d",
					view_title(mbr), mbr == view, mbr->urgent);
			}
		}
	}
	if (frame->drawn.valid && frame->drawn.gen == server->theme_gen &&
			frame->drawn.state == state &&
			frame->drawn.scale == scale && frame->drawn.frame_width == fw &&
			frame->drawn.resize == resize && frame->drawn.title &&
			strcmp(frame->drawn.title, sig) == 0) {
		return;
	}
	free(frame->drawn.title);
	frame->drawn.title = strdup(sig);
	frame->is_group = group;
	frame->drawn.state = state;
	frame->drawn.scale = scale;
	frame->drawn.frame_width = fw;
	frame->drawn.resize = resize;
	frame->drawn.gen = server->theme_gen;
	frame->drawn.valid = true;
	if (group) {
		draw_group_tab(frame, state, scale);
	} else {
		draw_tab(frame, title, state, scale);
	}
}

struct twm_frame *frame_create(struct twm_view *view) {
	struct twm_frame *frame = calloc(1, sizeof(*frame));
	if (!frame) {
		return NULL;
	}
	frame->view = view;
	frame->slot_index = -1;
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
	if (frame->is_group) {
		const struct tab_group_layout *g = &frame->glayout;
		int gx = (int)floor(lx) - frame->view->x - g->x;
		if (gx >= g->iconify.x && gx < g->iconify.x + g->iconify.width) {
			return TWM_HIT_ICONIFY;
		}
		if (g->resize.width && gx >= g->resize.x && gx < g->resize.x + g->resize.width) {
			return TWM_HIT_RESIZE;
		}
		return TWM_HIT_TITLE;
	}
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

struct twm_view *frame_member_at(struct twm_frame *frame, double lx) {
	if (!frame->is_group || !frame->view->group) {
		return frame->view;
	}
	int i = tab_group_tab_at(&frame->glayout, (int)floor(lx) - frame->view->x - frame->glayout.x);
	return i >= 0 ? group_member(frame->view->group, i) : NULL;
}

int frame_insert_at(struct twm_frame *frame, double lx) {
	struct twm_view *view = frame->view;
	int n = view->group ? (int)group_size(view->group) : 1;
	if (!frame->is_group) {
		/* A single tab: before it on its left half, else after. */
		int x = (int)floor(lx) - view->x - frame->layout.x;
		return x < frame->layout.text.x + frame->layout.text.width / 2 ? 0 : 1;
	}
	return tab_group_insert_at(&frame->glayout, (int)floor(lx) - view->x - frame->glayout.x, n);
}

void frame_set_slot(struct twm_frame *frame, int index, int width) {
	if (frame->slot_index == index && frame->slot_width == width) {
		return;
	}
	frame->slot_index = index;
	frame->slot_width = width;
	frame_update(frame);
}
