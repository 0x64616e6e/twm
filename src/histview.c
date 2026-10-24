#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "desktop.h"
#include "draw.h"
#include "grab.h"
#include "group.h"
#include "histview.h"
#include "internal.h"
#include "pixbuf.h"
#include "remember.h"
#include "rootline.h"
#include "settings.h"
#include "text.h"
#include "view.h"

/* HIS-3 metrics: B3 scaled by 26/30. */
#define HV_WIDTH 340
#define HV_ROW 33
#define HV_PAD 12
#define HV_OP_COL 66
#define HV_GAP 10
#define HV_TEXT 12
#define HV_FOOT 11
#define HV_FOOT_H 36
#define HV_ROWS 8
#define HV_BAR 3
#define HV_REFRESH_MS 30000

static const char *grab_op_name(enum twm_grab_mode mode) {
	switch (mode) {
	case TWM_GRAB_MOVE:
	case TWM_GRAB_PENDING_MOVE: return "move";
	case TWM_GRAB_RESIZE: return "resize";
	case TWM_GRAB_PLACE:
	case TWM_GRAB_PLACE_SIZE: return "place";
	case TWM_GRAB_TAB: return "ungroup";
	case TWM_GRAB_ICON:
	case TWM_GRAB_NONE: break;
	}
	return NULL;
}

struct row {
	char op[16], target[64], age[16];
	bool pending, undone;
};

static size_t collect(struct twm_server *server, struct row *rows) {
	size_t n = 0;
	const char *pending = grab_op_name(server->grab.mode);
	if (pending && server->grab.mode != TWM_GRAB_PENDING_MOVE && server->grab.view) {
		rows[n] = (struct row){.pending = true};
		snprintf(rows[n].op, sizeof(rows[n].op), "%s", pending);
		snprintf(rows[n].target, sizeof(rows[n].target), "%s", view_title(server->grab.view));
		snprintf(rows[n].age, sizeof(rows[n].age), "pending");
		n++;
	}
	time_t now = time(NULL);
	for (size_t i = 0; n < HV_ROWS; i++) {
		const struct hist_entry *e = history_newest(&server->history, i);
		if (!e) {
			break;
		}
		rows[n] = (struct row){.undone = e->undone};
		snprintf(rows[n].op, sizeof(rows[n].op), "%s", history_op_name(e->op));
		snprintf(rows[n].target, sizeof(rows[n].target), "%s", e->target);
		if (e->undone) {
			snprintf(rows[n].age, sizeof(rows[n].age), "undone");
		} else {
			history_age(now, e->when, rows[n].age, sizeof(rows[n].age));
		}
		n++;
	}
	return n;
}

/* A key bound to `kind`, for the footer (MNU-3's rule). */
static const char *key_for(struct twm_server *server, enum twm_func_kind kind,
		const char *fallback) {
	struct twm_func f = {.kind = kind};
	const struct twm_key_binding *k = config_key_for(server->config, &f);
	return k ? k->spec : fallback;
}

static void render(struct twm_internal_view *iv, float scale, struct twm_pixbuf **out,
		int *lw, int *lh) {
	struct twm_server *server = iv->view.server;
	const struct twm_theme *t = &server->theme;
	struct row rows[HV_ROWS + 1];
	size_t n = collect(server, rows);
	size_t shown = n ? n : 1;
	*lw = HV_WIDTH;
	*lh = (int)shown * HV_ROW + HV_FOOT_H;
#define S(v) ((int)lroundf((float)(v) * scale))
	struct twm_pixbuf *buf = pixbuf_create(S(*lw), S(*lh));
	if (!buf) {
		return;
	}
	pixman_image_t *img = buf->image;
	struct twm_font *font = font_get(t->ui_font, HV_TEXT, TWM_WEIGHT_REGULAR, scale);
	struct twm_font *foot = font_get(t->ui_font, HV_FOOT, TWM_WEIGHT_REGULAR, scale);
	uint32_t c_surface = rgba_argb32(rgba_hex(t->surface, 1));
	draw_fill(img, c_surface, 0, 0, S(*lw), S(*lh));
	int stroke = draw_stroke(scale);

	for (size_t i = 0; i < shown; i++) {
		int y0 = S((int)i * HV_ROW), y1 = S(((int)i + 1) * HV_ROW);
		if (i > 0) {
			draw_fill(img, rgba_argb32(rgba_hex(t->rule_faint, 1)), 0, y0, S(*lw), stroke);
		}
		int base = text_baseline_centered(font, y0, y1 - y0);
		if (n == 0) {
			text_draw(img, font, "nothing yet", S(HV_PAD), base,
				rgba_argb32(rgba_hex(t->dim, 1)), 0, 0);
			break;
		}
		struct row *r = &rows[i];
		uint32_t c_op = t->ink_2, c_target = t->ink, c_age = t->dim;
		if (r->pending) {
			/* B3: the pending row, washed and barred in accent. */
			struct twm_rgba wash = rgba_over(rgba_hex(t->accent, 0.08f), rgba_hex(t->surface, 1));
			draw_fill(img, rgba_argb32(wash), 0, y0 + (i > 0 ? stroke : 0), S(*lw), y1 - y0);
			draw_fill(img, rgba_argb32(rgba_hex(t->accent, 1)), 0, y0, S(HV_BAR), y1 - y0);
			c_op = t->accent;
			c_age = t->muted;
		} else if (r->undone) {
			c_op = c_target = c_age = t->dim;
		}
		int age_w = text_width(font, r->age, 0);
		int tx = S(HV_PAD + HV_OP_COL + HV_GAP);
		text_draw(img, font, r->op, S(HV_PAD), base, rgba_argb32(rgba_hex(c_op, 1)), 0,
			S(HV_OP_COL));
		text_draw(img, font, r->target, tx, base, rgba_argb32(rgba_hex(c_target, 1)), 0,
			S(*lw - HV_PAD) - age_w - S(HV_GAP) - tx);
		text_draw(img, font, r->age, S(*lw - HV_PAD) - age_w, base,
			rgba_argb32(rgba_hex(c_age, 1)), 0, 0);
	}

	/* Footer: "M-z undo   M-S-z redo   per window". */
	int fy0 = S((int)shown * HV_ROW);
	draw_fill(img, rgba_argb32(rgba_hex(t->hairline, 1)), 0, fy0, S(*lw), stroke);
	int base = text_baseline_centered(foot, fy0, S(*lh) - fy0);
	const char *undo_key = key_for(server, TWM_F_UNDO, "M-z");
	const char *redo_key = key_for(server, TWM_F_REDO, "M-S-z");
	const char *parts[][2] = {{undo_key, " undo"}, {redo_key, " redo"}, {NULL, "per window"}};
	int widths[3], total = 0;
	for (int i = 0; i < 3; i++) {
		widths[i] = (parts[i][0] ? text_width(foot, parts[i][0], 0) : 0) +
			text_width(foot, parts[i][1], 0);
		total += widths[i];
	}
	int span = S(*lw - 2 * HV_PAD);
	int gap = (span - total) / 2;
	int x = S(HV_PAD);
	for (int i = 0; i < 3; i++) {
		int cx = x;
		if (parts[i][0]) {
			cx += text_draw(img, foot, parts[i][0], cx, base,
				rgba_argb32(rgba_hex(t->accent, 1)), 0, 0);
		}
		text_draw(img, foot, parts[i][1], cx, base, rgba_argb32(rgba_hex(t->muted, 1)), 0, 0);
		x += widths[i] + gap;
	}
#undef S
	*out = buf;
}

