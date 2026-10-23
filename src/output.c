#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wlr/backend.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>
#include "layers.h"
#include "rootline.h"
#include "server.h"
#include "settings.h"
#include "view.h"

/* GEN-3: `Scale` and `Output "name" { scale mode position }`. */
static void configure_output(struct twm_server *server, struct wlr_output *wlr_output,
		bool initial) {
	const struct twm_config *cfg = server->config;
	const struct twm_output_rule *rule = NULL;
	for (size_t i = 0; cfg && i < cfg->noutputs; i++) {
		if (strcmp(cfg->outputs[i].name, wlr_output->name) == 0) {
			rule = &cfg->outputs[i];
		}
	}
	struct wlr_output_state state;
	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, true);
	if (rule && rule->width) {
		struct wlr_output_mode *best = NULL, *m;
		wl_list_for_each(m, &wlr_output->modes, link) {
			if (m->width == rule->width && m->height == rule->height &&
					(!rule->refresh_mhz || abs(m->refresh - rule->refresh_mhz) < 500)) {
				if (!best || m->refresh > best->refresh) {
					best = m;
				}
			}
		}
		if (best) {
			wlr_output_state_set_mode(&state, best);
		} else if (wl_list_empty(&wlr_output->modes)) {
			wlr_output_state_set_custom_mode(&state, rule->width, rule->height,
				rule->refresh_mhz);
		}
	} else if (initial) {
		struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
		if (mode) {
			wlr_output_state_set_mode(&state, mode);
		}
	}
	double scale = rule && rule->scale > 0 ? rule->scale : cfg ? cfg->scale : 0;
	if (scale > 0) {
		wlr_output_state_set_scale(&state, (float)scale);
	} else if (!initial) {
		wlr_output_state_set_scale(&state, 1.0f);
	}
	wlr_output_commit_state(wlr_output, &state);
	wlr_output_state_finish(&state);
	if (rule && rule->has_position) {
		wlr_output_layout_add(server->output_layout, wlr_output, rule->x, rule->y);
	} else if (!initial) {
		wlr_output_layout_add_auto(server->output_layout, wlr_output);
	}
}

void output_apply_config(struct twm_server *server) {
	struct twm_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		configure_output(server, output->wlr_output, false);
	}
	output_arrange(server);
}

static void output_handle_frame(struct wl_listener *listener, void *data) {
	struct twm_output *output = wl_container_of(listener, output, frame);
	struct wlr_scene_output *scene_output =
		wlr_scene_get_scene_output(output->server->scene, output->wlr_output);
	if (!scene_output) {
		return;
	}
	wlr_scene_output_commit(scene_output, NULL);
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	wlr_scene_output_send_frame_done(scene_output, &now);
}

static void output_handle_request_state(struct wl_listener *listener, void *data) {
	struct twm_output *output = wl_container_of(listener, output, request_state);
	const struct wlr_output_event_request_state *event = data;
	wlr_output_commit_state(output->wlr_output, event->state);
}

static void output_handle_destroy(struct wl_listener *listener, void *data) {
	struct twm_output *output = wl_container_of(listener, output, destroy);
	wl_list_remove(&output->frame.link);
	wl_list_remove(&output->request_state.link);
	wl_list_remove(&output->destroy.link);
	wl_list_remove(&output->link);
	if (output->root) {
		wlr_scene_node_destroy(&output->root->node);
	}
	free(output);
}

static void handle_new_output(struct wl_listener *listener, void *data) {
	struct twm_server *server = wl_container_of(listener, server, new_output);
	struct wlr_output *wlr_output = data;

	if (!wlr_output_init_render(wlr_output, server->allocator, server->renderer)) {
		wlr_log(WLR_ERROR, "output %s: cannot init rendering", wlr_output->name);
		return;
	}

	configure_output(server, wlr_output, true);

	struct twm_output *output = calloc(1, sizeof(*output));
	if (!output) {
		return;
	}
	output->server = server;
	output->wlr_output = wlr_output;
	wlr_output->data = output;

	output->frame.notify = output_handle_frame;
	wl_signal_add(&wlr_output->events.frame, &output->frame);
	output->request_state.notify = output_handle_request_state;
	wl_signal_add(&wlr_output->events.request_state, &output->request_state);
	output->destroy.notify = output_handle_destroy;
	wl_signal_add(&wlr_output->events.destroy, &output->destroy);
	wl_list_insert(server->outputs.prev, &output->link);

	struct wlr_output_layout_output *l_output =
		wlr_output_layout_get(server->output_layout, wlr_output);
	if (!l_output) {
		l_output = wlr_output_layout_add_auto(server->output_layout, wlr_output);
	}
	struct wlr_scene_output *scene_output =
		wlr_scene_output_create(server->scene, wlr_output);
	wlr_scene_output_layout_add_output(server->scene_layout, l_output, scene_output);

	wlr_log(WLR_INFO, "output %s: %dx%d scale %.2f", wlr_output->name,
		wlr_output->width, wlr_output->height, wlr_output->scale);
	output_arrange(server);
}

static void handle_layout_change(struct wl_listener *listener, void *data) {
	struct twm_server *server = wl_container_of(listener, server, layout_change);
	output_arrange(server);
}

void output_init(struct twm_server *server) {
	wl_list_init(&server->outputs);
	server->output_layout = wlr_output_layout_create(server->display);
	server->scene_layout =
		wlr_scene_attach_output_layout(server->scene, server->output_layout);
	server->new_output.notify = handle_new_output;
	wl_signal_add(&server->backend->events.new_output, &server->new_output);
	server->layout_change.notify = handle_layout_change;
	wl_signal_add(&server->output_layout->events.change, &server->layout_change);
}

void output_finish(struct twm_server *server) {
	if (!server->output_layout) {
		return;
	}
	wl_list_remove(&server->new_output.link);
	wl_list_remove(&server->layout_change.link);
	/* The scene goes before the backend; outputs must not touch it after. */
	struct twm_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		if (output->root) {
			wlr_scene_node_destroy(&output->root->node);
			output->root = NULL;
		}
	}
	server->output_layout = NULL;
}

void output_arrange(struct twm_server *server) {
	struct twm_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		struct wlr_box box;
		wlr_output_layout_get_box(server->output_layout, output->wlr_output, &box);
		if (wlr_box_empty(&box)) {
			continue;
		}
		bool changed = !wlr_box_equal(&box, &output->box) || !output->root ||
			output->scale != output->wlr_output->scale;
		output->box = box;
		output->scale = output->wlr_output->scale;
		if (changed) {
			root_update(output);
			layers_arrange(output);
		}
	}
	root_update_text(server);
	rootline_update(server);
	/* Frames are drawn at the largest output scale (GEN-3). */
	float scale = output_max_scale(server);
	if (scale != server->frame_scale) {
		server->frame_scale = scale;
		struct twm_view *view;
		wl_list_for_each(view, &server->views, link) {
			view_update_frame(view);
		}
	}
}

struct twm_output *output_at(struct twm_server *server, double lx, double ly) {
	struct wlr_output *wlr_output =
		wlr_output_layout_output_at(server->output_layout, lx, ly);
	return wlr_output ? wlr_output->data : NULL;
}

float output_max_scale(struct twm_server *server) {
	float scale = 1;
	struct twm_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		if (output->wlr_output->scale > scale) {
			scale = output->wlr_output->scale;
		}
	}
	return scale;
}
