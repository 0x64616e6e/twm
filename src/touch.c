#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/backend.h>
#include <wlr/interfaces/wlr_touch.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_touch.h>
#include <wlr/util/log.h>
#include "grab.h"
#include "hints.h"
#include "layers.h"
#include "menu.h"
#include "settings.h"
#include "view.h"

/*
 * The touchscreen (INP-3 … INP-5). A finger on an app that takes touch is
 * touch, for that app. Everywhere else -- twm's tabs, buttons and windows,
 * the root, an app that takes no touch -- the first finger is the pointer: it
 * moves there, a touch is a B1 press and lifting it the release, so a tap
 * clicks and a drag moves. Only one finger at a time is the pointer.
 */

#define TOUCH_SLOTS 16

struct touch_slot {
	int32_t id;
	bool used;
	double dx, dy; /* layout minus surface coordinates, at the touch */
};

static struct touch_slot slots[TOUCH_SLOTS];

static struct touch_slot *slot_of(int32_t id) {
	for (int i = 0; i < TOUCH_SLOTS; i++) {
		if (slots[i].used && slots[i].id == id) {
			return &slots[i];
		}
	}
	return NULL;
}

static struct touch_slot *slot_new(int32_t id) {
	for (int i = 0; i < TOUCH_SLOTS; i++) {
		if (!slots[i].used) {
			slots[i] = (struct touch_slot){.id = id, .used = true};
			return &slots[i];
		}
	}
	return NULL;
}

bool touch_present(struct twm_server *server) {
	struct twm_device *d;
	wl_list_for_each(d, &server->devices, link) {
		if (d->device->type == WLR_INPUT_DEVICE_TOUCH) {
			return true;
		}
	}
	return false;
}

/* INP-3: the output a touchscreen is on: the one it names (udev's
 * WL_OUTPUT), else the built-in panel, else the only output. */
static struct wlr_output *touch_output(struct twm_server *server, struct wlr_touch *touch) {
	struct twm_output *o, *builtin = NULL, *only = NULL;
	int n = 0;
	wl_list_for_each(o, &server->outputs, link) {
		const char *name = o->wlr_output->name;
		if (touch->output_name && strcmp(touch->output_name, name) == 0) {
			return o->wlr_output;
		}
		if (!builtin && (strncmp(name, "eDP", 3) == 0 || strncmp(name, "LVDS", 4) == 0 ||
				strncmp(name, "DSI", 3) == 0)) {
			builtin = o;
		}
		only = o;
		n++;
	}
	if (builtin) {
		return builtin->wlr_output;
	}
	return n == 1 ? only->wlr_output : NULL;
}

void touch_map(struct twm_server *server) {
	if (!server->cursor) {
		return;
	}
	struct twm_device *d;
	wl_list_for_each(d, &server->devices, link) {
		if (d->device->type == WLR_INPUT_DEVICE_TOUCH) {
			wlr_cursor_map_input_to_output(server->cursor, d->device,
				touch_output(server, wlr_touch_from_input_device(d->device)));
		}
	}
}

/* The pointer, driven by a finger (INP-5). */
static void pointer_down(struct twm_server *server, struct wlr_touch *touch, uint32_t time,
		int32_t id, double x, double y) {
	server->touch_pointer = id;
	wlr_cursor_warp_absolute(server->cursor, &touch->base, x, y);
	input_pointer_moved(server, time);
	input_button(server, &touch->base, time, TWM_BTN_LEFT, true);
}

static void pointer_up(struct twm_server *server, struct wlr_touch *touch, uint32_t time) {
	server->touch_pointer = -1;
	input_button(server, &touch->base, time, TWM_BTN_LEFT, false);
}

/* Does twm itself want this touch: a grab, a menu, select mode, hint mode,
 * a prompt (TRU-4), or anything not an app that takes touch? */
