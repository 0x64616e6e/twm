#include <stdlib.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include "remember.h"
#include "view.h"
#include "xdg.h"

struct twm_xdg_view {
	struct twm_view view;
	struct wlr_xdg_toplevel *toplevel;
	struct wlr_xdg_toplevel_decoration_v1 *decoration;

	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener commit;
	struct wl_listener destroy;
	struct wl_listener request_maximize;
	struct wl_listener request_fullscreen;
	struct wl_listener set_title;
	struct wl_listener set_app_id;
};

struct twm_xdg_popup {
	struct wlr_xdg_popup *popup;
	struct wl_listener commit;
	struct wl_listener destroy;
};

static struct twm_xdg_view *xdg_view(struct twm_view *view) {
	return wl_container_of(view, (struct twm_xdg_view *)NULL, view);
}

static const char *xdg_get_title(struct twm_view *view) {
	return xdg_view(view)->toplevel->title;
}

static const char *xdg_get_app_id(struct twm_view *view) {
	return xdg_view(view)->toplevel->app_id;
}

static void xdg_set_activated(struct twm_view *view, bool activated) {
	wlr_xdg_toplevel_set_activated(xdg_view(view)->toplevel, activated);
}

static void xdg_configure(struct twm_view *view, int x, int y, int w, int h) {
	struct wlr_xdg_toplevel *toplevel = xdg_view(view)->toplevel;
	if (w != view->width || h != view->height) {
		wlr_xdg_toplevel_set_size(toplevel, w, h);
	}
}

static void xdg_close(struct twm_view *view) {
	wlr_xdg_toplevel_send_close(xdg_view(view)->toplevel);
}

static struct twm_size_limits xdg_get_size_limits(struct twm_view *view) {
	struct wlr_xdg_toplevel_state *s = &xdg_view(view)->toplevel->current;
	return (struct twm_size_limits){
		.min_width = s->min_width, .min_height = s->min_height,
		.max_width = s->max_width, .max_height = s->max_height,
	};
}

static struct twm_view *view_of_toplevel(struct wlr_xdg_toplevel *toplevel) {
	struct wlr_scene_tree *content = toplevel ? toplevel->base->data : NULL;
	struct twm_hit *hit = content ? content->node.data : NULL;
	return hit ? hit->view : NULL;
}

static struct twm_view *xdg_get_parent(struct twm_view *view) {
	return view_of_toplevel(xdg_view(view)->toplevel->parent);
}

static const struct twm_view_impl xdg_impl = {
	.get_title = xdg_get_title,
	.get_app_id = xdg_get_app_id,
	.set_activated = xdg_set_activated,
	.configure = xdg_configure,
	.close = xdg_close,
	.get_size_limits = xdg_get_size_limits,
	.get_parent = xdg_get_parent,
};

void xdg_set_decoration(struct wlr_xdg_toplevel *toplevel,
		struct wlr_xdg_toplevel_decoration_v1 *deco) {
	struct twm_view *view = view_of_toplevel(toplevel);
	if (view && view->type == TWM_VIEW_XDG) {
		xdg_view(view)->decoration = deco;
	}
}

static void update_geometry(struct twm_xdg_view *xv) {
	struct wlr_box *geo = &xv->toplevel->base->geometry;
	view_update_geometry(&xv->view, geo->x, geo->y, geo->width, geo->height);
}

static void handle_map(struct wl_listener *listener, void *data) {
	struct twm_xdg_view *xv = wl_container_of(listener, xv, map);
	xv->view.surface = xv->toplevel->base->surface;
	update_geometry(xv);
	view_map(&xv->view);
}

static void handle_unmap(struct wl_listener *listener, void *data) {
	struct twm_xdg_view *xv = wl_container_of(listener, xv, unmap);
	view_unmap(&xv->view);
}

