#include <stdlib.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_seat.h>
#include "view.h"

void view_init(struct twm_view *view, struct twm_server *server,
		enum twm_view_type type, const struct twm_view_impl *impl) {
	view->server = server;
	view->type = type;
	view->impl = impl;
	view->client_hit = (struct twm_hit){.kind = TWM_HIT_CLIENT, .view = view};
	wl_list_init(&view->link);
	view->tree = wlr_scene_tree_create(server->layers[TWM_LAYER_WINDOWS]);
	wlr_scene_node_set_enabled(&view->tree->node, false);
}

void view_finish(struct twm_view *view) {
	if (view->mapped) {
		view_unmap(view);
	}
	wlr_scene_node_destroy(&view->tree->node);
}

static void position_content(struct twm_view *view) {
	if (!view->content) {
		return;
	}
	int dx, dy;
	view_client_offset(view, &dx, &dy);
	wlr_scene_node_set_position(&view->content->node,
		dx - view->geom_x, dy - view->geom_y);
}

void view_set_content(struct twm_view *view, struct wlr_scene_tree *content) {
	view->content = content;
	if (content) {
		content->node.data = &view->client_hit;
		position_content(view);
	}
}

void view_client_offset(struct twm_view *view, int *dx, int *dy) {
	/* M0 has no frames yet; M1 adds the border and title tab here. */
	*dx = 0;
	*dy = 0;
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

static void initial_position(struct twm_view *view) {
	/* Centered on the output under the pointer. Replaced by click-to-place
	 * (OUT-12) in M1. */
	struct twm_server *server = view->server;
	struct twm_output *output = output_at(server, server->cursor->x, server->cursor->y);
	if (!output && !wl_list_empty(&server->outputs)) {
		output = wl_container_of(server->outputs.next, output, link);
	}
	if (!output) {
		return;
	}
	int dx, dy;
	view_client_offset(view, &dx, &dy);
	int fw = view->width + 2 * dx;
	int fh = view->height + dy + dx;
	view_move(view, output->box.x + (output->box.width - fw) / 2,
		output->box.y + (output->box.height - fh) / 2);
}

void view_map(struct twm_view *view) {
	struct twm_server *server = view->server;
	view->mapped = true;
	wl_list_remove(&view->link);
	wl_list_insert(&server->views, &view->link);
	initial_position(view);
	wlr_scene_node_set_enabled(&view->tree->node, true);
	wlr_scene_node_raise_to_top(&view->tree->node);
	view_focus(view);
	input_update_pointer_focus(server, 0);
}

void view_unmap(struct twm_view *view) {
	struct twm_server *server = view->server;
	view->mapped = false;
	wlr_scene_node_set_enabled(&view->tree->node, false);
	wl_list_remove(&view->link);
	wl_list_init(&view->link);
	if (server->focused == view) {
		server->focused = NULL;
		struct twm_view *next = NULL;
		if (!wl_list_empty(&server->views)) {
			next = wl_container_of(server->views.next, next, link);
		}
		view_focus(next);
	}
	input_update_pointer_focus(server, 0);
}

void view_update_geometry(struct twm_view *view, int gx, int gy,
		int width, int height) {
	view->geom_x = gx;
	view->geom_y = gy;
	view->width = width;
	view->height = height;
	position_content(view);
}

void view_move(struct twm_view *view, int x, int y) {
	view->x = x;
	view->y = y;
	wlr_scene_node_set_position(&view->tree->node, x, y);
	int dx, dy;
	view_client_offset(view, &dx, &dy);
	view->impl->configure(view, x + dx, y + dy, view->width, view->height);
}

void view_raise(struct twm_view *view) {
	struct twm_server *server = view->server;
	wlr_scene_node_raise_to_top(&view->tree->node);
	wl_list_remove(&view->link);
	wl_list_insert(&server->views, &view->link);
}

void view_focus(struct twm_view *view) {
	struct twm_server *server = view ? view->server : NULL;
	if (!view) {
		return;
	}
	struct twm_view *prev = server->focused;
	if (prev == view) {
		return;
	}
	if (prev) {
		prev->impl->set_activated(prev, false);
	}
	server->focused = view;
	view->impl->set_activated(view, true);
	struct wlr_keyboard *kb = wlr_seat_get_keyboard(server->seat);
	if (kb) {
		wlr_seat_keyboard_notify_enter(server->seat, view->surface,
			kb->keycodes, kb->num_keycodes, &kb->modifiers);
	} else {
		wlr_seat_keyboard_notify_enter(server->seat, view->surface, NULL, 0, NULL);
	}
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
