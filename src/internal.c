#include <stdio.h>
#include <stdlib.h>
#include "internal.h"
#include "pixbuf.h"
#include "remember.h"

static struct twm_internal_view *iv_of(struct twm_view *view) {
	return wl_container_of(view, (struct twm_internal_view *)NULL, view);
}

static const char *iv_get_title(struct twm_view *view) {
	return iv_of(view)->title;
}

static const char *iv_get_app_id(struct twm_view *view) {
	return iv_of(view)->app_id;
}

static void iv_set_activated(struct twm_view *view, bool activated) {
}

static void iv_configure(struct twm_view *view, int x, int y, int w, int h) {
	/* The content decides its own size. */
}

static void iv_close(struct twm_view *view) {
	internal_view_hide(iv_of(view));
}

static struct twm_size_limits iv_get_size_limits(struct twm_view *view) {
	return (struct twm_size_limits){view->width, view->height, view->width, view->height};
}

static const struct twm_view_impl internal_impl = {
	.get_title = iv_get_title,
	.get_app_id = iv_get_app_id,
	.set_activated = iv_set_activated,
	.configure = iv_configure,
	.close = iv_close,
	.get_size_limits = iv_get_size_limits,
};

struct twm_internal_view *internal_view_create(struct twm_server *server,
		const char *title, const char *app_id, internal_render_fn render, void *data) {
	struct twm_internal_view *iv = calloc(1, sizeof(*iv));
	if (!iv) {
		return NULL;
	}
	snprintf(iv->title, sizeof(iv->title), "%s", title);
	snprintf(iv->app_id, sizeof(iv->app_id), "%s", app_id);
	iv->render = render;
	iv->data = data;
	view_init(&iv->view, server, TWM_VIEW_INTERNAL, &internal_impl);
	iv->view.utility = true; /* FRM-18: title only, no buttons */
	iv->view.sticky = true;  /* on every desktop */
	struct wlr_scene_tree *content = wlr_scene_tree_create(iv->view.tree);
	iv->buffer = wlr_scene_buffer_create(content, NULL);
	view_set_content(&iv->view, content);
	return iv;
}

void internal_view_destroy(struct twm_internal_view *iv) {
	if (!iv) {
		return;
	}
	view_finish(&iv->view);
	free(iv);
}

void internal_view_redraw(struct twm_internal_view *iv) {
	float scale = output_max_scale(iv->view.server);
	struct twm_pixbuf *buf = NULL;
	int lw = 1, lh = 1;
	iv->render(iv, scale, &buf, &lw, &lh);
	if (!buf) {
		return;
	}
	wlr_scene_buffer_set_buffer(iv->buffer, &buf->base);
	pixbuf_drop(buf);
	wlr_scene_buffer_set_dest_size(iv->buffer, lw, lh);
	if (lw != iv->view.width || lh != iv->view.height) {
		view_update_geometry(&iv->view, 0, 0, lw, lh);
	}
}

void internal_view_show(struct twm_internal_view *iv) {
	struct twm_view *view = &iv->view;
	struct twm_server *server = view->server;
	if (view->shown) {
		return;
	}
	internal_view_redraw(iv);
	view->mapped = true;
	if (iv->positioned) {
		view_show(view, false);
		return;
	}
	iv->positioned = true;
	if (remember_restore(server, view)) {
		return;
	}
	/* First time: the top-right corner, 24 px in (as B0's utility windows). */
	struct twm_output *out = wl_list_empty(&server->outputs) ? NULL :
		wl_container_of(server->outputs.next, out, link);
	if (out) {
		struct wlr_box f = view_frame_box(view);
		view_move(view, out->box.x + out->box.width - 24 - f.width, out->box.y + 24);
	}
	view_show(view, false);
}

void internal_view_hide(struct twm_internal_view *iv) {
	if (iv->view.shown) {
		view_unmap(&iv->view);
	}
}

void internal_view_toggle(struct twm_internal_view *iv) {
	if (iv->view.shown) {
		internal_view_hide(iv);
	} else {
		internal_view_show(iv);
	}
}
