#include <stdlib.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/util/log.h>
#include "layers.h"
#include "server.h"
#include "ui.h"
#include "view.h"

struct twm_layer_surface {
	struct wl_list link; /* twm_server.layer_surfaces */
	struct twm_server *server;
	struct wlr_layer_surface_v1 *ls;
	struct wlr_scene_layer_surface_v1 *scene;
	struct twm_hit hit;
	bool mapped;

	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener commit;
	struct wl_listener destroy;
};

static enum twm_layer scene_layer_for(enum zwlr_layer_shell_v1_layer layer) {
	switch (layer) {
	case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND: return TWM_LAYER_BACKGROUND;
	case ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM: return TWM_LAYER_BOTTOM;
	case ZWLR_LAYER_SHELL_V1_LAYER_TOP: return TWM_LAYER_TOP;
	case ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY: return TWM_LAYER_LAYER_OVERLAY;
	}
	return TWM_LAYER_TOP;
}

struct wlr_box output_usable(struct twm_output *output) {
	return wlr_box_empty(&output->usable) ? output->box : output->usable;
}

void layers_arrange(struct twm_output *output) {
	struct twm_server *server = output->server;
	struct wlr_box full = output->box, usable = output->box;
	/* Exclusive zones first, top layer down, then everything else. */
	static const enum zwlr_layer_shell_v1_layer order[] = {
		ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, ZWLR_LAYER_SHELL_V1_LAYER_TOP,
		ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM, ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
	};
	for (int exclusive = 1; exclusive >= 0; exclusive--) {
		for (int l = 0; l < 4; l++) {
			struct twm_layer_surface *s;
			wl_list_for_each(s, &server->layer_surfaces, link) {
				struct wlr_layer_surface_v1 *ls = s->ls;
				if (ls->output != output->wlr_output || !ls->initialized ||
						ls->current.layer != order[l] ||
						(ls->current.exclusive_zone > 0) != (exclusive == 1)) {
					continue;
				}
				wlr_scene_layer_surface_v1_configure(s->scene, &full, &usable);
			}
		}
	}
	if (!wlr_box_equal(&usable, &output->usable)) {
		output->usable = usable;
		ui_changed(server);
	}
}

static struct twm_output *output_of(struct twm_layer_surface *s) {
	return s->ls->output ? s->ls->output->data : NULL;
}

static void give_keyboard(struct twm_server *server, struct wlr_surface *surface) {
	if (server->prompt || server->locked) {
		return; /* TRU-4 */
	}
	struct wlr_keyboard *kb = wlr_seat_get_keyboard(server->seat);
	wlr_seat_keyboard_notify_enter(server->seat, surface,
		kb ? kb->keycodes : NULL, kb ? kb->num_keycodes : 0, kb ? &kb->modifiers : NULL);
}

static void return_keyboard(struct twm_server *server) {
	struct twm_view *v = server->focused;
	if (v && v->surface) {
		give_keyboard(server, v->surface);
	} else {
		wlr_seat_keyboard_notify_clear_focus(server->seat);
	}
}

bool layers_hold_keyboard(struct twm_server *server) {
	struct twm_layer_surface *s;
	wl_list_for_each(s, &server->layer_surfaces, link) {
		if (s->mapped && s->ls->current.keyboard_interactive ==
				ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE &&
				s->ls->current.layer >= ZWLR_LAYER_SHELL_V1_LAYER_TOP) {
			return true;
		}
	}
	return false;
}

void layers_pressed(struct twm_server *server, struct wlr_surface *surface) {
	struct twm_layer_surface *s;
	wl_list_for_each(s, &server->layer_surfaces, link) {
		if (s->ls->surface == surface && s->ls->current.keyboard_interactive !=
				ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE) {
			give_keyboard(server, surface);
		}
	}
}

static void handle_map(struct wl_listener *listener, void *data) {
	struct twm_layer_surface *s = wl_container_of(listener, s, map);
	s->mapped = true;
	/* Exclusive zones only count once mapped (wlroots), so arrange again. */
	struct twm_output *o = output_of(s);
	if (o) {
		layers_arrange(o);
	}
	if (s->ls->current.keyboard_interactive ==
			ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE) {
		give_keyboard(s->server, s->ls->surface); /* a launcher or locker */
	}
	input_update_pointer_focus(s->server, 0);
}

