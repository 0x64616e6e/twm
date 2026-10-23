#include "config.h"
#if HAVE_XWAYLAND
#include <stdlib.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/util/log.h>
#include <wlr/xwayland.h>
#include "view.h"

/* GEN-6: XWayland starts lazily, on the first X11 client. */

struct twm_xwayland_view {
	struct twm_view view;
	struct wlr_xwayland_surface *xsurface;

	struct wl_listener destroy;
	struct wl_listener associate;
	struct wl_listener dissociate;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener commit;
	struct wl_listener request_configure;
	struct wl_listener request_activate;
	struct wl_listener set_override_redirect;
};

/* Override-redirect windows (menus, tooltips): no frame, X positions them. */
struct twm_xwayland_unmanaged {
	struct twm_server *server;
	struct wlr_xwayland_surface *xsurface;
	struct wlr_scene_tree *tree;
	struct twm_hit hit;

	struct wl_listener destroy;
	struct wl_listener associate;
	struct wl_listener dissociate;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener request_configure;
	struct wl_listener set_geometry;
	struct wl_listener set_override_redirect;
};

static void create_view(struct twm_server *server, struct wlr_xwayland_surface *xs);
static void create_unmanaged(struct twm_server *server, struct wlr_xwayland_surface *xs);

static struct twm_xwayland_view *xw_view(struct twm_view *view) {
	return wl_container_of(view, (struct twm_xwayland_view *)NULL, view);
}

static const char *xw_get_title(struct twm_view *view) {
	return xw_view(view)->xsurface->title;
}

static const char *xw_get_app_id(struct twm_view *view) {
	return xw_view(view)->xsurface->class;
}

static void xw_set_activated(struct twm_view *view, bool activated) {
	struct wlr_xwayland_surface *xs = xw_view(view)->xsurface;
	if (activated && xs->minimized) {
		wlr_xwayland_surface_set_minimized(xs, false);
	}
	wlr_xwayland_surface_activate(xs, activated);
	if (activated) {
		wlr_xwayland_surface_restack(xs, NULL, XCB_STACK_MODE_ABOVE);
	}
}

static void xw_configure(struct twm_view *view, int x, int y, int w, int h) {
	struct wlr_xwayland_surface *xs = xw_view(view)->xsurface;
	if (w <= 0 || h <= 0) {
		w = xs->width;
		h = xs->height;
	}
	wlr_xwayland_surface_configure(xs, (int16_t)x, (int16_t)y,
		(uint16_t)w, (uint16_t)h);
}

static void xw_close(struct twm_view *view) {
	wlr_xwayland_surface_close(xw_view(view)->xsurface);
}

static struct twm_size_limits xw_get_size_limits(struct twm_view *view) {
	struct twm_size_limits limits = {0};
	xcb_size_hints_t *hints = xw_view(view)->xsurface->size_hints;
	if (!hints) {
		return limits;
	}
	if (hints->flags & XCB_ICCCM_SIZE_HINT_P_MIN_SIZE) {
		limits.min_width = hints->min_width;
		limits.min_height = hints->min_height;
	}
	if (hints->flags & XCB_ICCCM_SIZE_HINT_P_MAX_SIZE) {
		limits.max_width = hints->max_width;
		limits.max_height = hints->max_height;
	}
	return limits;
}

static const struct twm_view_impl xw_impl = {
	.get_title = xw_get_title,
	.get_app_id = xw_get_app_id,
	.set_activated = xw_set_activated,
	.configure = xw_configure,
	.close = xw_close,
	.get_size_limits = xw_get_size_limits,
};

static void view_handle_map(struct wl_listener *listener, void *data) {
	struct twm_xwayland_view *xv = wl_container_of(listener, xv, map);
	struct wlr_xwayland_surface *xs = xv->xsurface;
	xv->view.surface = xs->surface;
	view_set_content(&xv->view,
		wlr_scene_subsurface_tree_create(xv->view.tree, xs->surface));
	view_update_geometry(&xv->view, 0, 0, xs->width, xs->height);
	view_map(&xv->view);
}

