#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_seat.h>
#include "draw.h"
#include "grab.h"
#include "hints.h"
#include "internal.h"
#include "menu.h"
#include "pixbuf.h"
#include "prompt.h"
#include "text.h"
#include "view.h"

/* TRU-1 / TRU-5 metrics: B6 at 26/30, the band kept at 10 px. */
#define PR_W 470       /* content width, band included */
#define PR_BAND 10
#define PR_PAD 21
#define PR_HEAD 15
#define PR_SUB 11
#define PR_LEGEND 10
#define PR_ROW 35
#define PR_ROW_GAP 3
#define PR_RADIO 14
#define PR_TEXT 12
#define PR_BTN_H 38
#define PR_BTN_PAD 17
#define PR_BTN_GAP 9
#define PR_GAP 16

enum pr_hit { PR_HIT_CHOICE, PR_HIT_CHECK, PR_HIT_DENY, PR_HIT_GRANT };

struct twm_prompt {
	struct wl_list link; /* twm_server.prompt_queue */
	struct twm_server *server;
	struct prompt_spec spec;
	struct twm_internal_view *iv;
	struct { int x0, y0, x1, y1; enum pr_hit kind; int index; } hits[PROMPT_MAX_CHOICES + 3];
	int nhits;
	uint64_t restore_focus; /* the window that had focus before */
};

bool prompt_active(struct twm_server *server) {
	return server->prompt != NULL;
}

#define S(v) ((int)lroundf((float)(v) * scale))

static uint32_t C(uint32_t rgb) {
	return rgba_argb32(rgba_hex(rgb, 1));
}

static void add_hit(struct twm_prompt *p, int x0, int y0, int x1, int y1, enum pr_hit k, int i) {
	if (p->nhits < (int)(sizeof(p->hits) / sizeof(p->hits[0]))) {
		p->hits[p->nhits].x0 = x0;
		p->hits[p->nhits].y0 = y0;
		p->hits[p->nhits].x1 = x1;
		p->hits[p->nhits].y1 = y1;
		p->hits[p->nhits].kind = k;
		p->hits[p->nhits].index = i;
		p->nhits++;
	}
}

static int content_height(struct twm_prompt *p) {
	int h = 2 * PR_BAND + 2 + 2 * PR_PAD + 20 + 5 + 14;
	if (p->spec.nchoices) {
		h += PR_GAP + (p->spec.legend[0] ? 18 : 0) +
			p->spec.nchoices * PR_ROW + (p->spec.nchoices - 1) * PR_ROW_GAP;
	}
	if (p->spec.checkbox[0]) {
		h += PR_GAP + 18;
	}
	h += PR_GAP + PR_BTN_H;
	return h;
}

