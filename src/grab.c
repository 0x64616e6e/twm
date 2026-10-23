#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/util/log.h>
#include "desktop.h"
#include "frame.h"
#include "grab.h"
#include "group.h"
#include "icons.h"
#include "layers.h"
#include "histview.h"
#include "outline.h"
#include "remember.h"
#include "settings.h"
#include "view.h"

#define DRAG_THRESHOLD 4 /* FRM-12: B1 drag on a title */

bool grab_active(struct twm_server *server) {
	return server->grab.mode != TWM_GRAB_NONE;
}

static struct geo_box frame_geo(struct twm_view *view) {
	struct wlr_box b = view_frame_box(view);
	return (struct geo_box){b.x, b.y, b.width, b.height};
}

/* OUT-10: frame-box limits from the client's and the tab's. */
static struct geo_limits frame_limits(struct twm_view *view) {
	struct twm_server *server = view->server;
	struct twm_size_limits cl = view->impl->get_size_limits(view);
	int deco_w = 2 * server->theme.border_width;
	int deco_h = deco_w + view_title_height(view);
	struct geo_limits lim = {
		.min_w = cl.min_width + deco_w,
		.min_h = (cl.min_height > 1 ? cl.min_height : 1) + deco_h,
		.max_w = cl.max_width > 0 ? cl.max_width + deco_w : 0,
		.max_h = cl.max_height > 0 ? cl.max_height + deco_h : 0,
	};
	int tab_min = view->frame ? frame_min_width(view->frame) : 0;
	if (lim.min_w < tab_min) {
		lim.min_w = tab_min;
	}
	if (lim.max_w && lim.max_w < lim.min_w) {
		lim.max_w = lim.min_w;
	}
	return lim;
}

static const char *hint_for(struct twm_grab *g, char *buf, size_t len) {
	snap_hint(&g->snap, buf, len); /* SNP-5 */
	if (*buf) {
		return buf;
	}
	if (g->mode == TWM_GRAB_PLACE) {
		return "click to place \xc2\xb7 B3 to fit"; /* B0 */
	}
	return NULL;
}

static void output_label(struct twm_server *server, char *buf, size_t len) {
	struct twm_output *o = output_at(server, server->cursor->x, server->cursor->y);
	if (!o) {
		buf[0] = '\0';
		return;
	}
	struct wlr_output *w = o->wlr_output;
	char scale[16];
	snprintf(scale, sizeof(scale), "%.2f", w->scale);
	/* "1.25", "1.5", "1" */
	char *end = scale + strlen(scale) - 1;
	while (*end == '0') {
		*end-- = '\0';
	}
	if (*end == '.') {
		*end = '\0';
	}
	snprintf(buf, len, "%s \xc2\xb7 %d\xc3\x97%d \xc2\xb7 scale %s",
		w->name, w->width, w->height, scale);
}

static bool opaque(struct twm_server *server) {
	struct twm_grab *g = &server->grab;
	return server->config->opaque_move && g->mode == TWM_GRAB_MOVE; /* OUT-11 */
}

static bool show_drop(struct twm_server *server);

static void show_outline(struct twm_server *server) {
	struct twm_grab *g = &server->grab;
	if (g->mode == TWM_GRAB_PENDING_MOVE || g->mode == TWM_GRAB_ICON) {
		return;
	}
	if (show_drop(server)) {
		return;
	}
	if (server->outline) {
		wlr_scene_node_set_enabled(&server->outline->float_tab->node, false);
		wlr_scene_node_set_enabled(&server->outline->drop_label->node, false);
	}
	char olabel[96];
	output_label(server, olabel, sizeof(olabel));
	outline_guides(server, g->output, &g->snap,
		(server->config->snap & TWM_SNAP_THIRDS) && !g->free, olabel);
	if (opaque(server)) {
		view_move(g->view, g->box.x, g->box.y);
		return;
	}
	char hint[128];
	outline_show(server, g->box, view_title(g->view), hint_for(g, hint, sizeof(hint)));
}

/* GRP-1: the frame whose top edge (its title band, across its width) is
 * under the pointer, other than the dragged window's own. */
static struct twm_view *drop_target_at(struct twm_server *server, double px, double py) {
	struct twm_grab *g = &server->grab;
	struct twm_view *v;
	wl_list_for_each(v, &server->views, link) { /* top of the stack first */
		if (!view_visible(v) || v == g->view || v->utility || v->notitle ||
				(g->view->group && v->group == g->view->group)) {
			continue;
		}
		struct wlr_box f = view_frame_box(v);
		int th = server->theme.title_height;
		if (px >= f.x && px < f.x + f.width && py >= f.y && py < f.y + th) {
			return v;
		}
		if (px >= f.x && px < f.x + f.width && py >= f.y && py < f.y + f.height) {
			return NULL; /* over the body of a window in front */
		}
	}
	return NULL;
}

