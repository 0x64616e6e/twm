#include <stdlib.h>
#include <wlr/backend.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>
#include "actions.h"
#include "frame.h"
#include "grab.h"
#include "view.h"

struct twm_keyboard {
	struct wl_list link;
	struct twm_server *server;
	struct wlr_keyboard *wlr_keyboard;
	struct wl_listener modifiers;
	struct wl_listener key;
	struct wl_listener destroy;
};

/* Grabs first (OUT-6, OUT-7), then the configured key bindings (KEY). */
static bool handle_key(struct twm_server *server, struct wlr_keyboard *kb,
		uint32_t keycode, uint32_t mods) {
	const xkb_keysym_t *syms;
	int nsyms = xkb_state_key_get_syms(kb->xkb_state, keycode, &syms);
	for (int i = 0; i < nsyms; i++) {
		if (grab_key(server, syms[i], mods)) {
			return true;
		}
	}
	/* Bindings name the unshifted key ("M-S-1", not "M-exclam"). */
	xkb_layout_index_t layout = xkb_state_key_get_layout(kb->xkb_state, keycode);
	nsyms = xkb_keymap_key_get_syms_by_level(kb->keymap, keycode, layout, 0, &syms);
	for (int i = 0; i < nsyms; i++) {
		const struct twm_key_binding *b = config_match_key(server->config, mods, syms[i]);
		if (b) {
			struct action_ctx ctx = {
				.view = server->focused,
				.context = TWM_CTX_WINDOW,
				.lx = server->cursor->x,
				.ly = server->cursor->y,
			};
			action_run(server, &b->func, &ctx);
			return true;
		}
	}
	return false;
}

static void keyboard_handle_modifiers(struct wl_listener *listener, void *data) {
	struct twm_keyboard *kb = wl_container_of(listener, kb, modifiers);
	wlr_seat_set_keyboard(kb->server->seat, kb->wlr_keyboard);
	wlr_seat_keyboard_notify_modifiers(kb->server->seat, &kb->wlr_keyboard->modifiers);
	grab_modifiers(kb->server, wlr_keyboard_get_modifiers(kb->wlr_keyboard));
}

static void keyboard_handle_key(struct wl_listener *listener, void *data) {
	struct twm_keyboard *kb = wl_container_of(listener, kb, key);
	struct twm_server *server = kb->server;
	struct wlr_keyboard_key_event *event = data;

	bool handled = false;
	uint32_t bit = event->keycode < 256 ? event->keycode : 0;
	if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED && kb->wlr_keyboard->xkb_state) {
		uint32_t mods = wlr_keyboard_get_modifiers(kb->wlr_keyboard);
		handled = handle_key(server, kb->wlr_keyboard, event->keycode + 8, mods);
		if (handled && bit) {
			server->consumed_keys[bit / 32] |= 1u << (bit % 32);
		}
	} else if (event->state == WL_KEYBOARD_KEY_STATE_RELEASED && bit &&
			(server->consumed_keys[bit / 32] & (1u << (bit % 32)))) {
		/* The press went to twm; so does the release. */
		server->consumed_keys[bit / 32] &= ~(1u << (bit % 32));
		handled = true;
	}
	if (!handled) {
		wlr_seat_set_keyboard(server->seat, kb->wlr_keyboard);
		wlr_seat_keyboard_notify_key(server->seat, event->time_msec,
			event->keycode, event->state);
	}
}

static void keyboard_handle_destroy(struct wl_listener *listener, void *data) {
	struct twm_keyboard *kb = wl_container_of(listener, kb, destroy);
	wl_list_remove(&kb->modifiers.link);
	wl_list_remove(&kb->key.link);
	wl_list_remove(&kb->destroy.link);
	wl_list_remove(&kb->link);
	free(kb);
}

static void update_capabilities(struct twm_server *server) {
	uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
	if (!wl_list_empty(&server->keyboards)) {
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	}
	wlr_seat_set_capabilities(server->seat, caps);
}