static bool for_twm(struct twm_server *server, double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy, struct twm_hit *hit) {
	*surface = NULL;
	*hit = server_hit_at(server, lx, ly, surface, sx, sy);
	if (server->prompt || grab_active(server) || menu_active(server) || server->selecting ||
			hints_active(server)) {
		return true;
	}
	if (!*surface || (hit->kind != TWM_HIT_CLIENT && hit->kind != TWM_HIT_UNMANAGED &&
			hit->kind != TWM_HIT_LAYER)) {
		return true;
	}
	return !wlr_surface_accepts_touch(*surface, server->seat);
}

static void handle_down(struct wl_listener *listener, void *data) {
	struct twm_server *server = wl_container_of(listener, server, cursor_touch_down);
	struct wlr_touch_down_event *e = data;
	double lx, ly, sx, sy;
	wlr_cursor_absolute_to_layout_coords(server->cursor, &e->touch->base, e->x, e->y, &lx, &ly);
	struct wlr_surface *surface;
	struct twm_hit hit;
	bool twm_own = for_twm(server, lx, ly, &surface, &sx, &sy, &hit);
	wlr_log(WLR_DEBUG, "touch down %d at %.1f,%.1f: %s (hit %d, surface %p)", e->touch_id, lx, ly,
		twm_own ? "twm" : "the app", hit.kind, (void *)surface);
	if (twm_own) {
		if (server->touch_pointer < 0) {
			pointer_down(server, e->touch, e->time_msec, e->touch_id, e->x, e->y);
		}
		return;
	}
	/* INP-4: touch for the app; a touch focuses as a press does (FOC-5). */
	struct touch_slot *slot = slot_new(e->touch_id);
	if (!slot) {
		return;
	}
	slot->dx = lx - sx;
	slot->dy = ly - sy;
	if (hit.kind == TWM_HIT_LAYER) {
		layers_pressed(server, surface);
	} else if (hit.view) {
		view_focus(hit.view);
		if (server->config->raise_on_click) {
			view_raise(hit.view);
		}
	}
	wlr_seat_touch_notify_down(server->seat, surface, e->time_msec, e->touch_id, sx, sy);
}

static void handle_motion(struct wl_listener *listener, void *data) {
	struct twm_server *server = wl_container_of(listener, server, cursor_touch_motion);
	struct wlr_touch_motion_event *e = data;
	if (e->touch_id == server->touch_pointer) {
		wlr_cursor_warp_absolute(server->cursor, &e->touch->base, e->x, e->y);
		input_pointer_moved(server, e->time_msec);
		return;
	}
	struct touch_slot *slot = slot_of(e->touch_id);
	if (!slot) {
		return;
	}
	double lx, ly;
	wlr_cursor_absolute_to_layout_coords(server->cursor, &e->touch->base, e->x, e->y, &lx, &ly);
	wlr_seat_touch_notify_motion(server->seat, e->time_msec, e->touch_id, lx - slot->dx,
		ly - slot->dy);
}

static void handle_up(struct wl_listener *listener, void *data) {
	struct twm_server *server = wl_container_of(listener, server, cursor_touch_up);
	struct wlr_touch_up_event *e = data;
	if (e->touch_id == server->touch_pointer) {
		pointer_up(server, e->touch, e->time_msec);
		return;
	}
	struct touch_slot *slot = slot_of(e->touch_id);
	if (slot) {
		slot->used = false;
		wlr_seat_touch_notify_up(server->seat, e->time_msec, e->touch_id);
	}
}

static void handle_cancel(struct wl_listener *listener, void *data) {
	struct twm_server *server = wl_container_of(listener, server, cursor_touch_cancel);
	struct wlr_touch_cancel_event *e = data;
	if (e->touch_id == server->touch_pointer) {
		pointer_up(server, e->touch, e->time_msec);
		return;
	}
	struct touch_slot *slot = slot_of(e->touch_id);
	if (slot) {
		slot->used = false;
		struct wlr_touch_point *point = wlr_seat_touch_get_point(server->seat, e->touch_id);
		if (point && point->client) {
			wlr_seat_touch_notify_cancel(server->seat, point->client);
		}
	}
}