static void set_drop_target(struct twm_server *server, struct twm_view *target, int index) {
	struct twm_grab *g = &server->grab;
	if (g->drop_target && (g->drop_target != target || g->drop_index != index)) {
		frame_set_slot(g->drop_target->frame, -1, 0);
	}
	g->drop_target = target;
	g->drop_index = index;
	if (target) {
		frame_set_slot(target->frame, index, outline_tab_width(server, view_title(g->view)));
	}
}

/* GRP-2: over a target, a floating tab and a slot replace the outline. */
static bool show_drop(struct twm_server *server) {
	struct twm_grab *g = &server->grab;
	bool may = (g->mode == TWM_GRAB_TAB && g->dragged_off) ||
		(g->mode == TWM_GRAB_MOVE && !g->view->group && !g->view->utility);
	struct twm_view *t = may ? drop_target_at(server, server->cursor->x, server->cursor->y) : NULL;
	if (!t) {
		set_drop_target(server, NULL, 0);
		return false;
	}
	set_drop_target(server, t, frame_insert_at(t->frame, server->cursor->x));
	size_t n = t->group ? group_size(t->group) : 1;
	char label[48];
	snprintf(label, sizeof(label), "DROP \xe2\x86\x92 GROUP OF %zu", n + 1);
	outline_drop(server, server->cursor->x, server->cursor->y, view_title(g->view), label);
	return true;
}

/* SNP-1, SNP-7: snap the raw outline unless snapping is off or M is down. */
static void apply_snap(struct twm_server *server, struct geo_limits lim) {
	struct twm_grab *g = &server->grab;
	g->snap = (struct snap_result){.box = g->box};
	struct twm_output *out = output_at(server, server->cursor->x, server->cursor->y);
	if (out) {
		struct wlr_box a = output_usable(out); /* minus bars (GEN-2) */
		g->output = (struct geo_box){a.x, a.y, a.width, a.height};
	}
	const struct twm_config *cfg = server->config;
	if (g->free || !cfg->snap || !out) {
		return;
	}
	size_t n = (size_t)wl_list_length(&server->views);
	struct snap_neighbor *nb = calloc(n ? n : 1, sizeof(*nb));
	size_t k = 0;
	struct twm_view *v;
	wl_list_for_each(v, &server->views, link) {
		if (v == g->view || !view_visible(v)) {
			continue;
		}
		struct wlr_box f = view_frame_box(v);
		nb[k++] = (struct snap_neighbor){{f.x, f.y, f.width, f.height}, view_title(v)};
	}
	struct snap_params p = {.output = g->output, .flags = cfg->snap,
		.distance = cfg->snap_distance};
	if (g->mode == TWM_GRAB_RESIZE || g->mode == TWM_GRAB_PLACE_SIZE) {
		uint32_t edges = g->mode == TWM_GRAB_RESIZE ? g->edges :
			(GEO_EDGE_RIGHT | GEO_EDGE_BOTTOM);
		snap_resize(&p, nb, k, g->box, edges, lim, &g->snap);
	} else {
		snap_move(&p, nb, k, g->box, &g->snap);
	}
	g->box = g->snap.box;
	free(nb);
}

static void begin(struct twm_server *server, struct twm_view *view, uint32_t button,
		enum twm_grab_mode mode) {
	struct twm_grab *g = &server->grab;
	struct wlr_keyboard *kb = wlr_seat_get_keyboard(server->seat);
	*g = (struct twm_grab){
		.mode = mode,
		.view = view,
		.button = button,
		.cx = server->cursor->x,
		.cy = server->cursor->y,
		/* SNP-7: an M already down at the start (M-B1) doesn't count. */
		.mod_held = kb && (wlr_keyboard_get_modifiers(kb) & server->config->modifier),
	};
	g->start = g->box = frame_geo(view);
	g->start_w = view->width;
	g->start_h = view->height;
	/* FOC-8: the pointer belongs to the grab now. */
	wlr_seat_pointer_clear_focus(server->seat);
	focus_cancel_pending(server);
}

void grab_begin_pending_move(struct twm_server *server, struct twm_view *view,
		uint32_t button) {
	begin(server, view, button, TWM_GRAB_PENDING_MOVE);
}

void grab_begin_move(struct twm_server *server, struct twm_view *view, uint32_t button) {
	begin(server, view, button, TWM_GRAB_MOVE);
	histview_changed(server);
	wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "move");
	show_outline(server);
}

