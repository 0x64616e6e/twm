#include <stdlib.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_seat.h>
#include "desktop.h"
#include "frame.h"
#include "grab.h"
#include "group.h"
#include "hints.h"
#include "icons.h"
#include "internal.h"
#include "layers.h"
#include "ui.h"
#include "remember.h"
#include "settings.h"
#include "view.h"

void view_init(struct twm_view *view, struct twm_server *server,
		enum twm_view_type type, const struct twm_view_impl *impl) {
	view->server = server;
	view->type = type;
	view->impl = impl;
	view->client_hit = (struct twm_hit){.kind = TWM_HIT_CLIENT, .view = view};
	view->id = ++server->view_counter;
	view->desktop = server->desktop > 0 ? server->desktop : 1;
	wl_list_init(&view->link);
	wl_list_init(&view->place_link);
	wl_list_init(&view->group_link);
	view->tree = wlr_scene_tree_create(server->layers[TWM_LAYER_WINDOWS]);
	wlr_scene_node_set_enabled(&view->tree->node, false);
	/* The frame's nodes go first, so the client draws above them. */
	view->frame = frame_create(view);
}

void view_finish(struct twm_view *view) {
	if (view->mapped) {
		view_unmap(view);
	}
	frame_destroy(view->frame);
	view->frame = NULL;
	wlr_scene_node_destroy(&view->tree->node);
}

static void position_content(struct twm_view *view) {
	if (!view->content) {
		return;
	}
	int dx, dy;
	view_client_offset(view, &dx, &dy);
	/* Only the frame's offset: wlroots' scene tree for an xdg surface already
	 * shifts it by its window geometry (a client-side shadow falls outside),
	 * and subtracting geom_x/geom_y again put apps with shadows (Firefox,
	 * Electron) up and to the left of their frames by the shadow's width. */
	wlr_scene_node_set_position(&view->content->node, dx, dy);
}

void view_set_content(struct twm_view *view, struct wlr_scene_tree *content) {
	view->content = content;
	if (content) {
		content->node.data = &view->client_hit;
		position_content(view);
	}
}

int view_title_height(struct twm_view *view) {
	return view->notitle ? 0 : view->server->theme.title_height;
}

void view_client_offset(struct twm_view *view, int *dx, int *dy) {
	const struct twm_theme *t = &view->server->theme;
	*dx = t->border_width;
	*dy = view_title_height(view) + t->border_width;
}

void view_frame_size(struct twm_server *server, int cw, int ch, int *fw, int *fh) {
	const struct twm_theme *t = &server->theme;
	*fw = cw + 2 * t->border_width;
	*fh = ch + 2 * t->border_width + t->title_height;
}

struct wlr_box view_frame_box(struct twm_view *view) {
	const struct twm_theme *t = &view->server->theme;
	int th = view_title_height(view);
	return (struct wlr_box){
		.x = view->x, .y = view->y,
		.width = view->width + 2 * t->border_width,
		/* FRM-14: squeezed, the frame is its tab. */
		.height = view->squeezed && th ? th : view->height + 2 * t->border_width + th,
	};
}

static bool update_notitle(struct twm_view *view) {
	bool notitle = config_notitle(view->server->config,
		view->impl->get_app_id(view), view->impl->get_title(view));
	bool changed = notitle != view->notitle;
	view->notitle = notitle;
	return changed;
}

void view_relayout(struct twm_view *view) {
	update_notitle(view);
	position_content(view);
	/* XWayland clients need to hear where they are now. */
	view_move(view, view->x, view->y);
	view_update_frame(view);
}

const char *view_title(struct twm_view *view) {
	const char *title = view->impl->get_title(view);
	if (title && *title) {
		return title;
	}
	const char *app_id = view->impl->get_app_id(view);
	if (app_id && *app_id) {
		return app_id;
	}
	return "untitled";
}

void view_update_frame(struct twm_view *view) {
	if (view->server->config && update_notitle(view)) {
		position_content(view);
	}
	if (view->frame) {
		frame_update(view->frame);
	}
	/* A hidden tab is drawn in its group's row (title, urgency, marker). */
	if (view->group && view->group->active && view->group->active != view &&
			view->group->active->frame) {
		frame_update(view->group->active->frame);
	}
	ui_changed(view->server);
}

