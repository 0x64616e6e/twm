#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include "client.h"
#include "wlr-screencopy-unstable-v1-client-protocol.h"
#include "wlr-virtual-pointer-unstable-v1-client-protocol.h"
#include "virtual-keyboard-unstable-v1-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include <xkbcommon/xkbcommon.h>
#include "xdg-activation-v1-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

struct tc {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct wl_seat *seat;
	struct wl_output *output;
	struct xdg_wm_base *wm_base;
	struct zwlr_screencopy_manager_v1 *screencopy;
	struct zwlr_virtual_pointer_manager_v1 *vptr_mgr;
	struct zwlr_virtual_pointer_v1 *vptr;
	struct xdg_activation_v1 *activation;
	struct zxdg_decoration_manager_v1 *decoration;
	struct zwp_virtual_keyboard_manager_v1 *vkbd_mgr;
	struct zwlr_layer_shell_v1 *layer_shell;
	struct zwp_virtual_keyboard_v1 *vkbd;
	struct xkb_keymap *keymap;
	struct xkb_state *xkb; /* wlroots takes modifiers from us, not from keys */
	int out_width, out_height;
};

struct tc_window {
	struct tc *tc;
	struct tc_window_opts opts;
	struct wl_surface *surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *toplevel;
	struct zxdg_toplevel_decoration_v1 *deco;
	struct wl_buffer *buffer;
	int width, height;
	int pending_width, pending_height;
	bool activated, pending_activated;
	bool configured;
};

/* --- shm --- */

static int create_shm_file(size_t size) {
	char name[64];
	for (int i = 0; i < 100; i++) {
		struct timespec ts;
		clock_gettime(CLOCK_REALTIME, &ts);
		snprintf(name, sizeof(name), "/twm-test-%d-%ld", (int)getpid(), ts.tv_nsec);
		int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
		if (fd >= 0) {
			shm_unlink(name);
			if (ftruncate(fd, (off_t)size) < 0) {
				close(fd);
				return -1;
			}
			return fd;
		}
		if (errno != EEXIST) {
			return -1;
		}
	}
	return -1;
}

static struct wl_buffer *solid_buffer(struct tc *tc, int w, int h, uint32_t rgb) {
	int stride = w * 4;
	size_t size = (size_t)stride * (size_t)h;
	int fd = create_shm_file(size);
	if (fd < 0) {
		return NULL;
	}
	uint32_t *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (data == MAP_FAILED) {
		close(fd);
		return NULL;
	}
	for (size_t i = 0; i < (size_t)w * (size_t)h; i++) {
		data[i] = 0xFF000000u | rgb;
	}
	munmap(data, size);
	struct wl_shm_pool *pool = wl_shm_create_pool(tc->shm, fd, (int32_t)size);
	struct wl_buffer *buf = wl_shm_pool_create_buffer(pool, 0, w, h, stride,
		WL_SHM_FORMAT_XRGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);
	return buf;
}

/* --- registry --- */

static void output_geometry(void *data, struct wl_output *o, int32_t x, int32_t y,
		int32_t pw, int32_t ph, int32_t subpixel, const char *make,
		const char *model, int32_t transform) {
}

static void output_mode(void *data, struct wl_output *o, uint32_t flags,
		int32_t w, int32_t h, int32_t refresh) {
	struct tc *tc = data;
	if (flags & WL_OUTPUT_MODE_CURRENT) {
		tc->out_width = w;
		tc->out_height = h;
	}
}

static void output_done(void *data, struct wl_output *o) {
}

static void output_scale(void *data, struct wl_output *o, int32_t factor) {
}

static void output_name(void *data, struct wl_output *o, const char *name) {
}

static void output_description(void *data, struct wl_output *o, const char *desc) {
}

static const struct wl_output_listener output_listener = {
	.geometry = output_geometry,
	.mode = output_mode,
	.done = output_done,
	.scale = output_scale,
	.name = output_name,
	.description = output_description,
};

