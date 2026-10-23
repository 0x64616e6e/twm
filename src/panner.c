#include <math.h>
#include <stdlib.h>
#include "desktop.h"
#include "draw.h"
#include "group.h"
#include "internal.h"
#include "panner.h"
#include "pixbuf.h"
#include "settings.h"
#include "view.h"

/* DSK-3 metrics (B0). */
#define PN_W 234   /* content; the frame adds 1 px each side */
#define PN_PAD 10
#define PN_GAP 6
#define PN_CELL_H 62
#define PN_COLS 2

struct panner_hit {
	int x0, y0, x1, y1;
	int desktop;
	uint64_t view_id; /* 0 for the cell itself */
};

struct panner {
	struct twm_server *server;
	struct twm_internal_view *iv;
	struct panner_hit hits[256];
	size_t nhits;
	uint64_t dragging; /* B2 on a miniature (DSK-4) */
};

static struct panner *pn_of(struct twm_internal_view *iv) {
	return iv->data;
}

static int cell_w(void) {
	return (PN_W - 2 * PN_PAD - (PN_COLS - 1) * PN_GAP) / PN_COLS;
}

static void cell_origin(int i, int *x, int *y) {
	*x = PN_PAD + (i % PN_COLS) * (cell_w() + PN_GAP);
	*y = PN_PAD + (i / PN_COLS) * (PN_CELL_H + PN_GAP);
}

static void add_hit(struct panner *pn, int x0, int y0, int x1, int y1, int d, uint64_t id) {
	if (pn->nhits < sizeof(pn->hits) / sizeof(pn->hits[0])) {
		pn->hits[pn->nhits++] = (struct panner_hit){x0, y0, x1, y1, d, id};
	}
}

#define S(v) ((int)lroundf((float)(v) * scale))

static void dashed_box(pixman_image_t *img, uint32_t c, int x, int y, int w, int h,
		float scale) {
	int stroke = draw_stroke(scale);
	for (int i = 0; i < w; i += 6) {
		int len = i + 3 <= w ? 3 : w - i;
		draw_fill(img, c, S(x + i), S(y), S(len), stroke);
		draw_fill(img, c, S(x + i), S(y + h) - stroke, S(len), stroke);
	}
	for (int i = 0; i < h; i += 6) {
		int len = i + 3 <= h ? 3 : h - i;
		draw_fill(img, c, S(x), S(y + i), stroke, S(len));
		draw_fill(img, c, S(x + w) - stroke, S(y + i), stroke, S(len));
	}
}

static void render(struct twm_internal_view *iv, float scale, struct twm_pixbuf **out,
		int *lw, int *lh) {
	struct panner *pn = pn_of(iv);
	struct twm_server *server = pn->server;
	const struct twm_theme *t = &server->theme;
	int n = desktop_count(server);
	int rows = (n + PN_COLS - 1) / PN_COLS;
	*lw = PN_W;
	*lh = 2 * PN_PAD + rows * PN_CELL_H + (rows - 1) * PN_GAP;
	struct twm_pixbuf *buf = pixbuf_create(S(*lw), S(*lh));
	if (!buf) {
		return;
	}
	pixman_image_t *img = buf->image;
	draw_fill(img, rgba_argb32(rgba_hex(t->surface, 1)), 0, 0, S(*lw), S(*lh));
	pn->nhits = 0;
	struct twm_output *o = wl_list_empty(&server->outputs) ? NULL :
		wl_container_of(server->outputs.next, o, link);
	if (!o) {
		*out = buf;
		return;
	}
	int cw = cell_w(), stroke = draw_stroke(scale);
	double sx = (double)cw / o->box.width, sy = (double)PN_CELL_H / o->box.height;
	uint32_t c_accent = rgba_argb32(rgba_hex(t->accent, 1));
	uint32_t c_mini = rgba_argb32(rgba_hex(t->mini, 1));
	uint32_t c_hair = rgba_argb32(rgba_hex(t->hairline, 1));
	uint32_t c_wash = rgba_argb32(rgba_over(rgba_hex(t->accent, 0.12f), rgba_hex(t->surface, 1)));

	for (int d = 1; d <= n; d++) {
		int cx, cy;
		cell_origin(d - 1, &cx, &cy);
		bool current = d == server->desktop;
		bool empty = desktop_window_count(server, d) == 0;
		if (current) {
			draw_box(img, c_accent, S(cx), S(cy), S(cw), S(PN_CELL_H), stroke);
		} else if (empty) {
			dashed_box(img, c_hair, cx, cy, cw, PN_CELL_H, scale);
		} else {
			draw_box(img, c_hair, S(cx), S(cy), S(cw), S(PN_CELL_H), stroke);
		}
		add_hit(pn, cx, cy, cx + cw, cy + PN_CELL_H, d, 0);
		/* Miniatures, bottom of the stack first so the top one wins. */
		struct twm_view *v;
		wl_list_for_each_reverse(v, &server->views, link) {
			if (v->type == TWM_VIEW_INTERNAL || v->iconified || v->desktop != d ||
					view_hidden_member(v)) {
				continue;
			}
			struct wlr_box f = view_frame_box(v);
			int mx = cx + (int)lround((f.x - o->box.x) * sx);
			int my = cy + (int)lround((f.y - o->box.y) * sy);
			int mw = (int)lround(f.width * sx), mh = (int)lround(f.height * sy);
			/* Keep it inside the cell. */
			if (mx < cx + 1) { mw -= cx + 1 - mx; mx = cx + 1; }
			if (my < cy + 1) { mh -= cy + 1 - my; my = cy + 1; }
			if (mx + mw > cx + cw - 1) { mw = cx + cw - 1 - mx; }
			if (my + mh > cy + PN_CELL_H - 1) { mh = cy + PN_CELL_H - 1 - my; }
			if (mw < 3 || mh < 3) {
				continue;
			}
			bool focused = server->focused == v;
			draw_fill(img, focused ? c_wash : rgba_argb32(rgba_hex(t->surface, 1)),
				S(mx), S(my), S(mw), S(mh));
			draw_box(img, focused ? c_accent : c_mini, S(mx), S(my), S(mw), S(mh), stroke);
			add_hit(pn, mx, my, mx + mw, my + mh, d, v->id);
		}
	}
	*out = buf;
}