static struct twm_output *output_for_new_view(struct twm_server *server) {
	struct twm_output *output = output_at(server, server->cursor->x, server->cursor->y);
	if (!output && !wl_list_empty(&server->outputs)) {
		output = wl_container_of(server->outputs.next, output, link);
	}
	return output;
}

static void center_in(struct twm_view *view, struct wlr_box area) {
	struct wlr_box f = view_frame_box(view);
	view_move(view, area.x + (area.width - f.width) / 2,
		area.y + (area.height - f.height) / 2);
}

void view_map(struct twm_view *view) {
	struct twm_server *server = view->server;
	view->mapped = true;
	view_update_frame(view);

	/* OUT-15: transients open over their parent, focused if it was. */
	struct twm_view *parent = view->impl->get_parent ? view->impl->get_parent(view) : NULL;
	if (parent && parent->shown) {
		view->desktop = parent->desktop;
		center_in(view, view_frame_box(parent));
		view_show(view, server->focused == parent);
		return;
	}
	/* OUT-15: an X11 window placed by the user keeps its position. */
	int ux, uy;
	if (view->impl->get_user_position && view->impl->get_user_position(view, &ux, &uy)) {
		int dx, dy;
		view_client_offset(view, &dx, &dy);
		view_move(view, ux - dx, uy - dy);
		view_show(view, false);
		return;
	}
	/* OUT-16: back where it was last time. */
	if (remember_restore(server, view)) {
		return;
	}
	/* OUT-12: click-to-place. */
	if (server->config->placement == TWM_PLACE_INTERACTIVE &&
			grab_place_enqueue(server, view)) {
		return;
	}
	/* Placement cascade: down and right from the output's corner, no focus
	 * unless the client brings an activation token (FOC-9). */
	struct twm_output *co = output_for_new_view(server);
	if (server->config->placement == TWM_PLACE_CASCADE && co) {
		int step = server->theme.title_height;
		int n = server->cascade++ % 10;
		struct wlr_box ua = output_usable(co);
		view_move(view, ua.x + 40 + n * step, ua.y + 40 + n * step);
		view_show(view, false);
		return;
	}
	/* No pointer to place with (e.g. no outputs yet): center it, and don't
	 * take focus (FOC-9). */
	struct twm_output *output = output_for_new_view(server);
	if (output) {
		center_in(view, output->box);
	}
	view_show(view, false);
}

void view_show(struct twm_view *view, bool focus) {
	struct twm_server *server = view->server;
	view->shown = true;
	wl_list_remove(&view->link);
	wl_list_insert(&server->views, &view->link);
	view_update_visibility(view);
	wlr_scene_node_raise_to_top(&view->tree->node);
	if (focus) {
		view_focus(view);
	}
	view_update_frame(view);
	input_update_pointer_focus(server, 0);
}

static struct twm_view *next_focus(struct twm_server *server) {
	/* FOC-10: the window under the pointer, else the most recently focused. */
	double sx, sy;
	struct wlr_surface *surface;
	struct twm_hit hit = server_hit_at(server, server->cursor->x, server->cursor->y,
		&surface, &sx, &sy);
	if (hit.view && view_visible(hit.view)) {
		return hit.view;
	}
	struct twm_view *best = NULL, *view;
	wl_list_for_each(view, &server->views, link) {
		if (view_visible(view) && view->type != TWM_VIEW_INTERNAL &&
				(!best || view->focus_seq > best->focus_seq)) {
			best = view;
		}
	}
	return best;
}

void view_unmap(struct twm_view *view) {
	struct twm_server *server = view->server;
	grab_view_gone(server, view);
	remember_record(server, view);
	group_leave(view);
	hints_view_gone(server, view);
	icon_destroy(view);
	view->iconified = false;
	view->mapped = false;
	view->shown = false;
	ui_changed(server);
	wlr_scene_node_set_enabled(&view->tree->node, false);
	wl_list_remove(&view->link);
	wl_list_init(&view->link);
	if (server->focused == view) {
		server->focused = NULL;
		struct twm_view *next = next_focus(server);
		if (next) {
			view_focus(next);
		} else {
			wlr_seat_keyboard_notify_clear_focus(server->seat);
		}
	}
	input_update_pointer_focus(server, 0);
}

