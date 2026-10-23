#ifndef TWM_SERVER_H
#define TWM_SERVER_H

#include <stdbool.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>
#include "config.h"
#include "grab_state.h"
#include "intent.h"
#include "theme.h"

/* Linux input button codes (portable: no <linux/input-event-codes.h>). */
#define TWM_BTN_LEFT 0x110
#define TWM_BTN_RIGHT 0x111
#define TWM_BTN_MIDDLE 0x112

struct twm_view;
struct twm_outline;

/* Scene layers, back to front (D-4). */
enum twm_layer {
	TWM_LAYER_ROOT,
	TWM_LAYER_ROOT_TEXT,
	TWM_LAYER_WINDOWS,
	TWM_LAYER_UTILITY,
	TWM_LAYER_UNMANAGED, /* XWayland override-redirect: menus, tooltips */
	TWM_LAYER_TRUSTED_DIM,
	TWM_LAYER_TRUSTED,
	TWM_LAYER_OVERLAY, /* outlines, readouts, snap guides */
	TWM_LAYER_MENU,
	TWM_LAYER_COUNT,
};

/*
 * What a point in the scene belongs to. Scene nodes that matter for input
 * carry a pointer to one of these in node->data; hit testing walks up from
 * the node under the pointer to the first node with data.
 */
enum twm_hit_kind {
	TWM_HIT_ROOT,
	TWM_HIT_CLIENT,
	TWM_HIT_UNMANAGED,
	TWM_HIT_TITLE,
	TWM_HIT_ICONIFY,
	TWM_HIT_RESIZE,
	TWM_HIT_BORDER,
};

struct twm_hit {
	enum twm_hit_kind kind;
	struct twm_view *view; /* NULL for root and unmanaged */
};

struct twm_options {
	const char *startup_cmd;
	bool exit_with_startup;
	bool xwayland;
	int headless_width, headless_height; /* 0 = autocreate a backend */
};

struct twm_server {
	struct wl_display *display;
	struct wl_event_loop *loop;
	struct wlr_backend *backend;
	struct wlr_session *session;
	struct wlr_renderer *renderer;
	struct wlr_allocator *allocator;
	struct wlr_compositor *compositor;
	struct wlr_scene *scene;
	struct wlr_scene_output_layout *scene_layout;
	struct wlr_scene_tree *layers[TWM_LAYER_COUNT];

	struct twm_theme theme;
	struct twm_options opts;
	const char *socket;

	struct wlr_output_layout *output_layout;
	struct wl_list outputs; /* twm_output.link */
	struct wl_listener new_output;
	struct wl_listener layout_change;

	struct wlr_xdg_shell *xdg_shell;
	struct wl_listener new_xdg_toplevel;
	struct wl_listener new_xdg_popup;

	struct wl_list views; /* twm_view.link, top of the stack first */
	struct twm_view *focused;
	uint64_t focus_counter;

	struct wlr_xdg_decoration_manager_v1 *xdg_decoration;
	struct wl_listener new_toplevel_decoration;

	/* FOC-2: sloppy focus with intent */
	struct focus_intent intent;
	struct wl_event_source *focus_timer;
	struct wlr_xdg_activation_v1 *xdg_activation;
	struct wl_listener request_activate;

	/* OUT-*: move, resize, click-to-place */
	struct twm_grab grab;
	struct wl_list place_queue; /* twm_view.place_link */
	struct twm_outline *outline;
	uint32_t consumed_buttons; /* presses twm took; their releases too */
	uint32_t consumed_keys[8]; /* same for keys, by evdev keycode */

	float frame_scale;

	/* ROOT-2, ROOT-3 on the primary output */
	struct wlr_scene_buffer *wordmark;
	struct wlr_scene_buffer *status_line;

	struct wlr_cursor *cursor;
	struct wlr_xcursor_manager *cursor_mgr;
	struct wl_listener cursor_motion;
	struct wl_listener cursor_motion_absolute;
	struct wl_listener cursor_button;
	struct wl_listener cursor_axis;
	struct wl_listener cursor_frame;

	struct wlr_seat *seat;
	struct wl_list keyboards; /* twm_keyboard.link */
	struct wl_listener new_input;
	struct wl_listener request_cursor;
	struct wl_listener request_set_selection;
	struct wl_listener request_set_primary_selection;
	struct wl_listener pointer_focus_change;

	struct wlr_virtual_pointer_manager_v1 *virtual_pointer;
	struct wl_listener new_virtual_pointer;
	struct wlr_virtual_keyboard_manager_v1 *virtual_keyboard;
	struct wl_listener new_virtual_keyboard;
	struct wlr_cursor_shape_manager_v1 *cursor_shape;
	struct wl_listener request_cursor_shape;

	struct twm_hit root_hit;

#if HAVE_XWAYLAND
	struct wlr_xwayland *xwayland;
	struct wl_listener xwayland_ready;
	struct wl_listener xwayland_new_surface;
#endif

	pid_t startup_pid;
	int exit_code;
	struct wl_event_source *sigchld;
	struct wl_event_source *sigint;
	struct wl_event_source *sigterm;
};

struct twm_output {
	struct wl_list link;
	struct twm_server *server;
	struct wlr_output *wlr_output;
	struct wlr_box box; /* layout coordinates */
	float scale;        /* scale the root was last drawn at */

	struct wlr_scene_buffer *root; /* ground + weave (ROOT-1) */

	struct wl_listener frame;
	struct wl_listener request_state;
	struct wl_listener destroy;
};

/* server.c */
bool server_init(struct twm_server *server, const struct twm_options *opts);
bool server_start(struct twm_server *server);
void server_run(struct twm_server *server);
void server_finish(struct twm_server *server);
void server_spawn(struct twm_server *server, const char *cmd);
struct twm_hit server_hit_at(struct twm_server *server, double lx, double ly,
	struct wlr_surface **surface, double *sx, double *sy);

/* output.c */
void output_init(struct twm_server *server);
void output_finish(struct twm_server *server);
void output_arrange(struct twm_server *server);
struct twm_output *output_at(struct twm_server *server, double lx, double ly);
float output_max_scale(struct twm_server *server);

/* root.c */
void root_update(struct twm_output *output);
void root_update_text(struct twm_server *server);
void root_finish(struct twm_server *server);

/* focus.c */
void focus_init(struct twm_server *server);
void focus_finish(struct twm_server *server);
void focus_pointer_moved(struct twm_server *server);
void focus_cancel_pending(struct twm_server *server);
void focus_changed(struct twm_server *server);

/* decoration.c */
void decoration_init(struct twm_server *server);
void decoration_finish(struct twm_server *server);

/* input.c */
void input_init(struct twm_server *server);
void input_finish(struct twm_server *server);
void input_update_pointer_focus(struct twm_server *server, uint32_t time_msec);

/* xdg.c */
void xdg_init(struct twm_server *server);
void xdg_finish(struct twm_server *server);

/* xwayland.c */
#if HAVE_XWAYLAND
void xwayland_init(struct twm_server *server);
void xwayland_finish(struct twm_server *server);
#endif

#endif
