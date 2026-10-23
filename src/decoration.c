#include <stdlib.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include "server.h"
#include "xdg.h"

/*
 * FRM-15: server-side decorations, always. xdg-decoration requests are
 * answered with server_side; the KDE protocol (GTK 3) defaults to server.
 */

struct twm_decoration {
	struct wlr_xdg_toplevel_decoration_v1 *deco;
	struct wl_listener request_mode;
	struct wl_listener destroy;
};

static void handle_request_mode(struct wl_listener *listener, void *data) {
	struct twm_decoration *d = wl_container_of(listener, d, request_mode);
	if (d->deco->toplevel->base->initialized) {
		wlr_xdg_toplevel_decoration_v1_set_mode(d->deco,
			WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
	}
	/* Otherwise xdg.c answers on the initial commit. */
}

static void handle_destroy(struct wl_listener *listener, void *data) {
	struct twm_decoration *d = wl_container_of(listener, d, destroy);
	xdg_set_decoration(d->deco->toplevel, NULL);
	wl_list_remove(&d->request_mode.link);
	wl_list_remove(&d->destroy.link);
	free(d);
}

static void handle_new_decoration(struct wl_listener *listener, void *data) {
	struct wlr_xdg_toplevel_decoration_v1 *deco = data;
	struct twm_decoration *d = calloc(1, sizeof(*d));
	if (!d) {
		return;
	}
	d->deco = deco;
	d->request_mode.notify = handle_request_mode;
	wl_signal_add(&deco->events.request_mode, &d->request_mode);
	d->destroy.notify = handle_destroy;
	wl_signal_add(&deco->events.destroy, &d->destroy);
	xdg_set_decoration(deco->toplevel, deco);
	handle_request_mode(&d->request_mode, NULL);
}

void decoration_init(struct twm_server *server) {
	server->xdg_decoration = wlr_xdg_decoration_manager_v1_create(server->display);
	server->new_toplevel_decoration.notify = handle_new_decoration;
	wl_signal_add(&server->xdg_decoration->events.new_toplevel_decoration,
		&server->new_toplevel_decoration);

	struct wlr_server_decoration_manager *kde =
		wlr_server_decoration_manager_create(server->display);
	wlr_server_decoration_manager_set_default_mode(kde,
		WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);
}

void decoration_finish(struct twm_server *server) {
	if (server->xdg_decoration) {
		wl_list_remove(&server->new_toplevel_decoration.link);
		server->xdg_decoration = NULL;
	}
}