static void handle_frame(struct wl_listener *listener, void *data) {
	struct twm_server *server = wl_container_of(listener, server, cursor_touch_frame);
	wlr_seat_touch_notify_frame(server->seat);
}

/* --- `--test`: a touchscreen of twm's own, driven over the IPC socket --- */

static const struct wlr_touch_impl test_touch_impl = {.name = "twm-test-touch"};

/*
 * "touch down ID X Y", "touch move ID X Y", "touch up ID" (X, Y in the
 * touchscreen's output, logical px). Only with --test: the events go through
 * the same path a real touchscreen's do, from a device twm made itself.
 */
bool touch_test_request(struct twm_server *server, const char *req, char *reply, size_t len) {
	if (!server->opts.test_mode) {
		snprintf(reply, len, "touch: only with --test\n");
		return false;
	}
	if (!server->test_touch) {
		server->test_touch = calloc(1, sizeof(*server->test_touch));
		if (!server->test_touch) {
			return false;
		}
		wlr_touch_init(server->test_touch, &test_touch_impl, "twm-test-touch");
		wl_signal_emit_mutable(&server->backend->events.new_input, &server->test_touch->base);
	}
	struct wlr_touch *t = server->test_touch;
	char what[8] = "";
	int id = 0;
	double x = 0, y = 0;
	int n = sscanf(req, "touch %7s %d %lf %lf", what, &id, &x, &y);
	struct wlr_output *out = touch_output(server, t);
	struct wlr_box box = {0};
	if (out) {
		wlr_output_layout_get_box(server->output_layout, out, &box);
	} else {
		wlr_output_layout_get_box(server->output_layout, NULL, &box);
	}
	double nx = box.width ? x / box.width : 0, ny = box.height ? y / box.height : 0;
	uint32_t now = (uint32_t)(server_now_ms() & 0xffffffff);
	if (n == 4 && strcmp(what, "down") == 0) {
		struct wlr_touch_down_event e = {.touch = t, .time_msec = now, .touch_id = id, .x = nx, .y = ny};
		wl_signal_emit_mutable(&t->events.down, &e);
	} else if (n == 4 && strcmp(what, "move") == 0) {
		struct wlr_touch_motion_event e = {.touch = t, .time_msec = now, .touch_id = id, .x = nx, .y = ny};
		wl_signal_emit_mutable(&t->events.motion, &e);
	} else if (n >= 2 && strcmp(what, "up") == 0) {
		struct wlr_touch_up_event e = {.touch = t, .time_msec = now, .touch_id = id};
		wl_signal_emit_mutable(&t->events.up, &e);
	} else {
		snprintf(reply, len, "touch: down ID X Y | move ID X Y | up ID\n");
		return false;
	}
	wl_signal_emit_mutable(&t->events.frame, NULL);
	snprintf(reply, len, "touch ok\n");
	return true;
}

#define LISTEN(signal, field, fn) do { \
	server->field.notify = fn; \
	wl_signal_add(signal, &server->field); \
} while (0)

void touch_init(struct twm_server *server) {
	server->touch_pointer = -1;
	LISTEN(&server->cursor->events.touch_down, cursor_touch_down, handle_down);
	LISTEN(&server->cursor->events.touch_motion, cursor_touch_motion, handle_motion);
	LISTEN(&server->cursor->events.touch_up, cursor_touch_up, handle_up);
	LISTEN(&server->cursor->events.touch_cancel, cursor_touch_cancel, handle_cancel);
	LISTEN(&server->cursor->events.touch_frame, cursor_touch_frame, handle_frame);
}

void touch_finish(struct twm_server *server) {
	if (!server->cursor) {
		return;
	}
	wl_list_remove(&server->cursor_touch_down.link);
	wl_list_remove(&server->cursor_touch_motion.link);
	wl_list_remove(&server->cursor_touch_up.link);
	wl_list_remove(&server->cursor_touch_cancel.link);
	wl_list_remove(&server->cursor_touch_frame.link);
	if (server->test_touch) {
		wlr_touch_finish(server->test_touch);
		free(server->test_touch);
		server->test_touch = NULL;
	}
}