void view_update_geometry(struct twm_view *view, int gx, int gy,
		int width, int height) {
	bool resized = width != view->width || height != view->height;
	view->geom_x = gx;
	view->geom_y = gy;
	view->width = width;
	view->height = height;
	position_content(view);
	if (resized) {
		group_sync_geometry(view, view->x, view->y, width, height); /* GRP-3 */
	}
	view_update_frame(view);
}

void view_move(struct twm_view *view, int x, int y) {
	view_move_resize(view, x, y, view->width, view->height);
}

void view_move_resize(struct twm_view *view, int x, int y, int w, int h) {
	view->x = x;
	view->y = y;
	wlr_scene_node_set_position(&view->tree->node, x, y);
	int dx, dy;
	view_client_offset(view, &dx, &dy);
	view->impl->configure(view, x + dx, y + dy, w, h);
	group_sync_geometry(view, x, y, w, h); /* GRP-7 */
}

void view_raise(struct twm_view *view) {
	struct twm_server *server = view->server;
	wlr_scene_node_raise_to_top(&view->tree->node);
	if (view->shown) {
		wl_list_remove(&view->link);
		wl_list_insert(&server->views, &view->link);
	}
}

void view_lower(struct twm_view *view) {
	struct twm_server *server = view->server;
	wlr_scene_node_lower_to_bottom(&view->tree->node);
	if (view->shown) {
		wl_list_remove(&view->link);
		wl_list_insert(server->views.prev, &view->link);
	}
}

void view_focus(struct twm_view *view) {
	if (!view) {
		return;
	}
	struct twm_server *server = view->server;
	struct twm_view *prev = server->focused;
	if (server->prompt && !view->trusted) {
		return; /* TRU-4: the prompt keeps the focus until it closes */
	}
	view->focus_seq = ++server->focus_counter;
	if (prev == view) {
		return;
	}
	if (!view->trusted && layers_hold_keyboard(server)) {
		return; /* a launcher or locker has the keyboard (GEN-2) */
	}
	server->focused = view;
	if (prev) {
		prev->impl->set_activated(prev, false);
		view_update_frame(prev);
		struct twm_internal_view *piv = internal_view_of(prev);
		if (piv && piv->blur) {
			piv->blur(piv);
		}
	}
	view->urgent = false; /* URG-5 */
	view->urgent_held = false;
	view->impl->set_activated(view, true);
	struct wlr_keyboard *kb = wlr_seat_get_keyboard(server->seat);
	if (!view->surface) {
		/* twm's own windows take keys through twm, not a client. */
		wlr_seat_keyboard_notify_clear_focus(server->seat);
	} else if (kb) {
		wlr_seat_keyboard_notify_enter(server->seat, view->surface,
			kb->keycodes, kb->num_keycodes, &kb->modifiers);
	} else {
		wlr_seat_keyboard_notify_enter(server->seat, view->surface, NULL, 0, NULL);
	}
	view_update_frame(view);
	focus_changed(server);
	ui_changed(server);
}

void view_set_urgent(struct twm_view *view, bool urgent) {
	struct twm_server *server = view->server;
	if (urgent && server->focused == view) {
		return;
	}
	if (!urgent) {
		if (view->urgent || view->urgent_held) {
			view->urgent = view->urgent_held = false;
			view_update_frame(view);
		}
		return;
	}
	if (view->urgent || view->urgent_held) {
		return;
	}
	view->urgent_seq = ++server->urgent_counter;
	/* URG-3: a quiet desktop holds its windows' urgency until visited. */
	if (!view->sticky && view->desktop != server->desktop &&
			(server->quiet & (1u << (view->desktop - 1)))) {
		view->urgent_held = true;
	} else {
		view->urgent = true;
	}
	view_update_frame(view);
}

struct twm_view *view_from_surface(struct twm_server *server,
		struct wlr_surface *surface) {
	if (!surface) {
		return NULL;
	}
	struct wlr_surface *root = wlr_surface_get_root_surface(surface);
	struct twm_view *view;
	wl_list_for_each(view, &server->views, link) {
		if (view->surface == root) {
			return view;
		}
	}
	return NULL;
}