static void view_handle_unmap(struct wl_listener *listener, void *data) {
	struct twm_xwayland_view *xv = wl_container_of(listener, xv, unmap);
	view_unmap(&xv->view);
	if (xv->view.content) {
		wlr_scene_node_destroy(&xv->view.content->node);
		xv->view.content = NULL;
	}
}

static void view_handle_commit(struct wl_listener *listener, void *data) {
	struct twm_xwayland_view *xv = wl_container_of(listener, xv, commit);
	struct wlr_surface *surface = xv->xsurface->surface;
	if (surface->current.width != xv->view.width ||
			surface->current.height != xv->view.height) {
		view_update_geometry(&xv->view, 0, 0,
			surface->current.width, surface->current.height);
	}
}

static void view_handle_associate(struct wl_listener *listener, void *data) {
	struct twm_xwayland_view *xv = wl_container_of(listener, xv, associate);
	struct wlr_surface *surface = xv->xsurface->surface;
	xv->map.notify = view_handle_map;
	wl_signal_add(&surface->events.map, &xv->map);
	xv->unmap.notify = view_handle_unmap;
	wl_signal_add(&surface->events.unmap, &xv->unmap);
	xv->commit.notify = view_handle_commit;
	wl_signal_add(&surface->events.commit, &xv->commit);
}

static void view_handle_dissociate(struct wl_listener *listener, void *data) {
	struct twm_xwayland_view *xv = wl_container_of(listener, xv, dissociate);
	wl_list_remove(&xv->map.link);
	wl_list_remove(&xv->unmap.link);
	wl_list_remove(&xv->commit.link);
}

static void view_handle_request_configure(struct wl_listener *listener, void *data) {
	struct twm_xwayland_view *xv = wl_container_of(listener, xv, request_configure);
	struct wlr_xwayland_surface_configure_event *ev = data;
	if (!xv->view.mapped) {
		/* Before mapping, X clients may place and size themselves. */
		wlr_xwayland_surface_configure(xv->xsurface, ev->x, ev->y, ev->width, ev->height);
		return;
	}
	/* Mapped: honour the size, keep the frame where it is. */
	int dx, dy;
	view_client_offset(&xv->view, &dx, &dy);
	wlr_xwayland_surface_configure(xv->xsurface, (int16_t)(xv->view.x + dx),
		(int16_t)(xv->view.y + dy), ev->width, ev->height);
}

static void view_handle_request_activate(struct wl_listener *listener, void *data) {
	struct twm_xwayland_view *xv = wl_container_of(listener, xv, request_activate);
	/* URG-1 turns this into urgency (M6). Until then, ignore it: a window
	 * never takes focus because it asked. */
	wlr_log(WLR_DEBUG, "X11 window '%s' asked for focus", view_title(&xv->view));
}

static void view_destroy(struct twm_xwayland_view *xv) {
	if (xv->xsurface->surface) {
		view_handle_dissociate(&xv->dissociate, NULL);
	}
	wl_list_remove(&xv->destroy.link);
	wl_list_remove(&xv->associate.link);
	wl_list_remove(&xv->dissociate.link);
	wl_list_remove(&xv->request_configure.link);
	wl_list_remove(&xv->request_activate.link);
	wl_list_remove(&xv->set_override_redirect.link);
	xv->xsurface->data = NULL;
	view_finish(&xv->view);
	free(xv);
}

static void view_handle_destroy(struct wl_listener *listener, void *data) {
	struct twm_xwayland_view *xv = wl_container_of(listener, xv, destroy);
	view_destroy(xv);
}

static void view_handle_set_override_redirect(struct wl_listener *listener, void *data) {
	struct twm_xwayland_view *xv = wl_container_of(listener, xv, set_override_redirect);
	struct twm_server *server = xv->view.server;
	struct wlr_xwayland_surface *xs = xv->xsurface;
	view_destroy(xv);
	create_unmanaged(server, xs);
}