void grab_begin_tab(struct twm_server *server, struct twm_view *member, uint32_t button) {
	begin(server, member, button, TWM_GRAB_TAB);
	histview_changed(server);
}

void grab_begin_icon(struct twm_server *server, struct twm_view *view, uint32_t button) {
	if (!view->icon) {
		return;
	}
	begin(server, view, button, TWM_GRAB_ICON);
	server->grab.start = (struct geo_box){view->icon->x, view->icon->y, 0, 0};
}

void grab_begin_resize(struct twm_server *server, struct twm_view *view,
		uint32_t button, uint32_t edges) {
	begin(server, view, button, TWM_GRAB_RESIZE);
	server->grab.edges = edges;
	histview_changed(server);
	wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr,
		wlr_xcursor_get_resize_name((enum wlr_edges)edges));
	show_outline(server);
}

static void start_next_placement(struct twm_server *server);

bool grab_place_enqueue(struct twm_server *server, struct twm_view *view) {
	if (wl_list_empty(&server->outputs)) {
		return false;
	}
	view->placing = true;
	wl_list_insert(server->place_queue.prev, &view->place_link);
	if (!grab_active(server)) {
		start_next_placement(server);
	}
	return true;
}

static void start_next_placement(struct twm_server *server) {
	/* TRU-4: placement waits while a trusted prompt holds the input. */
	if (grab_active(server) || server->prompt || wl_list_empty(&server->place_queue)) {
		return;
	}
	struct twm_view *view = wl_container_of(server->place_queue.next, view, place_link);
	begin(server, view, 0, TWM_GRAB_PLACE);
	grab_motion(server);
	histview_changed(server);
}

static void end_grab(struct twm_server *server) {
	server->grab = (struct twm_grab){0};
	histview_changed(server);
	outline_hide(server);
	wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
	input_update_pointer_focus(server, 0);
	start_next_placement(server);
}

static void finish_placement(struct twm_server *server) {
	struct twm_grab *g = &server->grab;
	struct twm_view *view = g->view;
	wl_list_remove(&view->place_link);
	wl_list_init(&view->place_link);
	view->placing = false;
	int deco_w = 2 * server->theme.border_width;
	int deco_h = deco_w + view_title_height(view);
	int cw = g->box.w - deco_w, ch = g->box.h - deco_h;
	if (cw != view->width || ch != view->height) {
		view_move_resize(view, g->box.x, g->box.y, cw, ch);
	} else {
		view_move(view, g->box.x, g->box.y);
	}
	/* FOC-9: the placing click is the user's intent. */
	view_show(view, true);
}

static void commit(struct twm_server *server) {
	struct twm_grab *g = &server->grab;
	struct twm_view *view = g->view;
	int deco_w = 2 * server->theme.border_width;
	int deco_h = deco_w + view_title_height(view);
	/* HIS-1: what this grab does, before and after. */
	struct hist_state before = hist_state_of(view);
	before.x = g->start.x;
	before.y = g->start.y;
	before.w = g->start_w;
	before.h = g->start_h;
	struct hist_state after = before;
	after.x = g->box.x;
	after.y = g->box.y;
	after.w = g->box.w - deco_w;
	after.h = g->box.h - deco_h;
	enum hist_op op = g->mode == TWM_GRAB_RESIZE ? HIST_RESIZE :
		(g->mode == TWM_GRAB_PLACE || g->mode == TWM_GRAB_PLACE_SIZE) ? HIST_PLACE : HIST_MOVE;
	if (g->mode == TWM_GRAB_MOVE) {
		after.w = g->start_w;
		after.h = g->start_h;
	}
	bool record = g->mode != TWM_GRAB_PENDING_MOVE && g->mode != TWM_GRAB_NONE &&
		g->mode != TWM_GRAB_ICON && g->mode != TWM_GRAB_TAB;
	if (g->drop_target) {
		/* GRP-1: join the group whose edge it was dropped on. */
		struct twm_view *target = g->drop_target;
		int index = g->drop_index;
		set_drop_target(server, NULL, 0);
		struct hist_state hb = hist_state_of(view);
		group_join(view, target, index);
		histview_record(server, HIST_GROUP, view, hb, hist_state_of(view));
		end_grab(server);
		return;
	}
	if (g->mode == TWM_GRAB_TAB) {
		if (g->dragged_off) {
			/* GRP-5: out of the group, at its outline. */
			struct hist_state hb = hist_state_of(view);
			group_leave(view);
			view_move(view, g->box.x, g->box.y);
			view_raise(view);
			view_focus(view);
			remember_record(server, view);
			histview_record(server, HIST_UNGROUP, view, hb, hist_state_of(view));
		}
		end_grab(server);
		return;
	}
	switch (g->mode) {
	case TWM_GRAB_MOVE:
		view_move(view, g->box.x, g->box.y);
		break;
	case TWM_GRAB_RESIZE:
		view_move_resize(view, g->box.x, g->box.y, g->box.w - deco_w, g->box.h - deco_h);
		break;
	case TWM_GRAB_PLACE:
	case TWM_GRAB_PLACE_SIZE:
		finish_placement(server);
		break;
	case TWM_GRAB_PENDING_MOVE:
	case TWM_GRAB_ICON:
	case TWM_GRAB_TAB:
	case TWM_GRAB_NONE:
		break;
	}
	if (g->mode == TWM_GRAB_ICON) {
		end_grab(server);
		return;
	}
	remember_record(server, view); /* CFG-11 */
	if (record) {
		histview_record(server, op, view, before, after);
	}
	end_grab(server);
}