static void handle_commit(struct wl_listener *listener, void *data) {
	struct twm_xdg_view *xv = wl_container_of(listener, xv, commit);
	struct wlr_xdg_surface *base = xv->toplevel->base;
	if (base->initial_commit) {
		/* The size it had last time (OUT-16), else the client's choice. */
		int w = 0, h = 0;
		remember_initial_size(xv->view.server, xv->toplevel->app_id, &w, &h);
		wlr_xdg_toplevel_set_size(xv->toplevel, w, h);
		if (xv->decoration) {
			wlr_xdg_toplevel_decoration_v1_set_mode(xv->decoration,
				WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
		}
		return;
	}
	if (!xv->view.mapped) {
		return;
	}
	struct wlr_box *geo = &base->geometry;
	if (geo->x != xv->view.geom_x || geo->y != xv->view.geom_y ||
			geo->width != xv->view.width || geo->height != xv->view.height) {
		update_geometry(xv);
	} else {
		view_update_frame(&xv->view); /* size limits may have changed (FRM-6) */
	}
}

static void handle_set_title(struct wl_listener *listener, void *data) {
	struct twm_xdg_view *xv = wl_container_of(listener, xv, set_title);
	view_update_frame(&xv->view);
}

static void handle_set_app_id(struct wl_listener *listener, void *data) {
	struct twm_xdg_view *xv = wl_container_of(listener, xv, set_app_id);
	view_update_frame(&xv->view);
}

static void handle_request_state(struct twm_xdg_view *xv) {
	/* twm has no maximize or fullscreen yet; xdg-shell still wants a reply. */
	if (xv->toplevel->base->initialized) {
		wlr_xdg_surface_schedule_configure(xv->toplevel->base);
	}
}

static void handle_request_maximize(struct wl_listener *listener, void *data) {
	struct twm_xdg_view *xv = wl_container_of(listener, xv, request_maximize);
	handle_request_state(xv);
}

static void handle_request_fullscreen(struct wl_listener *listener, void *data) {
	struct twm_xdg_view *xv = wl_container_of(listener, xv, request_fullscreen);
	handle_request_state(xv);
}

static void handle_destroy(struct wl_listener *listener, void *data) {
	struct twm_xdg_view *xv = wl_container_of(listener, xv, destroy);
	wl_list_remove(&xv->map.link);
	wl_list_remove(&xv->unmap.link);
	wl_list_remove(&xv->commit.link);
	wl_list_remove(&xv->destroy.link);
	wl_list_remove(&xv->request_maximize.link);
	wl_list_remove(&xv->request_fullscreen.link);
	wl_list_remove(&xv->set_title.link);
	wl_list_remove(&xv->set_app_id.link);
	view_finish(&xv->view);
	free(xv);
}

static void handle_new_toplevel(struct wl_listener *listener, void *data) {
	struct twm_server *server = wl_container_of(listener, server, new_xdg_toplevel);
	struct wlr_xdg_toplevel *toplevel = data;

	struct twm_xdg_view *xv = calloc(1, sizeof(*xv));
	if (!xv) {
		return;
	}
	xv->toplevel = toplevel;
	view_init(&xv->view, server, TWM_VIEW_XDG, &xdg_impl);
	struct wlr_scene_tree *content =
		wlr_scene_xdg_surface_create(xv->view.tree, toplevel->base);
	view_set_content(&xv->view, content);
	/* Popups find their parent's scene tree here. */
	toplevel->base->data = content;

	xv->map.notify = handle_map;
	wl_signal_add(&toplevel->base->surface->events.map, &xv->map);
	xv->unmap.notify = handle_unmap;
	wl_signal_add(&toplevel->base->surface->events.unmap, &xv->unmap);
	xv->commit.notify = handle_commit;
	wl_signal_add(&toplevel->base->surface->events.commit, &xv->commit);
	xv->destroy.notify = handle_destroy;
	wl_signal_add(&toplevel->events.destroy, &xv->destroy);
	xv->request_maximize.notify = handle_request_maximize;
	wl_signal_add(&toplevel->events.request_maximize, &xv->request_maximize);
	xv->request_fullscreen.notify = handle_request_fullscreen;
	wl_signal_add(&toplevel->events.request_fullscreen, &xv->request_fullscreen);
	xv->set_title.notify = handle_set_title;
	wl_signal_add(&toplevel->events.set_title, &xv->set_title);
	xv->set_app_id.notify = handle_set_app_id;
	wl_signal_add(&toplevel->events.set_app_id, &xv->set_app_id);
}

static void popup_unconstrain(struct twm_xdg_popup *p) {
	struct wlr_xdg_popup *popup = p->popup;
	struct wlr_xdg_surface *root = wlr_xdg_surface_try_from_wlr_surface(
		wlr_surface_get_root_surface(popup->parent));
	if (!root || root->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL || !root->data) {
		return;
	}
	struct wlr_scene_tree *content = root->data;
	struct twm_hit *hit = content->node.data;
	if (!hit || !hit->view) {
		return;
	}
	struct twm_view *view = hit->view;
	struct twm_server *server = view->server;
	int dx, dy;
	view_client_offset(view, &dx, &dy);
	int ox = view->x + dx, oy = view->y + dy;
	struct twm_output *output = output_at(server, ox, oy);
	if (!output) {
		return;
	}
	struct wlr_box box = {
		.x = output->box.x - ox, .y = output->box.y - oy,
		.width = output->box.width, .height = output->box.height,
	};
	wlr_xdg_popup_unconstrain_from_box(popup, &box);
}

static void popup_handle_commit(struct wl_listener *listener, void *data) {
	struct twm_xdg_popup *p = wl_container_of(listener, p, commit);
	if (p->popup->base->initial_commit) {
		popup_unconstrain(p);
		wlr_xdg_surface_schedule_configure(p->popup->base);
	}
}

static void popup_handle_destroy(struct wl_listener *listener, void *data) {
	struct twm_xdg_popup *p = wl_container_of(listener, p, destroy);
	wl_list_remove(&p->commit.link);
	wl_list_remove(&p->destroy.link);
	free(p);
}

static void handle_new_popup(struct wl_listener *listener, void *data) {
	struct wlr_xdg_popup *popup = data;
	struct wlr_xdg_surface *parent = wlr_xdg_surface_try_from_wlr_surface(popup->parent);
	struct wlr_scene_tree *parent_tree = parent ? parent->data : NULL;
	if (!parent_tree) {
		/* A layer-shell surface's popup (a bar's menu). */
		struct wlr_layer_surface_v1 *ls = wlr_layer_surface_v1_try_from_wlr_surface(popup->parent);
		parent_tree = ls ? ls->data : NULL;
	}
	if (!parent_tree) {
		wlr_log(WLR_DEBUG, "popup without a known parent");
		return;
	}
	struct twm_xdg_popup *p = calloc(1, sizeof(*p));
	if (!p) {
		return;
	}
	p->popup = popup;
	popup->base->data = wlr_scene_xdg_surface_create(parent_tree, popup->base);
	p->commit.notify = popup_handle_commit;
	wl_signal_add(&popup->base->surface->events.commit, &p->commit);
	p->destroy.notify = popup_handle_destroy;
	wl_signal_add(&popup->events.destroy, &p->destroy);
}

void xdg_init(struct twm_server *server) {
	server->xdg_shell = wlr_xdg_shell_create(server->display, 6);
	server->new_xdg_toplevel.notify = handle_new_toplevel;
	wl_signal_add(&server->xdg_shell->events.new_toplevel, &server->new_xdg_toplevel);
	server->new_xdg_popup.notify = handle_new_popup;
	wl_signal_add(&server->xdg_shell->events.new_popup, &server->new_xdg_popup);
}

void xdg_finish(struct twm_server *server) {
	if (!server->xdg_shell) {
		return;
	}
	wl_list_remove(&server->new_xdg_toplevel.link);
	wl_list_remove(&server->new_xdg_popup.link);
	server->xdg_shell = NULL;
}