static void create_view(struct twm_server *server, struct wlr_xwayland_surface *xs) {
	struct twm_xwayland_view *xv = calloc(1, sizeof(*xv));
	if (!xv) {
		return;
	}
	xv->xsurface = xs;
	xs->data = xv;
	view_init(&xv->view, server, TWM_VIEW_XWAYLAND, &xw_impl);

	xv->destroy.notify = view_handle_destroy;
	wl_signal_add(&xs->events.destroy, &xv->destroy);
	xv->associate.notify = view_handle_associate;
	wl_signal_add(&xs->events.associate, &xv->associate);
	xv->dissociate.notify = view_handle_dissociate;
	wl_signal_add(&xs->events.dissociate, &xv->dissociate);
	xv->request_configure.notify = view_handle_request_configure;
	wl_signal_add(&xs->events.request_configure, &xv->request_configure);
	xv->request_activate.notify = view_handle_request_activate;
	wl_signal_add(&xs->events.request_activate, &xv->request_activate);
	xv->set_override_redirect.notify = view_handle_set_override_redirect;
	wl_signal_add(&xs->events.set_override_redirect, &xv->set_override_redirect);
	if (xs->surface) {
		view_handle_associate(&xv->associate, NULL);
	}
}

/* --- override-redirect --- */

static void unmanaged_handle_map(struct wl_listener *listener, void *data) {
	struct twm_xwayland_unmanaged *u = wl_container_of(listener, u, map);
	u->tree = wlr_scene_subsurface_tree_create(
		u->server->layers[TWM_LAYER_UNMANAGED], u->xsurface->surface);
	u->tree->node.data = &u->hit;
	wlr_scene_node_set_position(&u->tree->node, u->xsurface->x, u->xsurface->y);
	if (wlr_xwayland_surface_override_redirect_wants_focus(u->xsurface)) {
		struct wlr_seat *seat = u->server->seat;
		wlr_seat_keyboard_notify_enter(seat, u->xsurface->surface, NULL, 0, NULL);
	}
}

static void unmanaged_handle_unmap(struct wl_listener *listener, void *data) {
	struct twm_xwayland_unmanaged *u = wl_container_of(listener, u, unmap);
	if (u->tree) {
		wlr_scene_node_destroy(&u->tree->node);
		u->tree = NULL;
	}
	struct twm_server *server = u->server;
	if (server->seat->keyboard_state.focused_surface == u->xsurface->surface) {
		struct twm_view *focused = server->focused;
		server->focused = NULL;
		view_focus(focused);
	}
}

static void unmanaged_handle_associate(struct wl_listener *listener, void *data) {
	struct twm_xwayland_unmanaged *u = wl_container_of(listener, u, associate);
	u->map.notify = unmanaged_handle_map;
	wl_signal_add(&u->xsurface->surface->events.map, &u->map);
	u->unmap.notify = unmanaged_handle_unmap;
	wl_signal_add(&u->xsurface->surface->events.unmap, &u->unmap);
}

static void unmanaged_handle_dissociate(struct wl_listener *listener, void *data) {
	struct twm_xwayland_unmanaged *u = wl_container_of(listener, u, dissociate);
	wl_list_remove(&u->map.link);
	wl_list_remove(&u->unmap.link);
}

static void unmanaged_handle_request_configure(struct wl_listener *listener, void *data) {
	struct twm_xwayland_unmanaged *u = wl_container_of(listener, u, request_configure);
	struct wlr_xwayland_surface_configure_event *ev = data;
	wlr_xwayland_surface_configure(u->xsurface, ev->x, ev->y, ev->width, ev->height);
}

static void unmanaged_handle_set_geometry(struct wl_listener *listener, void *data) {
	struct twm_xwayland_unmanaged *u = wl_container_of(listener, u, set_geometry);
	if (u->tree) {
		wlr_scene_node_set_position(&u->tree->node, u->xsurface->x, u->xsurface->y);
	}
}

static void unmanaged_destroy(struct twm_xwayland_unmanaged *u) {
	if (u->xsurface->surface) {
		unmanaged_handle_dissociate(&u->dissociate, NULL);
	}
	if (u->tree) {
		wlr_scene_node_destroy(&u->tree->node);
	}
	wl_list_remove(&u->destroy.link);
	wl_list_remove(&u->associate.link);
	wl_list_remove(&u->dissociate.link);
	wl_list_remove(&u->request_configure.link);
	wl_list_remove(&u->set_geometry.link);
	wl_list_remove(&u->set_override_redirect.link);
	u->xsurface->data = NULL;
	free(u);
}