static void handle_unmap(struct wl_listener *listener, void *data) {
	struct twm_layer_surface *s = wl_container_of(listener, s, unmap);
	s->mapped = false;
	struct twm_output *o = output_of(s);
	if (o) {
		layers_arrange(o);
	}
	if (s->server->seat->keyboard_state.focused_surface == s->ls->surface) {
		return_keyboard(s->server);
	}
	input_update_pointer_focus(s->server, 0);
}

static void handle_commit(struct wl_listener *listener, void *data) {
	struct twm_layer_surface *s = wl_container_of(listener, s, commit);
	struct wlr_layer_surface_v1 *ls = s->ls;
	if (!ls->initialized) {
		return;
	}
	if (ls->current.committed & WLR_LAYER_SURFACE_V1_STATE_LAYER) {
		wlr_scene_node_reparent(&s->scene->tree->node,
			s->server->layers[scene_layer_for(ls->current.layer)]);
	}
	if (ls->initial_commit || ls->current.committed) {
		struct twm_output *o = output_of(s);
		if (o) {
			layers_arrange(o);
		}
	}
}

static void handle_destroy(struct wl_listener *listener, void *data) {
	struct twm_layer_surface *s = wl_container_of(listener, s, destroy);
	struct twm_server *server = s->server;
	struct twm_output *o = output_of(s);
	wl_list_remove(&s->map.link);
	wl_list_remove(&s->unmap.link);
	wl_list_remove(&s->commit.link);
	wl_list_remove(&s->destroy.link);
	wl_list_remove(&s->link);
	free(s);
	if (o) {
		layers_arrange(o);
	}
	(void)server;
}

static void handle_new_surface(struct wl_listener *listener, void *data) {
	struct twm_server *server = wl_container_of(listener, server, new_layer_surface);
	struct wlr_layer_surface_v1 *ls = data;
	if (!ls->output) {
		struct twm_output *o = output_at(server, server->cursor->x, server->cursor->y);
		if (!o && !wl_list_empty(&server->outputs)) {
			o = wl_container_of(server->outputs.next, o, link);
		}
		if (!o) {
			wlr_layer_surface_v1_destroy(ls);
			return;
		}
		ls->output = o->wlr_output;
	}
	struct twm_layer_surface *s = calloc(1, sizeof(*s));
	if (!s) {
		wlr_layer_surface_v1_destroy(ls);
		return;
	}
	s->server = server;
	s->ls = ls;
	s->hit = (struct twm_hit){.kind = TWM_HIT_LAYER};
	s->scene = wlr_scene_layer_surface_v1_create(
		server->layers[scene_layer_for(ls->pending.layer)], ls);
	s->scene->tree->node.data = &s->hit;
	ls->data = s->scene->tree; /* popups find their parent tree here */
	s->map.notify = handle_map;
	wl_signal_add(&ls->surface->events.map, &s->map);
	s->unmap.notify = handle_unmap;
	wl_signal_add(&ls->surface->events.unmap, &s->unmap);
	s->commit.notify = handle_commit;
	wl_signal_add(&ls->surface->events.commit, &s->commit);
	s->destroy.notify = handle_destroy;
	wl_signal_add(&ls->events.destroy, &s->destroy);
	wl_list_insert(&server->layer_surfaces, &s->link);
}

void layers_init(struct twm_server *server) {
	wl_list_init(&server->layer_surfaces);
	server->layer_shell = wlr_layer_shell_v1_create(server->display, 4);
	server->new_layer_surface.notify = handle_new_surface;
	wl_signal_add(&server->layer_shell->events.new_surface, &server->new_layer_surface);
}

void layers_finish(struct twm_server *server) {
	if (server->layer_shell) {
		wl_list_remove(&server->new_layer_surface.link);
		server->layer_shell = NULL;
	}
}