static void wm_base_ping(void *data, struct xdg_wm_base *base, uint32_t serial) {
	xdg_wm_base_pong(base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
	.ping = wm_base_ping,
};

static uint32_t min_u32(uint32_t a, uint32_t b) {
	return a < b ? a : b;
}

static void registry_global(void *data, struct wl_registry *reg, uint32_t name,
		const char *iface, uint32_t version) {
	struct tc *tc = data;
	if (strcmp(iface, wl_compositor_interface.name) == 0) {
		tc->compositor = wl_registry_bind(reg, name, &wl_compositor_interface, min_u32(version, 4));
	} else if (strcmp(iface, wl_shm_interface.name) == 0) {
		tc->shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
	} else if (strcmp(iface, wl_seat_interface.name) == 0 && !tc->seat) {
		tc->seat = wl_registry_bind(reg, name, &wl_seat_interface, 1);
	} else if (strcmp(iface, wl_output_interface.name) == 0 && !tc->output) {
		tc->output = wl_registry_bind(reg, name, &wl_output_interface, min_u32(version, 4));
		wl_output_add_listener(tc->output, &output_listener, tc);
	} else if (strcmp(iface, xdg_wm_base_interface.name) == 0) {
		tc->wm_base = wl_registry_bind(reg, name, &xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(tc->wm_base, &wm_base_listener, tc);
	} else if (strcmp(iface, zwlr_screencopy_manager_v1_interface.name) == 0) {
		tc->screencopy = wl_registry_bind(reg, name,
			&zwlr_screencopy_manager_v1_interface, min_u32(version, 3));
	} else if (strcmp(iface, zwlr_virtual_pointer_manager_v1_interface.name) == 0) {
		tc->vptr_mgr = wl_registry_bind(reg, name,
			&zwlr_virtual_pointer_manager_v1_interface, 1);
	} else if (strcmp(iface, xdg_activation_v1_interface.name) == 0) {
		tc->activation = wl_registry_bind(reg, name, &xdg_activation_v1_interface, 1);
	} else if (strcmp(iface, zwlr_layer_shell_v1_interface.name) == 0) {
		tc->layer_shell = wl_registry_bind(reg, name, &zwlr_layer_shell_v1_interface, 1);
	} else if (strcmp(iface, zwp_virtual_keyboard_manager_v1_interface.name) == 0) {
		tc->vkbd_mgr = wl_registry_bind(reg, name,
			&zwp_virtual_keyboard_manager_v1_interface, 1);
	} else if (strcmp(iface, zxdg_decoration_manager_v1_interface.name) == 0) {
		tc->decoration = wl_registry_bind(reg, name,
			&zxdg_decoration_manager_v1_interface, 1);
	}
}

static void registry_global_remove(void *data, struct wl_registry *reg, uint32_t name) {
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_global,
	.global_remove = registry_global_remove,
};

struct tc *tc_connect(void) {
	struct tc *tc = calloc(1, sizeof(*tc));
	tc->display = wl_display_connect(NULL);
	if (!tc->display) {
		fprintf(stderr, "tc: cannot connect to the compositor\n");
		exit(2);
	}
	tc->registry = wl_display_get_registry(tc->display);
	wl_registry_add_listener(tc->registry, &registry_listener, tc);
	wl_display_roundtrip(tc->display);
	wl_display_roundtrip(tc->display);
	if (!tc->compositor || !tc->shm || !tc->wm_base || !tc->output ||
			!tc->screencopy || !tc->vptr_mgr || !tc->seat) {
		fprintf(stderr, "tc: compositor lacks a required global\n");
		exit(2);
	}
	tc->vptr = zwlr_virtual_pointer_manager_v1_create_virtual_pointer(tc->vptr_mgr, tc->seat);
	if (tc->vkbd_mgr) {
		/* The protocol wants a keymap before any key: the default one. */
		tc->vkbd = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(tc->vkbd_mgr, tc->seat);
		struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
		struct xkb_keymap *km = xkb_keymap_new_from_names(ctx, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
		char *text = xkb_keymap_get_as_string(km, XKB_KEYMAP_FORMAT_TEXT_V1);
		size_t size = strlen(text) + 1;
		int fd = create_shm_file(size);
		void *map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		memcpy(map, text, size);
		munmap(map, size);
		zwp_virtual_keyboard_v1_keymap(tc->vkbd, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd,
			(uint32_t)size);
		close(fd);
		free(text);
		tc->keymap = km;
		tc->xkb = xkb_state_new(km);
		xkb_context_unref(ctx);
	}
	wl_display_roundtrip(tc->display);
	return tc;
}

void tc_disconnect(struct tc *tc) {
	wl_display_disconnect(tc->display);
	free(tc);
}

int tc_output_width(struct tc *tc) {
	return tc->out_width;
}

int tc_output_height(struct tc *tc) {
	return tc->out_height;
}

/* --- windows --- */

static void window_commit_buffer(struct tc_window *win) {
	if (win->buffer) {
		wl_buffer_destroy(win->buffer);
	}
	win->buffer = solid_buffer(win->tc, win->width, win->height, win->opts.color);
	wl_surface_attach(win->surface, win->buffer, 0, 0);
	wl_surface_damage_buffer(win->surface, 0, 0, win->width, win->height);
	wl_surface_commit(win->surface);
}

void tc_window_resize_self(struct tc_window *win, int w, int h) {
	win->opts.width = w;
	win->opts.height = h;
	win->width = w;
	win->height = h;
	window_commit_buffer(win);
	wl_display_flush(win->tc->display);
}

static void xdg_surface_configure(void *data, struct xdg_surface *xs, uint32_t serial) {
	struct tc_window *win = data;
	xdg_surface_ack_configure(xs, serial);
	int w = win->pending_width > 0 ? win->pending_width : win->opts.width;
	int h = win->pending_height > 0 ? win->pending_height : win->opts.height;
	win->activated = win->pending_activated;
	if (!win->configured || w != win->width || h != win->height) {
		win->width = w;
		win->height = h;
		window_commit_buffer(win);
	} else {
		wl_surface_commit(win->surface);
	}
	win->configured = true;
}

static const struct xdg_surface_listener xdg_surface_listener = {
	.configure = xdg_surface_configure,
};

static void toplevel_configure(void *data, struct xdg_toplevel *t, int32_t w, int32_t h,
		struct wl_array *states) {
	struct tc_window *win = data;
	win->pending_width = w;
	win->pending_height = h;
	win->pending_activated = false;
	uint32_t *state;
	wl_array_for_each(state, states) {
		if (*state == XDG_TOPLEVEL_STATE_ACTIVATED) {
			win->pending_activated = true;
		}
	}
}

static void toplevel_close(void *data, struct xdg_toplevel *t) {
}

static void toplevel_configure_bounds(void *data, struct xdg_toplevel *t, int32_t w, int32_t h) {
}

static void toplevel_wm_capabilities(void *data, struct xdg_toplevel *t, struct wl_array *caps) {
}

static const struct xdg_toplevel_listener toplevel_listener = {
	.configure = toplevel_configure,
	.close = toplevel_close,
	.configure_bounds = toplevel_configure_bounds,
	.wm_capabilities = toplevel_wm_capabilities,
};

struct tc_window *tc_window_create(struct tc *tc, const struct tc_window_opts *opts) {
	struct tc_window *win = calloc(1, sizeof(*win));
	win->tc = tc;
	win->opts = *opts;
	win->surface = wl_compositor_create_surface(tc->compositor);
	win->xdg_surface = xdg_wm_base_get_xdg_surface(tc->wm_base, win->surface);
	xdg_surface_add_listener(win->xdg_surface, &xdg_surface_listener, win);
	win->toplevel = xdg_surface_get_toplevel(win->xdg_surface);
	xdg_toplevel_add_listener(win->toplevel, &toplevel_listener, win);
	if (opts->title) {
		xdg_toplevel_set_title(win->toplevel, opts->title);
	}
	if (opts->app_id) {
		xdg_toplevel_set_app_id(win->toplevel, opts->app_id);
	}
	if (opts->fixed_size) {
		xdg_toplevel_set_min_size(win->toplevel, opts->width, opts->height);
		xdg_toplevel_set_max_size(win->toplevel, opts->width, opts->height);
	}
	if (opts->request_ssd && tc->decoration) {
		win->deco = zxdg_decoration_manager_v1_get_toplevel_decoration(
			tc->decoration, win->toplevel);
		zxdg_toplevel_decoration_v1_set_mode(win->deco,
			ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
	}
	wl_surface_commit(win->surface);
	wl_display_roundtrip(tc->display);
	wl_display_roundtrip(tc->display);
	return win;
}

void tc_window_destroy(struct tc_window *win) {
	if (win->deco) {
		zxdg_toplevel_decoration_v1_destroy(win->deco);
	}
	xdg_toplevel_destroy(win->toplevel);
	xdg_surface_destroy(win->xdg_surface);
	wl_surface_destroy(win->surface);
	if (win->buffer) {
		wl_buffer_destroy(win->buffer);
	}
	wl_display_roundtrip(win->tc->display);
	free(win);
}

bool tc_window_activated(struct tc_window *win) {
	return win->activated;
}

void tc_window_size(struct tc_window *win, int *w, int *h) {
	*w = win->width;
	*h = win->height;
}

static void token_done(void *data, struct xdg_activation_token_v1 *token, const char *str) {
	char **out = data;
	*out = strdup(str);
}

static const struct xdg_activation_token_v1_listener token_listener = {
	.done = token_done,
};

void tc_window_request_activation(struct tc_window *win) {
	struct tc *tc = win->tc;
	if (!tc->activation) {
		return;
	}
	/* A token not tied to any input event. */
	struct xdg_activation_token_v1 *token = xdg_activation_v1_get_activation_token(tc->activation);
	char *str = NULL;
	xdg_activation_token_v1_add_listener(token, &token_listener, &str);
	xdg_activation_token_v1_commit(token);
	while (!str && wl_display_dispatch(tc->display) >= 0) {
	}
	xdg_activation_token_v1_destroy(token);
	if (str) {
		xdg_activation_v1_activate(tc->activation, str, win->surface);
		free(str);
	}
	wl_display_roundtrip(tc->display);
}

/* --- layer shell --- */

struct tc_layer {
	struct tc *tc;
	struct wl_surface *surface;
	struct zwlr_layer_surface_v1 *ls;
	struct wl_buffer *buffer;
	uint32_t color;
	bool configured;
};

static void layer_configure(void *data, struct zwlr_layer_surface_v1 *ls, uint32_t serial,
		uint32_t w, uint32_t h) {
	struct tc_layer *l = data;
	zwlr_layer_surface_v1_ack_configure(ls, serial);
	if (l->buffer) {
		wl_buffer_destroy(l->buffer);
	}
	l->buffer = solid_buffer(l->tc, (int)w, (int)h, l->color);
	wl_surface_attach(l->surface, l->buffer, 0, 0);
	wl_surface_damage_buffer(l->surface, 0, 0, (int)w, (int)h);
	wl_surface_commit(l->surface);
	l->configured = true;
}

static void layer_closed(void *data, struct zwlr_layer_surface_v1 *ls) {
}

static const struct zwlr_layer_surface_v1_listener layer_listener = {
	.configure = layer_configure,
	.closed = layer_closed,
};

struct tc_layer *tc_layer_bar(struct tc *tc, int height, int exclusive, uint32_t color) {
	if (!tc->layer_shell) {
		fprintf(stderr, "tc: no layer shell\n");
		exit(2);
	}
	struct tc_layer *l = calloc(1, sizeof(*l));
	l->tc = tc;
	l->color = color;
	l->surface = wl_compositor_create_surface(tc->compositor);
	l->ls = zwlr_layer_shell_v1_get_layer_surface(tc->layer_shell, l->surface, tc->output,
		ZWLR_LAYER_SHELL_V1_LAYER_TOP, "tc-bar");
	zwlr_layer_surface_v1_add_listener(l->ls, &layer_listener, l);
	zwlr_layer_surface_v1_set_size(l->ls, 0, (uint32_t)height);
	zwlr_layer_surface_v1_set_anchor(l->ls, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
		ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
	zwlr_layer_surface_v1_set_exclusive_zone(l->ls, exclusive);
	wl_surface_commit(l->surface);
	wl_display_roundtrip(tc->display);
	wl_display_roundtrip(tc->display);
	return l;
}

void tc_layer_destroy(struct tc_layer *l) {
	zwlr_layer_surface_v1_destroy(l->ls);
	wl_surface_destroy(l->surface);
	if (l->buffer) {
		wl_buffer_destroy(l->buffer);
	}
	wl_display_roundtrip(l->tc->display);
	free(l);
}

/* --- input --- */

static uint32_t now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

void tc_pointer_move(struct tc *tc, double x, double y) {
	zwlr_virtual_pointer_v1_motion_absolute(tc->vptr, now_ms(),
		(uint32_t)(x < 0 ? 0 : x), (uint32_t)(y < 0 ? 0 : y),
		(uint32_t)tc->out_width, (uint32_t)tc->out_height);
	zwlr_virtual_pointer_v1_frame(tc->vptr);
	wl_display_roundtrip(tc->display);
}

void tc_pointer_button(struct tc *tc, uint32_t button, bool pressed) {
	zwlr_virtual_pointer_v1_button(tc->vptr, now_ms(), button,
		pressed ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED);
	zwlr_virtual_pointer_v1_frame(tc->vptr);
	wl_display_roundtrip(tc->display);
}

void tc_pointer_click(struct tc *tc, uint32_t button) {
	tc_pointer_button(tc, button, true);
	tc_pointer_button(tc, button, false);
}

void tc_key(struct tc *tc, uint32_t key, bool pressed) {
	if (!tc->vkbd) {
		fprintf(stderr, "tc: no virtual keyboard\n");
		exit(2);
	}
	/* Virtual keyboards don't update modifier state from keys in wlroots
	 * (update_state = false): track it here and send it first. */
	xkb_state_update_key(tc->xkb, key + 8, pressed ? XKB_KEY_DOWN : XKB_KEY_UP);
	zwp_virtual_keyboard_v1_modifiers(tc->vkbd,
		xkb_state_serialize_mods(tc->xkb, XKB_STATE_MODS_DEPRESSED),
		xkb_state_serialize_mods(tc->xkb, XKB_STATE_MODS_LATCHED),
		xkb_state_serialize_mods(tc->xkb, XKB_STATE_MODS_LOCKED),
		xkb_state_serialize_layout(tc->xkb, XKB_STATE_LAYOUT_EFFECTIVE));
	zwp_virtual_keyboard_v1_key(tc->vkbd, now_ms(), key,
		pressed ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);
	wl_display_roundtrip(tc->display);
}

void tc_chord(struct tc *tc, int n, ...) {
	uint32_t keys[8];
	va_list ap;
	va_start(ap, n);
	for (int i = 0; i < n && i < 8; i++) {
		keys[i] = va_arg(ap, uint32_t);
	}
	va_end(ap);
	for (int i = 0; i < n && i < 8; i++) {
		tc_key(tc, keys[i], true);
	}
	for (int i = (n < 8 ? n : 8) - 1; i >= 0; i--) {
		tc_key(tc, keys[i], false);
	}
}

void tc_roundtrip(struct tc *tc) {
	wl_display_roundtrip(tc->display);
}

void tc_wait(struct tc *tc, int ms) {
	struct timespec start;
	clock_gettime(CLOCK_MONOTONIC, &start);
	for (;;) {
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		long elapsed = (now.tv_sec - start.tv_sec) * 1000 +
			(now.tv_nsec - start.tv_nsec) / 1000000;
		if (elapsed >= ms) {
			break;
		}
		while (wl_display_prepare_read(tc->display) != 0) {
			wl_display_dispatch_pending(tc->display);
		}
		wl_display_flush(tc->display);
		struct pollfd pfd = {.fd = wl_display_get_fd(tc->display), .events = POLLIN};
		if (poll(&pfd, 1, (int)(ms - elapsed)) > 0) {
			wl_display_read_events(tc->display);
			wl_display_dispatch_pending(tc->display);
		} else {
			wl_display_cancel_read(tc->display);
		}
	}
	wl_display_roundtrip(tc->display);
}

/* --- screencopy --- */

struct capture {
	struct tc *tc;
	uint32_t format;
	int width, height, stride;
	bool have_buffer, buffer_done, ready, failed, y_invert;
};

static void frame_buffer(void *data, struct zwlr_screencopy_frame_v1 *f, uint32_t format,
		uint32_t w, uint32_t h, uint32_t stride) {
	struct capture *c = data;
	if (format == WL_SHM_FORMAT_XRGB8888 || format == WL_SHM_FORMAT_ARGB8888) {
		c->format = format;
		c->width = (int)w;
		c->height = (int)h;
		c->stride = (int)stride;
		c->have_buffer = true;
	}
}

static void frame_flags(void *data, struct zwlr_screencopy_frame_v1 *f, uint32_t flags) {
	struct capture *c = data;
	c->y_invert = flags & ZWLR_SCREENCOPY_FRAME_V1_FLAGS_Y_INVERT;
}

static void frame_ready(void *data, struct zwlr_screencopy_frame_v1 *f,
		uint32_t sec_hi, uint32_t sec_lo, uint32_t nsec) {
	struct capture *c = data;
	c->ready = true;
}

static void frame_failed(void *data, struct zwlr_screencopy_frame_v1 *f) {
	struct capture *c = data;
	c->failed = true;
}

static void frame_damage(void *data, struct zwlr_screencopy_frame_v1 *f,
		uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
}

static void frame_linux_dmabuf(void *data, struct zwlr_screencopy_frame_v1 *f,
		uint32_t format, uint32_t w, uint32_t h) {
}

static void frame_buffer_done(void *data, struct zwlr_screencopy_frame_v1 *f) {
	struct capture *c = data;
	c->buffer_done = true;
}

static const struct zwlr_screencopy_frame_v1_listener frame_listener = {
	.buffer = frame_buffer,
	.flags = frame_flags,
	.ready = frame_ready,
	.failed = frame_failed,
	.damage = frame_damage,
	.linux_dmabuf = frame_linux_dmabuf,
	.buffer_done = frame_buffer_done,
};

bool tc_screenshot(struct tc *tc, struct tc_image *img) {
	/* Let pending commits reach the screen first. */
	tc_wait(tc, 50);
	struct capture c = {.tc = tc};
	struct zwlr_screencopy_frame_v1 *frame =
		zwlr_screencopy_manager_v1_capture_output(tc->screencopy, 0, tc->output);
	zwlr_screencopy_frame_v1_add_listener(frame, &frame_listener, &c);
	bool v3 = zwlr_screencopy_manager_v1_get_version(tc->screencopy) >= 3;
	while (!c.failed && !(v3 ? c.buffer_done : c.have_buffer)) {
		if (wl_display_dispatch(tc->display) < 0) {
			return false;
		}
	}
	if (c.failed || !c.have_buffer) {
		zwlr_screencopy_frame_v1_destroy(frame);
		return false;
	}
	size_t size = (size_t)c.stride * (size_t)c.height;
	int fd = create_shm_file(size);
	if (fd < 0) {
		return false;
	}
	void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	struct wl_shm_pool *pool = wl_shm_create_pool(tc->shm, fd, (int32_t)size);
	struct wl_buffer *buf = wl_shm_pool_create_buffer(pool, 0, c.width, c.height,
		c.stride, c.format);
	wl_shm_pool_destroy(pool);
	close(fd);
	zwlr_screencopy_frame_v1_copy(frame, buf);
	while (!c.ready && !c.failed) {
		if (wl_display_dispatch(tc->display) < 0) {
			break;
		}
	}
	bool ok = c.ready;
	if (ok) {
		img->width = c.width;
		img->height = c.height;
		img->pixels = malloc((size_t)c.width * (size_t)c.height * 4);
		for (int y = 0; y < c.height; y++) {
			int sy = c.y_invert ? c.height - 1 - y : y;
			const uint32_t *row = (const uint32_t *)((const char *)data + (size_t)sy * (size_t)c.stride);
			for (int x = 0; x < c.width; x++) {
				img->pixels[(size_t)y * (size_t)c.width + (size_t)x] = 0xFF000000u | (row[x] & 0xFFFFFF);
			}
		}
	}
	munmap(data, size);
	wl_buffer_destroy(buf);
	zwlr_screencopy_frame_v1_destroy(frame);
	return ok;
}

void tc_image_free(struct tc_image *img) {
	free(img->pixels);
	img->pixels = NULL;
}

uint32_t tc_pixel(const struct tc_image *img, int x, int y) {
	if (x < 0 || y < 0 || x >= img->width || y >= img->height) {
		return 0;
	}
	return img->pixels[(size_t)y * (size_t)img->width + (size_t)x] & 0xFFFFFF;
}

/* --- PNG (stored deflate, no compression; D-7) --- */

static uint32_t crc_table[256];

static void crc_init(void) {
	for (uint32_t n = 0; n < 256; n++) {
		uint32_t c = n;
		for (int k = 0; k < 8; k++) {
			c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
		}
		crc_table[n] = c;
	}
}

static uint32_t crc_update(uint32_t crc, const uint8_t *buf, size_t len) {
	for (size_t i = 0; i < len; i++) {
		crc = crc_table[(crc ^ buf[i]) & 0xff] ^ (crc >> 8);
	}
	return crc;
}

static void put_be32(uint8_t *p, uint32_t v) {
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}

static bool write_chunk(FILE *f, const char *type, const uint8_t *data, size_t len) {
	uint8_t hdr[8];
	put_be32(hdr, (uint32_t)len);
	memcpy(hdr + 4, type, 4);
	uint32_t crc = crc_update(0xFFFFFFFFu, (const uint8_t *)type, 4);
	crc = crc_update(crc, data, len) ^ 0xFFFFFFFFu;
	uint8_t tail[4];
	put_be32(tail, crc);
	return fwrite(hdr, 1, 8, f) == 8 && (len == 0 || fwrite(data, 1, len, f) == len) &&
		fwrite(tail, 1, 4, f) == 4;
}

bool tc_image_write_png(const struct tc_image *img, const char *path) {
	crc_init();
	size_t row_len = (size_t)img->width * 3 + 1;
	size_t raw_len = row_len * (size_t)img->height;
	uint8_t *raw = malloc(raw_len);
	for (int y = 0; y < img->height; y++) {
		uint8_t *row = raw + (size_t)y * row_len;
		row[0] = 0;
		for (int x = 0; x < img->width; x++) {
			uint32_t p = img->pixels[(size_t)y * (size_t)img->width + (size_t)x];
			row[1 + x * 3] = (uint8_t)(p >> 16);
			row[2 + x * 3] = (uint8_t)(p >> 8);
			row[3 + x * 3] = (uint8_t)p;
		}
	}
	size_t nblocks = (raw_len + 65534) / 65535;
	size_t z_len = 2 + raw_len + nblocks * 5 + 4;
	uint8_t *z = malloc(z_len);
	size_t o = 0;
	z[o++] = 0x78;
	z[o++] = 0x01;
	uint32_t a = 1, b = 0;
	for (size_t i = 0; i < raw_len; i += 65535) {
		size_t n = raw_len - i < 65535 ? raw_len - i : 65535;
		z[o++] = (i + n == raw_len) ? 1 : 0;
		z[o++] = (uint8_t)n;
		z[o++] = (uint8_t)(n >> 8);
		z[o++] = (uint8_t)~n;
		z[o++] = (uint8_t)(~n >> 8);
		memcpy(z + o, raw + i, n);
		o += n;
		for (size_t k = 0; k < n; k++) {
			a = (a + raw[i + k]) % 65521;
			b = (b + a) % 65521;
		}
	}
	put_be32(z + o, (b << 16) | a);
	o += 4;

	FILE *f = fopen(path, "wb");
	bool ok = f != NULL;
	if (ok) {
		static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
		uint8_t ihdr[13];
		put_be32(ihdr, (uint32_t)img->width);
		put_be32(ihdr + 4, (uint32_t)img->height);
		ihdr[8] = 8;  /* bit depth */
		ihdr[9] = 2;  /* truecolor */
		ihdr[10] = 0;
		ihdr[11] = 0;
		ihdr[12] = 0;
		ok = fwrite(sig, 1, 8, f) == 8 && write_chunk(f, "IHDR", ihdr, 13) &&
			write_chunk(f, "IDAT", z, o) && write_chunk(f, "IEND", NULL, 0);
		ok = (fclose(f) == 0) && ok;
	}
	free(raw);
	free(z);
	return ok;
}

/* --- checks --- */

static int checks_run, checks_failed;

static int channel_diff(uint32_t a, uint32_t b, int shift) {
	return abs((int)((a >> shift) & 0xff) - (int)((b >> shift) & 0xff));
}

bool tc_expect_pixel(const char *req, const struct tc_image *img,
		int x, int y, uint32_t rgb, int tolerance) {
	uint32_t got = tc_pixel(img, x, y);
	bool ok = channel_diff(got, rgb, 16) <= tolerance &&
		channel_diff(got, rgb, 8) <= tolerance &&
		channel_diff(got, rgb, 0) <= tolerance;
	return tc_expect(req, ok, "pixel (%d,%d) is #%06X, want #%06X", x, y, got, rgb);
}

bool tc_expect(const char *req, bool ok, const char *fmt, ...) {
	checks_run++;
	if (!ok) {
		checks_failed++;
	}
	printf("%s %-8s ", ok ? "ok  " : "FAIL", req);
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	putchar('\n');
	return ok;
}

int tc_report(void) {
	printf("%d checks, %d failed\n", checks_run, checks_failed);
	return checks_failed == 0 ? 0 : 1;
}

uint32_t tc_blend(uint32_t top, double alpha, uint32_t bottom) {
	uint32_t out = 0;
	for (int shift = 16; shift >= 0; shift -= 8) {
		double t = (top >> shift) & 0xff;
		double bt = (bottom >> shift) & 0xff;
		long v = lround(t * alpha + bt * (1 - alpha));
		out |= (uint32_t)v << shift;
	}
	return out;
}