static void render(struct twm_internal_view *iv, float scale, struct twm_pixbuf **out,
		int *lw, int *lh) {
	struct twm_prompt *p = iv->data;
	const struct twm_theme *t = &iv->view.server->theme;
	*lw = PR_W;
	*lh = content_height(p);
	struct twm_pixbuf *buf = pixbuf_create(S(*lw), S(*lh));
	if (!buf) {
		return;
	}
	pixman_image_t *img = buf->image;
	int stroke = draw_stroke(scale);
	p->nhits = 0;
	/* TRU-1: the band (trusted weave), the inner hairline, the content. */
	draw_fill(img, C(t->ground), 0, 0, S(*lw), S(*lh));
	struct twm_rgba dot = rgba_over(rgba_hex(t->ink, 0.32f), rgba_hex(t->ground, 1));
	draw_weave(img, scale, 4, 0, 0, rgba_argb32(dot));
	int ix = PR_BAND, iy = PR_BAND, iw = *lw - 2 * PR_BAND, ih = *lh - 2 * PR_BAND;
	draw_fill(img, C(t->surface), S(ix), S(iy), S(iw), S(ih));
	draw_box(img, C(t->hairline_strong), S(ix), S(iy), S(iw), S(ih), stroke);

	struct twm_font *head = font_get(t->ui_font, PR_HEAD, TWM_WEIGHT_SEMIBOLD, scale);
	struct twm_font *sub = font_get(t->ui_font, PR_SUB, TWM_WEIGHT_REGULAR, scale);
	struct twm_font *legend = font_get(t->ui_font, PR_LEGEND, TWM_WEIGHT_REGULAR, scale);
	struct twm_font *txt = font_get(t->ui_font, PR_TEXT, TWM_WEIGHT_REGULAR, scale);
	struct twm_font *bold = font_get(t->ui_font, PR_TEXT, TWM_WEIGHT_SEMIBOLD, scale);
	int x = ix + 1 + PR_PAD, w = iw - 2 - 2 * PR_PAD;
	int y = iy + 1 + PR_PAD;
	text_draw(img, head, p->spec.heading, S(x), S(y) + head->ascent, C(t->ink), 0, S(w));
	y += 20 + 5;
	/* TRU-6: the client's words, verbatim, never as a heading. */
	text_draw(img, sub, p->spec.client, S(x), S(y) + sub->ascent, C(t->ink_2), 0, S(w));
	y += 14;
	if (p->spec.nchoices) {
		y += PR_GAP;
		if (p->spec.legend[0]) {
			text_draw(img, legend, p->spec.legend, S(x), S(y) + legend->ascent, C(t->muted),
				1.5f * scale, 0);
			y += 18;
		}
		for (int i = 0; i < p->spec.nchoices; i++) {
			bool sel = i == p->spec.selected;
			if (sel) {
				struct twm_rgba wash = rgba_over(rgba_hex(t->accent, 0.08f),
					rgba_hex(t->surface, 1));
				draw_fill(img, rgba_argb32(wash), S(x), S(y), S(w), S(PR_ROW));
			}
			draw_box(img, C(sel ? t->accent : t->hairline), S(x), S(y), S(w), S(PR_ROW), stroke);
			/* A square marker, not a round radio: the capture dot is the
			 * only round shape (CAP-4). */
			int rx = x + 10, ry = y + (PR_ROW - PR_RADIO) / 2;
			draw_box(img, C(sel ? t->accent : t->muted), S(rx), S(ry), S(PR_RADIO),
				S(PR_RADIO), stroke);
			if (sel) {
				draw_fill(img, C(t->accent), S(rx + 4), S(ry + 4), S(PR_RADIO - 8),
					S(PR_RADIO - 8));
			}
			text_draw(img, txt, p->spec.choices[i], S(x + 10 + PR_RADIO + 10),
				text_baseline_centered(txt, S(y), S(PR_ROW)), C(t->ink), 0,
				S(w - 10 - PR_RADIO - 20));
			add_hit(p, x, y, x + w, y + PR_ROW, PR_HIT_CHOICE, i);
			y += PR_ROW + PR_ROW_GAP;
		}
		y -= PR_ROW_GAP;
	}
	if (p->spec.checkbox[0]) {
		y += PR_GAP;
		draw_box(img, C(t->muted), S(x), S(y + 1), S(16), S(16), stroke);
		if (p->spec.checked) {
			draw_fill(img, C(t->accent), S(x + 4), S(y + 5), S(8), S(8));
		}
		text_draw(img, txt, p->spec.checkbox, S(x + 26), text_baseline_centered(txt, S(y), S(18)),
			C(t->ink_2), 0, S(w - 26));
		add_hit(p, x, y, x + w, y + 18, PR_HIT_CHECK, 0);
		y += 18;
	}
	/* TRU-5: Deny outlined, the primary filled, right-aligned. */
	y += PR_GAP;
	int gw = (int)ceilf(text_width(bold, p->spec.grant, 0) / scale) + 2 * PR_BTN_PAD;
	int dw = (int)ceilf(text_width(txt, p->spec.deny, 0) / scale) + 2 * PR_BTN_PAD;
	int gx = x + w - gw, dx = gx - PR_BTN_GAP - dw;
	draw_box(img, C(t->hairline_strong), S(dx), S(y), S(dw), S(PR_BTN_H), stroke);
	text_draw(img, txt, p->spec.deny, S(dx + PR_BTN_PAD), text_baseline_centered(txt, S(y),
		S(PR_BTN_H)), C(t->ink), 0, 0);
	draw_fill(img, C(t->accent), S(gx), S(y), S(gw), S(PR_BTN_H));
	text_draw(img, bold, p->spec.grant, S(gx + PR_BTN_PAD), text_baseline_centered(bold, S(y),
		S(PR_BTN_H)), C(t->on_accent), 0, 0);
	add_hit(p, dx, y, dx + dw, y + PR_BTN_H, PR_HIT_DENY, 0);
	add_hit(p, gx, y, gx + gw, y + PR_BTN_H, PR_HIT_GRANT, 0);
	*out = buf;
}

#undef S

static void update_dim(struct twm_server *server) {
	struct wlr_box all;
	wlr_output_layout_get_box(server->output_layout, NULL, &all);
	if (!server->prompt_dim) {
		float color[4];
		rgba_premul(rgba_hex(server->theme.ground, 0.35f), color); /* TRU-4: one step */
		server->prompt_dim = wlr_scene_rect_create(server->layers[TWM_LAYER_TRUSTED_DIM],
			all.width, all.height, color);
	}
	wlr_scene_rect_set_size(server->prompt_dim, all.width, all.height);
	wlr_scene_node_set_position(&server->prompt_dim->node, all.x, all.y);
	wlr_scene_node_set_enabled(&server->prompt_dim->node, server->prompt != NULL);
}

void prompt_layout(struct twm_server *server) {
	if (server->output_layout) {
		update_dim(server);
	}
}

static void show_next(struct twm_server *server);

static void finish(struct twm_server *server, struct twm_prompt *p, bool granted) {
	struct prompt_spec spec = p->spec;
	uint64_t restore = p->restore_focus;
	wl_list_remove(&p->link);
	if (server->prompt == p) {
		server->prompt = NULL;
	}
	if (p->iv) {
		internal_view_destroy(p->iv);
	}
	free(p);
	update_dim(server);
	/* Focus goes back where it was. */
	struct twm_view *v;
	wl_list_for_each(v, &server->views, link) {
		if (v->id == restore) {
			view_focus(v);
			break;
		}
	}
	input_update_pointer_focus(server, 0);
	grab_resume(server);
	if (spec.done) {
		spec.done(spec.data, granted, spec.selected, spec.checked);
	}
	show_next(server);
}