static void add_keyboard(struct twm_server *server, struct wlr_keyboard *wlr_keyboard,
		bool set_keymap) {
	struct twm_keyboard *kb = calloc(1, sizeof(*kb));
	if (!kb) {
		return;
	}
	kb->server = server;
	kb->wlr_keyboard = wlr_keyboard;

	if (set_keymap) {
		/* Layout from XKB_DEFAULT_* until the config sets one (M2). */
		struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
		struct xkb_keymap *keymap =
			xkb_keymap_new_from_names(ctx, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
		if (keymap) {
			wlr_keyboard_set_keymap(wlr_keyboard, keymap);
			xkb_keymap_unref(keymap);
		}
		xkb_context_unref(ctx);
		wlr_keyboard_set_repeat_info(wlr_keyboard, 25, 600);
	}

	kb->modifiers.notify = keyboard_handle_modifiers;
	wl_signal_add(&wlr_keyboard->events.modifiers, &kb->modifiers);
	kb->key.notify = keyboard_handle_key;
	wl_signal_add(&wlr_keyboard->events.key, &kb->key);
	kb->destroy.notify = keyboard_handle_destroy;
	wl_signal_add(&wlr_keyboard->base.events.destroy, &kb->destroy);

	wlr_seat_set_keyboard(server->seat, wlr_keyboard);
	wl_list_insert(&server->keyboards, &kb->link);
	update_capabilities(server);
}

static void handle_new_input(struct wl_listener *listener, void *data) {
	struct twm_server *server = wl_container_of(listener, server, new_input);
	struct wlr_input_device *device = data;
	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		add_keyboard(server, wlr_keyboard_from_input_device(device), true);
		break;
	case WLR_INPUT_DEVICE_POINTER:
		wlr_cursor_attach_input_device(server->cursor, device);
		break;
	default:
		break;
	}
	update_capabilities(server);
}

static void handle_new_virtual_pointer(struct wl_listener *listener, void *data) {
	struct twm_server *server = wl_container_of(listener, server, new_virtual_pointer);
	struct wlr_virtual_pointer_v1_new_pointer_event *event = data;
	struct wlr_input_device *device = &event->new_pointer->pointer.base;
	wlr_cursor_attach_input_device(server->cursor, device);
	if (event->suggested_output) {
		wlr_cursor_map_input_to_output(server->cursor, device, event->suggested_output);
	}
}

static void handle_new_virtual_keyboard(struct wl_listener *listener, void *data) {
	struct twm_server *server = wl_container_of(listener, server, new_virtual_keyboard);
	struct wlr_virtual_keyboard_v1 *vk = data;
	/* The client brings its own keymap. */
	add_keyboard(server, &vk->keyboard, false);
}

void input_update_pointer_focus(struct twm_server *server, uint32_t time_msec) {
	if (grab_active(server)) {
		return; /* FOC-8: the grab owns the pointer */
	}
	double sx = 0, sy = 0;
	struct wlr_surface *surface = NULL;
	struct twm_hit hit = server_hit_at(server, server->cursor->x, server->cursor->y,
		&surface, &sx, &sy);
	(void)hit;
	if (surface) {
		wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
		if (time_msec) {
			wlr_seat_pointer_notify_motion(server->seat, time_msec, sx, sy);
		}
	} else {
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
		wlr_seat_pointer_clear_focus(server->seat);
	}
}

static void pointer_moved(struct twm_server *server, uint32_t time_msec) {
	if (grab_active(server)) {
		grab_motion(server);
		return;
	}
	input_update_pointer_focus(server, time_msec); /* FOC-4: immediate */
	focus_pointer_moved(server);                    /* FOC-2: keyboard waits */
}

static void handle_cursor_motion(struct wl_listener *listener, void *data) {
	struct twm_server *server = wl_container_of(listener, server, cursor_motion);
	struct wlr_pointer_motion_event *event = data;
	wlr_cursor_move(server->cursor, &event->pointer->base, event->delta_x, event->delta_y);
	pointer_moved(server, event->time_msec);
}

static void handle_cursor_motion_absolute(struct wl_listener *listener, void *data) {
	struct twm_server *server = wl_container_of(listener, server, cursor_motion_absolute);
	struct wlr_pointer_motion_absolute_event *event = data;
	wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x, event->y);
	pointer_moved(server, event->time_msec);
}

static uint32_t button_bit(uint32_t button) {
	return button >= TWM_BTN_LEFT && button < TWM_BTN_LEFT + 32 ?
		1u << (button - TWM_BTN_LEFT) : 0;
}

static uint32_t current_mods(struct twm_server *server) {
	struct wlr_keyboard *kb = wlr_seat_get_keyboard(server->seat);
	return kb ? wlr_keyboard_get_modifiers(kb) : 0;
}

