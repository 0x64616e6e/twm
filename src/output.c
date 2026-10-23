#include <stdlib.h>
#include <time.h>
#include <wlr/backend.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>
#include "server.h"

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

	struct wlr_output_state state;
	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, true);
	struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
	if (mode) {
		wlr_output_state_set_mode(&state, mode);
	}
	wlr_output_commit_state(wlr_output, &state);
	wlr_output_state_finish(&state);

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
		wlr_output_layout_add_auto(server->output_layout, wlr_output);
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