/* OUT-13: grow into the largest empty rectangle around the pointer, then
 * place. Over another window (no room), it places as B1 would. */
static void fit(struct twm_server *server) {
	struct twm_grab *g = &server->grab;
	struct twm_output *out = output_at(server, server->cursor->x, server->cursor->y);
	if (out) {
		size_t n = (size_t)wl_list_length(&server->views);
		struct geo_box *obs = calloc(n ? n : 1, sizeof(*obs));
		size_t k = 0;
		struct twm_view *v;
		wl_list_for_each(v, &server->views, link) {
			if (v != g->view && view_visible(v)) {
				struct wlr_box f = view_frame_box(v);
				obs[k++] = (struct geo_box){f.x, f.y, f.width, f.height};
			}
		}
		struct geo_box area;
		struct wlr_box ua = output_usable(out);
		struct geo_box ob = {ua.x, ua.y, ua.width, ua.height};
		if (snap_fit(ob, obs, k, (int)server->cursor->x, (int)server->cursor->y, &area)) {
			struct geo_limits lim = frame_limits(g->view);
			int w = area.w, h = area.h;
			if (lim.max_w && w > lim.max_w) {
				w = lim.max_w;
			}
			if (lim.max_h && h > lim.max_h) {
				h = lim.max_h;
			}
			if (w >= lim.min_w && h >= lim.min_h) {
				g->box = (struct geo_box){area.x, area.y, w, h};
			}
		}
		free(obs);
	}
	commit(server);
}

static void cancel(struct twm_server *server) {
	struct twm_grab *g = &server->grab;
	if (g->mode == TWM_GRAB_PLACE || g->mode == TWM_GRAB_PLACE_SIZE) {
		/* OUT-12: Esc places it where the outline is. */
		commit(server);
		return;
	}
	if (opaque(server)) {
		view_move(g->view, g->start.x, g->start.y); /* OUT-6: unchanged */
	}
	set_drop_target(server, NULL, 0);
	end_grab(server);
}

void grab_cancel(struct twm_server *server) {
	switch (server->grab.mode) {
	case TWM_GRAB_NONE:
		return;
	case TWM_GRAB_PENDING_MOVE:
		end_grab(server);
		return;
	default:
		cancel(server);
	}
}

void grab_resume(struct twm_server *server) {
	start_next_placement(server);
}