#undef S

static const struct panner_hit *hit_at(struct panner *pn, double x, double y, bool mini) {
	/* Later entries are drawn above earlier ones. */
	for (size_t i = pn->nhits; i-- > 0;) {
		const struct panner_hit *h = &pn->hits[i];
		if (x >= h->x0 && x < h->x1 && y >= h->y0 && y < h->y1 && (!mini || h->view_id)) {
			return h;
		}
	}
	return NULL;
}

static bool on_button(struct twm_internal_view *iv, uint32_t button, bool pressed,
		double x, double y) {
	struct panner *pn = pn_of(iv);
	struct twm_server *server = pn->server;
	if (pressed && button == TWM_BTN_LEFT) {
		const struct panner_hit *h = hit_at(pn, x, y, false);
		if (h) {
			desktop_switch(server, h->desktop); /* DSK-4 */
		}
	} else if (pressed && button == TWM_BTN_MIDDLE) {
		const struct panner_hit *h = hit_at(pn, x, y, true);
		pn->dragging = h ? h->view_id : 0;
	} else if (!pressed && button == TWM_BTN_MIDDLE && pn->dragging) {
		const struct panner_hit *h = hit_at(pn, x, y, false);
		struct twm_view *v;
		wl_list_for_each(v, &server->views, link) {
			if (v->id == pn->dragging && h) {
				desktop_send(server, v, h->desktop); /* DSK-4 */
				break;
			}
		}
		pn->dragging = 0;
	}
	return true;
}

/* DSK-3: bottom-right, 24 px in. */
static void place(struct twm_internal_view *iv, struct wlr_box out, int *x, int *y) {
	struct wlr_box f = view_frame_box(&iv->view);
	*x = out.x + out.width - 24 - f.width;
	*y = out.y + out.height - 24 - f.height;
}

void panner_init(struct twm_server *server) {
	struct panner *pn = calloc(1, sizeof(*pn));
	if (!pn) {
		return;
	}
	pn->server = server;
	pn->iv = internal_view_create(server, "Panner", "twm-panner", render, pn);
	pn->iv->button = on_button;
	pn->iv->place = place;
	server->panner = pn->iv;
}

void panner_finish(struct twm_server *server) {
	if (!server->panner) {
		return;
	}
	struct panner *pn = pn_of(server->panner);
	internal_view_destroy(server->panner);
	server->panner = NULL;
	free(pn);
}

void panner_refresh(struct twm_server *server) {
	struct twm_internal_view *iv = server->panner;
	if (!iv) {
		return;
	}
	bool want = server->config->panner && !wl_list_empty(&server->outputs);
	if (want && !iv->view.shown) {
		internal_view_show(iv);
	} else if (!want && iv->view.shown) {
		internal_view_hide(iv);
	} else if (iv->view.shown) {
		internal_view_redraw(iv);
	}
}