static int handle_refresh(void *data) {
	struct twm_server *server = data;
	histview_changed(server);
	return 0;
}

/* First time: left of the Icon Manager's column (HIS-3). */
static void place(struct twm_internal_view *iv, struct wlr_box out, int *x, int *y) {
	struct wlr_box f = view_frame_box(&iv->view);
	*x = out.x + out.width - 24 - 236 - 24 - f.width;
	*y = out.y + 24;
}

void histview_init(struct twm_server *server) {
	server->histview = internal_view_create(server, "history", "twm-history", render, NULL);
	server->histview->place = place;
	server->histview_timer = wl_event_loop_add_timer(server->loop, handle_refresh, server);
}

void histview_finish(struct twm_server *server) {
	if (server->histview_timer) {
		wl_event_source_remove(server->histview_timer);
		server->histview_timer = NULL;
	}
	internal_view_destroy(server->histview);
	server->histview = NULL;
}

void histview_changed(struct twm_server *server) {
	struct twm_internal_view *iv = server->histview;
	if (!iv || !iv->view.shown) {
		return;
	}
	internal_view_redraw(iv);
	wl_event_source_timer_update(server->histview_timer, HV_REFRESH_MS); /* ages */
}

void histview_toggle(struct twm_server *server) {
	if (!server->histview) {
		return;
	}
	internal_view_toggle(server->histview);
	histview_changed(server);
}

void histview_record(struct twm_server *server, enum hist_op op, struct twm_view *view,
		struct hist_state before, struct hist_state after) {
	if (view->type == TWM_VIEW_INTERNAL) {
		return; /* twm's own windows stay out of the log */
	}
	if (op != HIST_PLACE && hist_state_equal(before, after)) {
		return; /* a click, not a change */
	}
	history_record(&server->history, op, view->id, view_title(view), time(NULL), before, after);
	histview_changed(server);
}

struct hist_state hist_state_of(struct twm_view *view) {
	uint64_t with = 0;
	if (view->group) {
		struct twm_view *m;
		wl_list_for_each(m, &view->group->members, group_link) {
			if (m != view) {
				with = m->id;
				break;
			}
		}
	}
	return (struct hist_state){
		view->x, view->y, view->width, view->height,
		view->desktop, view->iconified, view->squeezed, with,
		view->zoom, view->fullscreen,
		view->normal.x, view->normal.y, view->normal.width, view->normal.height,
	};
}

static void apply(struct twm_server *server, struct twm_view *view, struct hist_state s) {
	view_apply_state(view, s);
	remember_record(server, view);
	histview_changed(server);
}

void histview_undo(struct twm_server *server, struct twm_view *view) {
	if (!view) {
		return;
	}
	const struct hist_entry *e = history_undo(&server->history, view->id);
	if (!e) {
		rootline_post(server, ROOTLINE_INFO, "history", "nothing to undo for %s",
			view_title(view));
		return;
	}
	apply(server, view, e->before);
}

void histview_redo(struct twm_server *server, struct twm_view *view) {
	if (!view) {
		return;
	}
	const struct hist_entry *e = history_redo(&server->history, view->id);
	if (!e) {
		rootline_post(server, ROOTLINE_INFO, "history", "nothing to redo for %s",
			view_title(view));
		return;
	}
	apply(server, view, e->after);
}