void grab_motion(struct twm_server *server) {
	struct twm_grab *g = &server->grab;
	double dx = server->cursor->x - g->cx;
	double dy = server->cursor->y - g->cy;
	/* Round, don't truncate: an absolute pointer gives 999.9999 for 1000,
	 * and (int) of -99.9999 is -99. */
	int ix = (int)lround(dx) + g->key_dx, iy = (int)lround(dy) + g->key_dy;
	switch (g->mode) {
	case TWM_GRAB_NONE:
		return;
	case TWM_GRAB_ICON:
		/* ROOT-10: icons move directly, no outline. */
		icon_move(g->view, g->start.x + ix, g->start.y + iy);
		return;
	case TWM_GRAB_TAB: {
		/* GRP-5: nothing until it leaves the row; then its outline hangs
		 * from the pointer, as a new window's does (OUT-12). */
		struct wlr_box f = view_frame_box(g->view);
		int th = server->theme.title_height;
		double px = server->cursor->x, py = server->cursor->y;
		if (!g->dragged_off && px >= f.x && px < f.x + f.width && py >= f.y && py < f.y + th) {
			return;
		}
		g->dragged_off = true;
		g->box.x = (int)lround(px);
		g->box.y = (int)lround(py);
		break;
	}
	case TWM_GRAB_PENDING_MOVE:
		if (dx * dx + dy * dy < DRAG_THRESHOLD * DRAG_THRESHOLD) {
			return;
		}
		g->mode = TWM_GRAB_MOVE;
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "move");
		histview_changed(server);
		/* fall through */
	case TWM_GRAB_MOVE:
		g->box.x = g->start.x + ix;
		g->box.y = g->start.y + iy;
		break;
	case TWM_GRAB_RESIZE:
		g->box = geo_resize(g->start, g->edges, ix, iy, frame_limits(g->view));
		break;
	case TWM_GRAB_PLACE:
		/* B0: the outline's top-left hangs from the pointer. */
		g->box.x = (int)lround(server->cursor->x) + g->key_dx;
		g->box.y = (int)lround(server->cursor->y) + g->key_dy;
		break;
	case TWM_GRAB_PLACE_SIZE:
		g->box = geo_resize(g->start, GEO_EDGE_RIGHT | GEO_EDGE_BOTTOM,
			(int)lround(server->cursor->x - (g->start.x + g->start.w)),
			(int)lround(server->cursor->y - (g->start.y + g->start.h)),
			frame_limits(g->view));
		break;
	}
	apply_snap(server, frame_limits(g->view));
	show_outline(server);
}

bool grab_button(struct twm_server *server, uint32_t button, bool pressed) {
	struct twm_grab *g = &server->grab;
	switch (g->mode) {
	case TWM_GRAB_NONE:
		return false;
	case TWM_GRAB_PLACE:
		if (!pressed) {
			return true;
		}
		if (button == TWM_BTN_LEFT) {
			commit(server); /* OUT-12: B1 places */
		} else if (button == TWM_BTN_MIDDLE) {
			/* OUT-12: B2 drags out the size from the top-left (twm). */
			g->mode = TWM_GRAB_PLACE_SIZE;
			g->button = button;
			g->start = g->box;
		} else if (button == TWM_BTN_RIGHT) {
			fit(server); /* OUT-13: B3 fits */
		}
		return true;
	default:
		break;
	}
	if (pressed && g->button == 0 && (g->mode == TWM_GRAB_MOVE ||
			g->mode == TWM_GRAB_RESIZE)) {
		commit(server); /* keyboard or menu-started: a click ends it (MNU-5) */
		return true;
	}
	if (!pressed && button == g->button) {
		if (g->mode == TWM_GRAB_PENDING_MOVE) {
			end_grab(server); /* a click, not a drag: the raise already happened */
		} else {
			commit(server);
		}
	}
	return true;
}

bool grab_key(struct twm_server *server, xkb_keysym_t sym, uint32_t mods) {
	struct twm_grab *g = &server->grab;
	if (g->mode == TWM_GRAB_NONE || g->mode == TWM_GRAB_PENDING_MOVE) {
		return false;
	}
	int step = (mods & WLR_MODIFIER_SHIFT) ? 10 : 1;
	switch (sym) {
	case XKB_KEY_Escape:
		cancel(server);
		return true;
	case XKB_KEY_Return:
	case XKB_KEY_KP_Enter:
		commit(server);
		return true;
	case XKB_KEY_Left:
		g->key_dx -= step;
		break;
	case XKB_KEY_Right:
		g->key_dx += step;
		break;
	case XKB_KEY_Up:
		g->key_dy -= step;
		break;
	case XKB_KEY_Down:
		g->key_dy += step;
		break;
	default:
		return false;
	}
	grab_motion(server);
	return true;
}

void grab_modifiers(struct twm_server *server, uint32_t mods) {
	struct twm_grab *g = &server->grab;
	if (g->mode == TWM_GRAB_NONE) {
		return;
	}
	bool held = (mods & server->config->modifier) != 0;
	bool was_free = g->free;
	if (held && !g->mod_held) {
		g->free = true; /* SNP-7: pressed during the drag */
	} else if (!held) {
		g->free = false;
	}
	g->mod_held = held;
	if (g->free != was_free) {
		grab_motion(server);
	}
}

void grab_view_gone(struct twm_server *server, struct twm_view *view) {
	if (view->placing) {
		wl_list_remove(&view->place_link);
		wl_list_init(&view->place_link);
		view->placing = false;
	}
	if (server->grab.drop_target == view) {
		set_drop_target(server, NULL, 0);
	}
	if (server->grab.view == view) {
		set_drop_target(server, NULL, 0);
		server->grab = (struct twm_grab){0};
		outline_hide(server);
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
		start_next_placement(server);
	}
}