static void show_next(struct twm_server *server) {
	if (server->prompt || wl_list_empty(&server->prompt_queue)) {
		return;
	}
	struct twm_prompt *p = wl_container_of(server->prompt_queue.prev, p, link);
	/* Nothing else may hold the input (TRU-4). */
	p->restore_focus = server->focused ? server->focused->id : 0;
	server->prompt = p;
	grab_cancel(server);
	menu_close_all(server);
	if (server->selecting) {
		select_cancel(server);
	}
	hints_cancel(server);
	char title[64];
	snprintf(title, sizeof(title), "system \xc2\xb7 %s", p->spec.purpose);
	p->iv = internal_view_create(server, title, "twm-prompt", render, p);
	p->iv->view.utility = false;
	p->iv->view.trusted = true;
	wlr_scene_node_reparent(&p->iv->view.tree->node, server->layers[TWM_LAYER_TRUSTED]);
	internal_view_redraw(p->iv);
	/* Centred on the output under the pointer. */
	struct twm_output *o = output_at(server, server->cursor->x, server->cursor->y);
	if (!o && !wl_list_empty(&server->outputs)) {
		o = wl_container_of(server->outputs.next, o, link);
	}
	p->iv->positioned = true;
	p->iv->view.mapped = true;
	if (o) {
		struct wlr_box f = view_frame_box(&p->iv->view);
		view_move(&p->iv->view, o->box.x + (o->box.width - f.width) / 2,
			o->box.y + (o->box.height - f.height) / 2);
	}
	view_show(&p->iv->view, false);
	view_focus(&p->iv->view);
	update_dim(server);
	wlr_seat_pointer_clear_focus(server->seat);
	wlr_seat_keyboard_notify_clear_focus(server->seat);
}

struct twm_prompt *prompt_open(struct twm_server *server, const struct prompt_spec *spec) {
	struct twm_prompt *p = calloc(1, sizeof(*p));
	if (!p) {
		return NULL;
	}
	p->server = server;
	p->spec = *spec;
	if (p->spec.selected < 0 || p->spec.selected >= p->spec.nchoices) {
		p->spec.selected = 0;
	}
	wl_list_insert(&server->prompt_queue, &p->link);
	show_next(server);
	return p;
}

void prompt_close(struct twm_server *server, struct twm_prompt *p) {
	struct twm_prompt *it;
	wl_list_for_each(it, &server->prompt_queue, link) {
		if (it == p) {
			finish(server, p, false);
			return;
		}
	}
}

void prompt_key(struct twm_server *server, xkb_keysym_t sym) {
	struct twm_prompt *p = server->prompt;
	if (!p) {
		return;
	}
	switch (sym) {
	case XKB_KEY_Escape:
		finish(server, p, false);
		return;
	case XKB_KEY_Return:
	case XKB_KEY_KP_Enter:
		finish(server, p, true);
		return;
	case XKB_KEY_Up:
		if (p->spec.selected > 0) {
			p->spec.selected--;
		}
		break;
	case XKB_KEY_Down:
		if (p->spec.selected + 1 < p->spec.nchoices) {
			p->spec.selected++;
		}
		break;
	case XKB_KEY_space:
		p->spec.checked = !p->spec.checked;
		break;
	default:
		return;
	}
	internal_view_redraw(p->iv);
}

void prompt_pointer_button(struct twm_server *server, uint32_t button, bool pressed) {
	struct twm_prompt *p = server->prompt;
	if (!p || !pressed || button != TWM_BTN_LEFT) {
		return;
	}
	int dx, dy;
	view_client_offset(&p->iv->view, &dx, &dy);
	double lx = server->cursor->x - p->iv->view.x - dx;
	double ly = server->cursor->y - p->iv->view.y - dy;
	for (int i = 0; i < p->nhits; i++) {
		if (lx < p->hits[i].x0 || lx >= p->hits[i].x1 || ly < p->hits[i].y0 ||
				ly >= p->hits[i].y1) {
			continue;
		}
		switch (p->hits[i].kind) {
		case PR_HIT_CHOICE:
			p->spec.selected = p->hits[i].index;
			internal_view_redraw(p->iv);
			return;
		case PR_HIT_CHECK:
			p->spec.checked = !p->spec.checked;
			internal_view_redraw(p->iv);
			return;
		case PR_HIT_DENY:
			finish(server, p, false);
			return;
		case PR_HIT_GRANT:
			finish(server, p, true);
			return;
		}
	}
}

void prompt_init(struct twm_server *server) {
	wl_list_init(&server->prompt_queue);
}

void prompt_finish(struct twm_server *server) {
	while (!wl_list_empty(&server->prompt_queue)) {
		struct twm_prompt *p = wl_container_of(server->prompt_queue.next, p, link);
		finish(server, p, false);
	}
}
