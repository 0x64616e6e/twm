#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output.h>
#include "desktop.h"
#include "grab.h"
#include "group.h"
#include "histview.h"
#include "layers.h"
#include "remember.h"
#include "ui.h"
#include "zoom.h"

/*
 * Zoom and fullscreen (ZOM-*). The window keeps one normal geometry while
 * zoomed or fullscreen; what's on screen is always worked out again from it,
 * the zoom kind and the output, so the states combine without piling up
 * (ZOM-2).
 */

static bool zoomable(struct twm_view *view) {
	return view && view->shown && !view->iconified && !view->utility && !view->trusted &&
		view->type != TWM_VIEW_INTERNAL;
}

static struct twm_output *first_output(struct twm_server *server) {
	if (wl_list_empty(&server->outputs)) {
		return NULL;
	}
	struct twm_output *o = wl_container_of(server->outputs.next, o, link);
	return o;
}

/* The output a box (frame x/y, client size) is mostly on. */
static struct twm_output *output_for_box(struct twm_server *server, struct wlr_box b) {
	int fw, fh;
	view_frame_size(server, b.width, b.height, &fw, &fh);
	struct twm_output *o = output_at(server, b.x + fw / 2, b.y + fh / 2);
	if (!o) {
		o = output_at(server, b.x, b.y);
	}
	if (!o) {
		o = output_at(server, server->cursor->x, server->cursor->y);
	}
	return o ? o : first_output(server);
}

static void clamp_size(struct twm_view *view, int *w, int *h) {
	struct twm_size_limits l = view->impl->get_size_limits(view);
	if (l.max_width > 0 && *w > l.max_width) {
		*w = l.max_width;
	}
	if (l.max_height > 0 && *h > l.max_height) {
		*h = l.max_height;
	}
	if (*w < 1) {
		*w = 1;
	}
	if (*h < 1) {
		*h = 1;
	}
}

/* ZOM-1: the frame position and client size of a zoom, from the normal box. */
static struct wlr_box zoom_box(struct twm_view *view, enum twm_zoom kind) {
	struct twm_server *server = view->server;
	struct wlr_box n = view->normal, z = n;
	struct twm_output *o = output_for_box(server, n);
	if (!o) {
		return n;
	}
	struct wlr_box ua = output_usable(o);
	int bw = server->theme.border_width;
	int deco_w = 2 * bw, deco_h = 2 * bw + view_title_height(view);
	if (kind == TWM_ZOOM_VERT || kind == TWM_ZOOM_FULL) {
		z.y = ua.y;
		z.height = ua.height - deco_h;
	}
	if (kind == TWM_ZOOM_HORIZ || kind == TWM_ZOOM_FULL) {
		z.x = ua.x;
		z.width = ua.width - deco_w;
	}
	clamp_size(view, &z.width, &z.height);
	return z;
}

static void tell_client(struct twm_view *view) {
	if (view->impl->set_state) {
		view->impl->set_state(view, view->zoom, view->fullscreen);
	}
}

void view_update_layer(struct twm_view *view) {
	struct twm_server *server = view->server;
	if (view->type == TWM_VIEW_INTERNAL) {
		return; /* twm's own windows keep their layer */
	}
	enum twm_layer want = view->fullscreen && server->focused == view ?
		TWM_LAYER_FULLSCREEN : TWM_LAYER_WINDOWS;
	struct wlr_scene_tree *parent = server->layers[want];
	if (view->tree->node.parent == parent) {
		return;
	}
	/* reparenting puts it on top of its new layer */
	wlr_scene_node_reparent(&view->tree->node, parent);
	struct twm_view *f = server->focused;
	if (want == TWM_LAYER_WINDOWS && f && f != view && f->tree->node.parent == parent) {
		/* stepping back: just under the window that took the focus */
		wlr_scene_node_place_below(&view->tree->node, &f->tree->node);
	}
}

/* Put the window where its state says, and let everything else know. */
static void apply(struct twm_view *view) {
	struct twm_server *server = view->server;
	if (view->fullscreen) {
		struct wlr_box b = view->fs_box;
		int w = b.width, h = b.height;
		clamp_size(view, &w, &h);
		view_move_resize(view, b.x, b.y, w, h);
	} else if (view->zoom != TWM_ZOOM_NONE) {
		struct wlr_box z = zoom_box(view, view->zoom);
		view_move_resize(view, z.x, z.y, z.width, z.height);
	} else {
		struct wlr_box n = view->normal;
		view_move_resize(view, n.x, n.y, n.width, n.height);
	}
	tell_client(view);
	view_update_layer(view);
	view_update_visibility(view);
	view_update_frame(view);
	remember_record(server, view);
	input_update_pointer_focus(server, 0);
	ui_changed(server);
}

static void save_normal(struct twm_view *view) {
	if (view->zoom == TWM_ZOOM_NONE && !view->fullscreen) {
		view->normal = (struct wlr_box){view->x, view->y, view->width, view->height};
	}
}

static struct twm_view *front_of(struct twm_view *view) {
	/* GRP-7, ZOM-7: a group zooms as one window, by the tab in front */
	return view->group && view->group->active ? view->group->active : view;
}