static uint32_t button_number(uint32_t button) {
	switch (button) {
	case TWM_BTN_LEFT: return 1;
	case TWM_BTN_MIDDLE: return 2;
	case TWM_BTN_RIGHT: return 3;
	case 0x113: return 4; /* BTN_SIDE */
	case 0x114: return 5; /* BTN_EXTRA */
	default: return 0;
	}
}

/*
 * Pointer bindings from the config (FRM-12, OUT-8, OUT-9, ROOT-4). Returns
 * true when twm takes the press; the client never sees it, nor its release.
 */
static bool handle_press(struct twm_server *server, uint32_t button) {
	double sx, sy;
	struct wlr_surface *surface = NULL;
	double lx = server->cursor->x, ly = server->cursor->y;
	struct twm_hit hit = server_hit_at(server, lx, ly, &surface, &sx, &sy);
	struct twm_view *view = hit.view;
	if (hit.kind == TWM_HIT_TITLE && view && view->frame) {
		hit.kind = frame_part_at(view->frame, lx, ly);
	}
	enum twm_context context;
	switch (hit.kind) {
	case TWM_HIT_ROOT: context = TWM_CTX_ROOT; break;
	case TWM_HIT_TITLE: context = TWM_CTX_TITLE; break;
	case TWM_HIT_ICONIFY: context = TWM_CTX_ICONIFY; break;
	case TWM_HIT_RESIZE: context = TWM_CTX_RESIZE; break;
	case TWM_HIT_BORDER: context = TWM_CTX_BORDER; break;
	case TWM_HIT_CLIENT: context = TWM_CTX_WINDOW; break;
	case TWM_HIT_ICON: context = TWM_CTX_ICON; break;
	case TWM_HIT_UNMANAGED:
	default:
		return false;
	}
	const struct twm_button_binding *b = config_match_button(server->config,
		button_number(button), current_mods(server), context);
	if (b) {
		struct action_ctx ctx = {
			.view = view, .context = context, .button = button, .lx = lx, .ly = ly,
		};
		action_run(server, &b->func, &ctx);
		return true;
	}
	if (!view) {
		return false;
	}
	/* FOC-5: a press focuses at once; FOC-6: raising is optional. */
	view_focus(view);
	if (server->config->raise_on_click) {
		view_raise(view);
	}
	/* Unbound presses on the frame stay with twm. */
	return context != TWM_CTX_WINDOW;
}

static void handle_cursor_button(struct wl_listener *listener, void *data) {
	struct twm_server *server = wl_container_of(listener, server, cursor_button);
	struct wlr_pointer_button_event *event = data;
	bool pressed = event->state == WL_POINTER_BUTTON_STATE_PRESSED;
	uint32_t bit = button_bit(event->button);

	bool consumed;
	if (grab_active(server)) {
		consumed = grab_button(server, event->button, pressed);
	} else if (pressed) {
		consumed = handle_press(server, event->button);
	} else {
		consumed = (server->consumed_buttons & bit) != 0;
	}
	if (pressed && consumed) {
		server->consumed_buttons |= bit;
	} else if (!pressed && (server->consumed_buttons & bit)) {
		server->consumed_buttons &= ~bit;
		consumed = true;
	}
	if (!consumed) {
		wlr_seat_pointer_notify_button(server->seat, event->time_msec,
			event->button, event->state);
	}
}

static void handle_cursor_axis(struct wl_listener *listener, void *data) {
	struct twm_server *server = wl_container_of(listener, server, cursor_axis);
	struct wlr_pointer_axis_event *event = data;
	if (grab_active(server)) {
		return;
	}
	wlr_seat_pointer_notify_axis(server->seat, event->time_msec, event->orientation,
		event->delta, event->delta_discrete, event->source, event->relative_direction);
}

static void handle_cursor_frame(struct wl_listener *listener, void *data) {
	struct twm_server *server = wl_container_of(listener, server, cursor_frame);
	wlr_seat_pointer_notify_frame(server->seat);
}

static void handle_request_cursor(struct wl_listener *listener, void *data) {
	struct twm_server *server = wl_container_of(listener, server, request_cursor);
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	if (server->seat->pointer_state.focused_client == event->seat_client) {
		wlr_cursor_set_surface(server->cursor, event->surface,
			event->hotspot_x, event->hotspot_y);
	}
}

static void handle_request_cursor_shape(struct wl_listener *listener, void *data) {
	struct twm_server *server = wl_container_of(listener, server, request_cursor_shape);
	struct wlr_cursor_shape_manager_v1_request_set_shape_event *event = data;
	if (server->seat->pointer_state.focused_client == event->seat_client) {
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr,
			wlr_cursor_shape_v1_name(event->shape));
	}
}

