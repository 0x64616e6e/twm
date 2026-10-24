#include <stdlib.h>
#include <libinput.h>
#include <wlr/backend.h>
#include <wlr/backend/libinput.h>
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
#include <wayland-server-core.h>
#include "actions.h"
#include "capture.h"
#include "frame.h"
#include "grab.h"
#include "hints.h"
#include "internal.h"
#include "layers.h"
#include "menu.h"
#include "prompt.h"
#include "view.h"

struct twm_keyboard {
	struct wl_list link;
	struct twm_server *server;
	struct wlr_keyboard *wlr_keyboard;
	struct wl_listener modifiers;
	struct wl_listener key;
	struct wl_listener destroy;
	bool synthetic; /* a virtual keyboard: never reaches a prompt (TRU-4) */
};

/* Virtual pointers carry this in wlr_input_device.data (TRU-4). */
static char synthetic_mark;

static bool synthetic_device(struct wlr_input_device *device) {
	return device && device->data == &synthetic_mark;
}

/* D-7: only `--test`'s own program may drive prompts with virtual input. */
static bool synthetic_client(struct twm_server *server, struct wl_resource *resource) {
	return !capture_client_exempt(server, resource ? wl_resource_get_client(resource) : NULL);
}

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
	/* An open menu holds the keyboard (MNU-6); so does selecting (MNU-5). */
	if (menu_active(server)) {
		if (nsyms > 0) {
			menu_key(server, syms[0]);
		}
		return true;
	}
	/* Hint mode holds the keyboard (HNT-5). */
	if (hints_active(server)) {
		char hu[16] = "";
		xkb_state_key_get_utf8(kb->xkb_state, keycode, hu, sizeof(hu));
		hints_key(server, nsyms > 0 ? syms[0] : XKB_KEY_NoSymbol, hu);
		return true;
	}
	if (server->selecting && nsyms > 0 && syms[0] == XKB_KEY_Escape) {
		select_cancel(server);
		return true;
	}
	/* twm's own focused window (the Icon Manager filtering) may want keys
	 * before the bindings (IMG-8: M-Return brings a window here). */
	struct twm_internal_view *iv = internal_view_of(server->focused);
	char utf8[16] = "";
	xkb_state_key_get_utf8(kb->xkb_state, keycode, utf8, sizeof(utf8));
	xkb_keysym_t sym = nsyms > 0 ? syms[0] : XKB_KEY_NoSymbol;
	if (iv && iv->key && iv->wants_keys_first && iv->wants_keys_first(iv) &&
			iv->key(iv, sym, utf8, mods, true)) {
		return true;
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
	if (iv && iv->key && iv->key(iv, sym, utf8, mods, false)) {
		return true;
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
	if (server->prompt) {
		/* TRU-4: every key goes to the prompt, none to a client; synthetic
		 * keys go nowhere. Presses are marked so their releases stay too. */
		if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
			if (bit) {
				server->consumed_keys[bit / 32] |= 1u << (bit % 32);
			}
			if (!kb->synthetic && kb->wlr_keyboard->xkb_state) {
				const xkb_keysym_t *syms;
				int n = xkb_state_key_get_syms(kb->wlr_keyboard->xkb_state,
					event->keycode + 8, &syms);
				if (n > 0) {
					prompt_key(server, syms[0]);
				}
			}
		}
		return;
	}
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
	if (touch_present(server)) {
		caps |= WL_SEAT_CAPABILITY_TOUCH; /* INP-4: clients may take touch */
	}
	wlr_seat_set_capabilities(server->seat, caps);
}