void view_zoom(struct twm_view *view, enum twm_zoom kind) {
	if (!view) {
		return;
	}
	view = front_of(view);
	if (!zoomable(view)) {
		return;
	}
	struct twm_server *server = view->server;
	grab_view_gone(server, view);
	struct hist_state before = hist_state_of(view);
	save_normal(view);
	if (view->squeezed) {
		view->squeezed = false; /* ZOM-2: it opens to zoom */
	}
	if (view->fullscreen) {
		view->fullscreen = false; /* from fullscreen straight into that zoom */
		view->zoom = kind;
	} else {
		view->zoom = view->zoom == kind ? TWM_ZOOM_NONE : kind;
	}
	apply(view);
	histview_record(server, HIST_ZOOM, view, before, hist_state_of(view));
}

void view_set_fullscreen(struct twm_view *view, bool on, struct twm_output *output) {
	if (!view) {
		return;
	}
	view = front_of(view);
	if (!zoomable(view) || view->fullscreen == on) {
		return;
	}
	struct twm_server *server = view->server;
	grab_view_gone(server, view);
	struct hist_state before = hist_state_of(view);
	if (on) {
		if (!output) {
			struct wlr_box f = view_frame_box(view);
			output = output_at(server, f.x + f.width / 2, f.y + f.height / 2);
			if (!output) {
				output = output_for_box(server, (struct wlr_box){view->x, view->y,
					view->width, view->height});
			}
		}
		if (!output) {
			return;
		}
		save_normal(view);
		view->squeezed = false;
		view->fs_box = output->box;
		view->fullscreen = true;
	} else {
		view->fullscreen = false;
	}
	apply(view);
	histview_record(server, HIST_FULLSCREEN, view, before, hist_state_of(view));
}

void view_end_zoom(struct twm_view *view) {
	if (view->zoom == TWM_ZOOM_NONE) {
		return;
	}
	view->zoom = TWM_ZOOM_NONE;
	tell_client(view);
}

bool zoom_initial_size(struct twm_server *server, bool fullscreen, bool maximized,
		int title_height, int *w, int *h) {
	struct twm_output *o = output_at(server, server->cursor->x, server->cursor->y);
	if (!o) {
		o = first_output(server);
	}
	if (!o || (!fullscreen && !maximized)) {
		return false;
	}
	if (fullscreen) {
		*w = o->box.width;
		*h = o->box.height;
	} else {
		struct wlr_box ua = output_usable(o);
		int bw = server->theme.border_width;
		*w = ua.width - 2 * bw;
		*h = ua.height - 2 * bw - title_height;
	}
	return true;
}

bool zoom_open_requested(struct twm_view *view) {
	struct twm_server *server = view->server;
	struct twm_output *o = output_at(server, server->cursor->x, server->cursor->y);
	if (!o) {
		o = first_output(server);
	}
	if (!o) {
		return false;
	}
	/* Its normal geometry: two thirds of the area, centred (it has none). */
	struct wlr_box ua = output_usable(o);
	int w = ua.width * 2 / 3, h = ua.height * 2 / 3;
	int fw, fh;
	view_frame_size(server, w, h, &fw, &fh);
	view->normal = (struct wlr_box){ua.x + (ua.width - fw) / 2, ua.y + (ua.height - fh) / 2, w, h};
	view->zoom = view->map_zoom;
	view->fullscreen = view->map_fullscreen;
	view->fs_box = o->box;
	view->map_zoom = TWM_ZOOM_NONE;
	view->map_fullscreen = false;
	if (view->fullscreen) {
		int cw = o->box.width, ch = o->box.height;
		clamp_size(view, &cw, &ch);
		view_move_resize(view, o->box.x, o->box.y, cw, ch);
	} else {
		struct wlr_box z = zoom_box(view, view->zoom);
		view_move_resize(view, z.x, z.y, z.width, z.height);
	}
	tell_client(view);
	return true;
}

void zoom_apply_recorded(struct twm_view *view, struct hist_state s) {
	view->zoom = (enum twm_zoom)s.zoom;
	if (s.fullscreen && !view->fullscreen) {
		struct twm_output *o = output_at(view->server, s.x, s.y);
		if (o) {
			view->fs_box = o->box;
		}
	}
	view->fullscreen = s.fullscreen;
	if (s.zoom != TWM_ZOOM_NONE || s.fullscreen) {
		view->normal = (struct wlr_box){s.nx, s.ny, s.nw, s.nh};
	}
	tell_client(view);
	view_update_layer(view);
}

struct wlr_box view_normal_geometry(struct twm_view *view) {
	if (view->zoom != TWM_ZOOM_NONE || view->fullscreen) {
		return view->normal;
	}
	return (struct wlr_box){view->x, view->y, view->width, view->height};
}

void zoom_hand_over(struct twm_view *from, struct twm_view *to) {
	if (from == to) {
		return;
	}
	to->zoom = from->zoom;
	to->fullscreen = from->fullscreen;
	to->normal = from->normal;
	to->fs_box = from->fs_box;
	bool changed = from->zoom != TWM_ZOOM_NONE || from->fullscreen;
	from->zoom = TWM_ZOOM_NONE;
	from->fullscreen = false;
	if (changed) {
		tell_client(from);
		view_update_layer(from);
	}
	tell_client(to);
	view_update_layer(to);
}