static void unmanaged_handle_destroy(struct wl_listener *listener, void *data) {
	struct twm_xwayland_unmanaged *u = wl_container_of(listener, u, destroy);
	unmanaged_destroy(u);
}

static void unmanaged_handle_set_override_redirect(struct wl_listener *listener, void *data) {
	struct twm_xwayland_unmanaged *u =
		wl_container_of(listener, u, set_override_redirect);
	struct twm_server *server = u->server;
	struct wlr_xwayland_surface *xs = u->xsurface;
	unmanaged_destroy(u);
	create_view(server, xs);
}

static void create_unmanaged(struct twm_server *server, struct wlr_xwayland_surface *xs) {
	struct twm_xwayland_unmanaged *u = calloc(1, sizeof(*u));
	if (!u) {
		return;
	}
	u->server = server;
	u->xsurface = xs;
	u->hit.kind = TWM_HIT_UNMANAGED;
	xs->data = u;

	u->destroy.notify = unmanaged_handle_destroy;
	wl_signal_add(&xs->events.destroy, &u->destroy);
	u->associate.notify = unmanaged_handle_associate;
	wl_signal_add(&xs->events.associate, &u->associate);
	u->dissociate.notify = unmanaged_handle_dissociate;
	wl_signal_add(&xs->events.dissociate, &u->dissociate);
	u->request_configure.notify = unmanaged_handle_request_configure;
	wl_signal_add(&xs->events.request_configure, &u->request_configure);
	u->set_geometry.notify = unmanaged_handle_set_geometry;
	wl_signal_add(&xs->events.set_geometry, &u->set_geometry);
	u->set_override_redirect.notify = unmanaged_handle_set_override_redirect;
	wl_signal_add(&xs->events.set_override_redirect, &u->set_override_redirect);
	if (xs->surface) {
		unmanaged_handle_associate(&u->associate, NULL);
	}
}

/* --- server --- */

static void handle_new_surface(struct wl_listener *listener, void *data) {
	struct twm_server *server = wl_container_of(listener, server, xwayland_new_surface);
	struct wlr_xwayland_surface *xs = data;
	if (xs->override_redirect) {
		create_unmanaged(server, xs);
	} else {
		create_view(server, xs);
	}
}

static void handle_ready(struct wl_listener *listener, void *data) {
	struct twm_server *server = wl_container_of(listener, server, xwayland_ready);
	wlr_xwayland_set_seat(server->xwayland, server->seat);
	wlr_xcursor_manager_load(server->cursor_mgr, 1);
	struct wlr_xcursor *xcursor =
		wlr_xcursor_manager_get_xcursor(server->cursor_mgr, "default", 1);
	if (xcursor) {
		struct wlr_xcursor_image *image = xcursor->images[0];
		wlr_xwayland_set_cursor(server->xwayland, wlr_xcursor_image_get_buffer(image),
			image->hotspot_x, image->hotspot_y);
	}
}

void xwayland_init(struct twm_server *server) {
	server->xwayland = wlr_xwayland_create(server->display, server->compositor, true);
	if (!server->xwayland) {
		wlr_log(WLR_ERROR, "XWayland is not available; X11 clients won't run");
		return;
	}
	server->xwayland_ready.notify = handle_ready;
	wl_signal_add(&server->xwayland->events.ready, &server->xwayland_ready);
	server->xwayland_new_surface.notify = handle_new_surface;
	wl_signal_add(&server->xwayland->events.new_surface, &server->xwayland_new_surface);
	setenv("DISPLAY", server->xwayland->display_name, true);
	wlr_log(WLR_INFO, "XWayland on DISPLAY=%s (starts on first X client)",
		server->xwayland->display_name);
}

void xwayland_finish(struct twm_server *server) {
	if (!server->xwayland) {
		return;
	}
	wl_list_remove(&server->xwayland_ready.link);
	wl_list_remove(&server->xwayland_new_surface.link);
	wlr_xwayland_destroy(server->xwayland);
	server->xwayland = NULL;
}
#endif