static void add_keyboard(struct twm_server *server, struct wlr_keyboard *wlr_keyboard,
		bool set_keymap, bool synthetic) {
	struct twm_keyboard *kb = calloc(1, sizeof(*kb));
	if (!kb) {
		return;
	}
	kb->server = server;
	kb->wlr_keyboard = wlr_keyboard;
	kb->synthetic = synthetic;

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

/* --- pointers and touchscreens: libinput's settings (INP-*) --- */

static const char *onoff(bool on) {
	return on ? "on" : "off";
}

/* The .twmrc's touchpad keys on a touchpad, Touchscreen on a touchscreen;
 * other pointers (a mouse, a TrackPoint) keep libinput's defaults. */
static void configure_device(struct twm_server *server, struct wlr_input_device *device) {
	const struct twm_config *c = server->config;
	if (!c || !wlr_input_device_is_libinput(device)) {
		return;
	}
	struct libinput_device *ld = wlr_libinput_get_device_handle(device);
	if (device->type == WLR_INPUT_DEVICE_TOUCH) {
		libinput_device_config_send_events_set_mode(ld, c->touchscreen ?
			LIBINPUT_CONFIG_SEND_EVENTS_ENABLED : LIBINPUT_CONFIG_SEND_EVENTS_DISABLED);
		wlr_log(WLR_INFO, "touchscreen %s: %s", device->name, onoff(c->touchscreen));
		return;
	}
	if (libinput_device_config_tap_get_finger_count(ld) == 0) {
		return; /* not a touchpad */
	}
	libinput_device_config_tap_set_enabled(ld, c->tp_tap ?
		LIBINPUT_CONFIG_TAP_ENABLED : LIBINPUT_CONFIG_TAP_DISABLED);
	libinput_device_config_tap_set_drag_enabled(ld, c->tp_tap_drag ?
		LIBINPUT_CONFIG_DRAG_ENABLED : LIBINPUT_CONFIG_DRAG_DISABLED);
	if (libinput_device_config_scroll_has_natural_scroll(ld)) {
		libinput_device_config_scroll_set_natural_scroll_enabled(ld, c->tp_natural_scroll);
	}
	if (libinput_device_config_dwt_is_available(ld)) {
		libinput_device_config_dwt_set_enabled(ld, c->tp_dwt ?
			LIBINPUT_CONFIG_DWT_ENABLED : LIBINPUT_CONFIG_DWT_DISABLED);
	}
	static const enum libinput_config_scroll_method scroll[] = {
		LIBINPUT_CONFIG_SCROLL_2FG, LIBINPUT_CONFIG_SCROLL_EDGE, LIBINPUT_CONFIG_SCROLL_NO_SCROLL,
	};
	enum libinput_config_scroll_method sm = scroll[c->tp_scroll];
	if (sm == LIBINPUT_CONFIG_SCROLL_NO_SCROLL ||
			(libinput_device_config_scroll_get_methods(ld) & sm)) {
		libinput_device_config_scroll_set_method(ld, sm);
	}
	enum libinput_config_click_method cm = libinput_device_config_click_get_default_method(ld);
	if (c->tp_click == TWM_TP_CLICK_AREAS) {
		cm = LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS;
	} else if (c->tp_click == TWM_TP_CLICK_FINGERS) {
		cm = LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER;
	}
	if (cm == LIBINPUT_CONFIG_CLICK_METHOD_NONE || (libinput_device_config_click_get_methods(ld) & cm)) {
		libinput_device_config_click_set_method(ld, cm);
	}
	if (libinput_device_config_accel_is_available(ld)) {
		libinput_device_config_accel_set_speed(ld, c->tp_speed);
		libinput_device_config_accel_set_profile(ld, c->tp_accel == TWM_TP_ACCEL_FLAT ?
			LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT : LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE);
	}
	static const char *const scroll_name[] = {"two-finger", "edge", "none"};
	static const char *const click_name[] = {"default", "areas", "clickfinger"};
	wlr_log(WLR_INFO, "touchpad %s: tap %s, drag %s, natural scroll %s, while typing %s, "
		"scroll %s, click %s, speed %.2f, accel %s", device->name, onoff(c->tp_tap),
		onoff(c->tp_tap_drag), onoff(c->tp_natural_scroll), c->tp_dwt ? "ignored" : "used",
		scroll_name[c->tp_scroll], click_name[c->tp_click], c->tp_speed,
		c->tp_accel == TWM_TP_ACCEL_FLAT ? "flat" : "adaptive");
}

void input_configure_devices(struct twm_server *server) {
	struct twm_device *d;
	wl_list_for_each(d, &server->devices, link) {
		configure_device(server, d->device);
	}
}

static void device_handle_destroy(struct wl_listener *listener, void *data) {
	struct twm_device *d = wl_container_of(listener, d, destroy);
	struct twm_server *server = d->server;
	wl_list_remove(&d->destroy.link);
	wl_list_remove(&d->link);
	free(d);
	update_capabilities(server);
}

static void add_device(struct twm_server *server, struct wlr_input_device *device) {
	struct twm_device *d = calloc(1, sizeof(*d));
	if (!d) {
		return;
	}
	d->server = server;
	d->device = device;
	d->destroy.notify = device_handle_destroy;
	wl_signal_add(&device->events.destroy, &d->destroy);
	wl_list_insert(&server->devices, &d->link);
	configure_device(server, device);
	wlr_cursor_attach_input_device(server->cursor, device);
	if (device->type == WLR_INPUT_DEVICE_TOUCH) {
		touch_map(server); /* INP-3: onto its own screen */
	}
}

static void handle_new_input(struct wl_listener *listener, void *data) {
	struct twm_server *server = wl_container_of(listener, server, new_input);
	struct wlr_input_device *device = data;
	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		add_keyboard(server, wlr_keyboard_from_input_device(device), true, false);
		break;
	case WLR_INPUT_DEVICE_POINTER:
	case WLR_INPUT_DEVICE_TOUCH:
		add_device(server, device);
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
	if (synthetic_client(server, event->new_pointer->resource)) {
		device->data = &synthetic_mark;
	}
	wlr_cursor_attach_input_device(server->cursor, device);
	if (event->suggested_output) {
		wlr_cursor_map_input_to_output(server->cursor, device, event->suggested_output);
	}
}

static void handle_new_virtual_keyboard(struct wl_listener *listener, void *data) {
	struct twm_server *server = wl_container_of(listener, server, new_virtual_keyboard);
	struct wlr_virtual_keyboard_v1 *vk = data;
	/* The client brings its own keymap. */
	add_keyboard(server, &vk->keyboard, false, synthetic_client(server, vk->resource));
}

void input_update_pointer_focus(struct twm_server *server, uint32_t time_msec) {
	if (grab_active(server)) {
		return; /* FOC-8: the grab owns the pointer */
	}
	if (server->prompt) {
		/* TRU-4: no client sees the pointer while a prompt is up. */
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
		wlr_seat_pointer_clear_focus(server->seat);
		return;
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

void input_pointer_moved(struct twm_server *server, uint32_t time_msec) {
	if (server->prompt) {
		return; /* TRU-4: nothing under the pointer reacts */
	}
	if (grab_active(server)) {
		grab_motion(server);
		return;
	}
	if (menu_active(server)) {
		menu_motion(server, server->cursor->x, server->cursor->y);
		return; /* FOC-8 */
	}
	if (server->selecting) {
		return;
	}
	input_update_pointer_focus(server, time_msec); /* FOC-4: immediate */
	focus_pointer_moved(server);                    /* FOC-2: keyboard waits */
}

static void handle_cursor_motion(struct wl_listener *listener, void *data) {
	struct twm_server *server = wl_container_of(listener, server, cursor_motion);
	struct wlr_pointer_motion_event *event = data;
	if (server->prompt && synthetic_device(&event->pointer->base)) {
		return; /* TRU-4 */
	}
	wlr_cursor_move(server->cursor, &event->pointer->base, event->delta_x, event->delta_y);
	input_pointer_moved(server, event->time_msec);
}

static void handle_cursor_motion_absolute(struct wl_listener *listener, void *data) {
	struct twm_server *server = wl_container_of(listener, server, cursor_motion_absolute);
	struct wlr_pointer_motion_absolute_event *event = data;
	if (server->prompt && synthetic_device(&event->pointer->base)) {
		return; /* TRU-4 */
	}
	wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x, event->y);
	input_pointer_moved(server, event->time_msec);
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
	case TWM_HIT_CLOSE: context = TWM_CTX_CLOSE; break;
	case TWM_HIT_BORDER: context = TWM_CTX_BORDER; break;
	case TWM_HIT_CLIENT: context = TWM_CTX_WINDOW; break;
	case TWM_HIT_ICON: context = TWM_CTX_ICON; break;
	case TWM_HIT_LAYER:
		layers_pressed(server, surface); /* keyboard on demand */
		return false;
	case TWM_HIT_UNMANAGED:
	default:
		return false;
	}
	const struct twm_button_binding *b = config_match_button(server->config,
		button_number(button), current_mods(server), context);
	if (b) {
		struct action_ctx ctx = {
			.view = view, .context = context, .button = button, .lx = lx, .ly = ly,
			.tab = context == TWM_CTX_TITLE && view && view->frame ?
				frame_member_at(view->frame, lx) : NULL,
		};
		action_run(server, &b->func, &ctx);
		return true;
	}
	if (!view) {
		return false;
	}
	/* FOC-5: a press focuses at once; FOC-6: raising is optional. */
	view_focus(view);
	struct twm_internal_view *iv = internal_view_of(view);
	if (iv && context == TWM_CTX_WINDOW) {
		int dx, dy;
		view_client_offset(view, &dx, &dy);
		if (iv->button) {
			iv->button(iv, button, true, lx - view->x - dx, ly - view->y - dy);
		}
		server->button_target = iv;
		return true;
	}
	if (server->config->raise_on_click) {
		view_raise(view);
	}
	/* Unbound presses on the frame stay with twm. */
	return context != TWM_CTX_WINDOW;
}

/* A button from a pointer, or from a finger acting as one (touch.c). */
void input_button(struct twm_server *server, struct wlr_input_device *device,
		uint32_t time_msec, uint32_t button, bool pressed) {
	const struct { uint32_t button, time_msec; enum wl_pointer_button_state state; } ev = {
		button, time_msec,
		pressed ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED,
	}, *event = &ev;
	uint32_t bit = button_bit(event->button);

	bool consumed;
	if (server->prompt) {
		/* TRU-4: buttons reach the prompt or nothing; synthetic ones never. */
		if (pressed) {
			server->consumed_buttons |= bit;
		} else {
			server->consumed_buttons &= ~bit;
		}
		if (!synthetic_device(device)) {
			prompt_pointer_button(server, event->button, pressed);
		}
		return;
	}
	if (pressed && hints_active(server)) {
		hints_cancel(server); /* a click leaves hint mode and goes on */
	}
	if (menu_active(server)) {
		/* MNU-1: the menu takes every button, the opening press's release too. */
		menu_button(server, event->button, pressed);
		if (pressed) {
			server->consumed_buttons |= bit;
		} else {
			server->consumed_buttons &= ~bit;
		}
		return;
	}
	if (pressed && server->selecting) {
		select_press(server);
		server->consumed_buttons |= bit;
		return;
	}
	if (grab_active(server)) {
		consumed = grab_button(server, event->button, pressed);
	} else if (pressed) {
		consumed = handle_press(server, event->button);
	} else {
		consumed = (server->consumed_buttons & bit) != 0;
		struct twm_internal_view *iv = server->button_target;
		if (iv && consumed) {
			server->button_target = NULL;
			if (iv->button && iv->view.shown) {
				int dx, dy;
				view_client_offset(&iv->view, &dx, &dy);
				iv->button(iv, event->button, false, server->cursor->x - iv->view.x - dx,
					server->cursor->y - iv->view.y - dy);
			}
		}
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

static void handle_cursor_button(struct wl_listener *listener, void *data) {
	struct twm_server *server = wl_container_of(listener, server, cursor_button);
	struct wlr_pointer_button_event *event = data;
	input_button(server, &event->pointer->base, event->time_msec, event->button,
		event->state == WL_POINTER_BUTTON_STATE_PRESSED);
}

static void handle_cursor_axis(struct wl_listener *listener, void *data) {
	struct twm_server *server = wl_container_of(listener, server, cursor_axis);
	struct wlr_pointer_axis_event *event = data;
	if (grab_active(server) || server->prompt) {
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