static void handle_pointer_focus_change(struct wl_listener *listener, void *data) {
	struct twm_server *server = wl_container_of(listener, server, pointer_focus_change);
	struct wlr_seat_pointer_focus_change_event *event = data;
	if (!event->new_surface) {
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
	}
}

static void handle_request_set_selection(struct wl_listener *listener, void *data) {
	struct twm_server *server = wl_container_of(listener, server, request_set_selection);
	struct wlr_seat_request_set_selection_event *event = data;
	wlr_seat_set_selection(server->seat, event->source, event->serial);
}

static void handle_request_set_primary_selection(struct wl_listener *listener, void *data) {
	struct twm_server *server =
		wl_container_of(listener, server, request_set_primary_selection);
	struct wlr_seat_request_set_primary_selection_event *event = data;
	wlr_seat_set_primary_selection(server->seat, event->source, event->serial);
}

#define LISTEN(signal, field, fn) do { \
	server->field.notify = fn; \
	wl_signal_add(signal, &server->field); \
} while (0)

void input_init(struct twm_server *server) {
	wl_list_init(&server->keyboards);

	server->cursor = wlr_cursor_create();
	wlr_cursor_attach_output_layout(server->cursor, server->output_layout);
	server->cursor_mgr = wlr_xcursor_manager_create(getenv("XCURSOR_THEME"), 24);

	LISTEN(&server->cursor->events.motion, cursor_motion, handle_cursor_motion);
	LISTEN(&server->cursor->events.motion_absolute, cursor_motion_absolute,
		handle_cursor_motion_absolute);
	LISTEN(&server->cursor->events.button, cursor_button, handle_cursor_button);
	LISTEN(&server->cursor->events.axis, cursor_axis, handle_cursor_axis);
	LISTEN(&server->cursor->events.frame, cursor_frame, handle_cursor_frame);

	server->seat = wlr_seat_create(server->display, "seat0");
	LISTEN(&server->backend->events.new_input, new_input, handle_new_input);
	LISTEN(&server->seat->events.request_set_cursor, request_cursor, handle_request_cursor);
	LISTEN(&server->seat->events.request_set_selection, request_set_selection,
		handle_request_set_selection);
	LISTEN(&server->seat->events.request_set_primary_selection,
		request_set_primary_selection, handle_request_set_primary_selection);
	LISTEN(&server->seat->pointer_state.events.focus_change, pointer_focus_change,
		handle_pointer_focus_change);

	server->cursor_shape = wlr_cursor_shape_manager_v1_create(server->display, 1);
	LISTEN(&server->cursor_shape->events.request_set_shape, request_cursor_shape,
		handle_request_cursor_shape);

	/* D-6: used by tests and remote tools; never reaches trusted frames (TRU-4). */
	server->virtual_pointer = wlr_virtual_pointer_manager_v1_create(server->display);
	LISTEN(&server->virtual_pointer->events.new_virtual_pointer, new_virtual_pointer,
		handle_new_virtual_pointer);
	server->virtual_keyboard = wlr_virtual_keyboard_manager_v1_create(server->display);
	LISTEN(&server->virtual_keyboard->events.new_virtual_keyboard, new_virtual_keyboard,
		handle_new_virtual_keyboard);

	update_capabilities(server);
}

void input_finish(struct twm_server *server) {
	if (!server->cursor) {
		return;
	}
	wl_list_remove(&server->cursor_motion.link);
	wl_list_remove(&server->cursor_motion_absolute.link);
	wl_list_remove(&server->cursor_button.link);
	wl_list_remove(&server->cursor_axis.link);
	wl_list_remove(&server->cursor_frame.link);
	wl_list_remove(&server->new_input.link);
	wl_list_remove(&server->request_cursor.link);
	wl_list_remove(&server->request_set_selection.link);
	wl_list_remove(&server->request_set_primary_selection.link);
	wl_list_remove(&server->pointer_focus_change.link);
	wl_list_remove(&server->request_cursor_shape.link);
	wl_list_remove(&server->new_virtual_pointer.link);
	wl_list_remove(&server->new_virtual_keyboard.link);
	wlr_xcursor_manager_destroy(server->cursor_mgr);
	wlr_cursor_destroy(server->cursor);
	server->cursor = NULL;
}
