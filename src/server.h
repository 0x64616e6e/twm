#ifndef TWM_SERVER_H
#define TWM_SERVER_H

#include <stdbool.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>
#include "config.h"
#include "geomem.h"
#include "grab_state.h"
#include "history.h"
#include "intent.h"
#include "theme.h"

/* Linux input button codes (portable: no <linux/input-event-codes.h>). */
#define TWM_BTN_LEFT 0x110
#define TWM_BTN_RIGHT 0x111
#define TWM_BTN_MIDDLE 0x112

struct twm_view;
struct twm_outline;
struct twm_config;
struct twm_rootline;
struct twm_watch;
struct twm_internal_view;
struct twm_menu_ui;
struct twm_hints;
struct twm_prompt;
struct twm_card;
struct wlr_input_device;
struct wlr_touch;
struct twm_func;

/* Scene layers, back to front (D-4). */
enum twm_layer {
	TWM_LAYER_ROOT,
	TWM_LAYER_BACKGROUND, /* layer shell: wallpapers (below the root text) */
	TWM_LAYER_ROOT_TEXT,
	TWM_LAYER_BOTTOM,     /* layer shell */
	TWM_LAYER_WINDOWS,
	TWM_LAYER_UTILITY,
	TWM_LAYER_TOP,        /* layer shell: bars */
	TWM_LAYER_FULLSCREEN, /* the focused fullscreen window (ZOM-4) */
	TWM_LAYER_UNMANAGED, /* XWayland override-redirect: menus, tooltips */
	TWM_LAYER_LAYER_OVERLAY, /* layer shell: launchers, OSDs */
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
	TWM_HIT_CLOSE, /* FRM-19 */
	TWM_HIT_BORDER,
	TWM_HIT_ICON, /* a root icon (ROOT-9) */
	TWM_HIT_LAYER, /* a layer-shell surface (GEN-2) */
	TWM_HIT_CARD,  /* the root card (CARD-3) */
};

struct twm_hit {
	enum twm_hit_kind kind;
	struct twm_view *view; /* NULL for root and unmanaged */
};

struct twm_options {
	const char *config_path; /* -f */
	const char *startup_cmd;
	bool exit_with_startup;
	bool xwayland;
	int headless_width, headless_height; /* 0 = autocreate a backend */
	bool test_mode; /* --test: the startup command is a trusted test harness */
	bool session;   /* not nested, not headless: twm is the desktop session */
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
	uint32_t theme_gen; /* bumped on every applied config */
	struct twm_options opts;

	/* CFG-*: the running config, its file, and the text it came from */
	struct twm_config *config;
	char *config_path;
	char *config_text;
	time_t config_applied; /* FET-3: when the running config was applied */
	struct twm_watch *watch;
	struct twm_rootline *rootline;
	int ipc_fd;
	char *ipc_path;
	struct wl_event_source *ipc_source;
	int cascade; /* Placement cascade: next slot */

	/* CFG-11: geometry remembered per app id */
	struct geomem geomem;
	char *geomem_path;
	struct wl_event_source *geomem_timer;
	bool geomem_dirty;
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
	uint64_t view_counter;
	uint64_t icon_counter;
	int desktop; /* current, 1-based (DSK-1) */
	uint32_t quiet; /* URG-3: quiet desktops, bit n-1 = desktop n */
	uint64_t urgent_counter;
	uint64_t urgent_return;    /* URG-4: where M-u came from (view id) */
	uint64_t urgent_jumped_to; /* and where it went */
	struct wl_event_source *ui_idle;
	bool shutting_down;

	/* M4: twm's own windows, the root menu, click-to-select */
	struct twm_internal_view *imgr;
	struct twm_internal_view *panner;
	struct twm_internal_view *logview;
	struct twm_internal_view *setwin; /* SET-*: the Settings window */
	struct twm_menu_ui *menu;
	bool selecting;
	struct twm_func *select_func;
	struct twm_internal_view *button_target; /* got the press; gets the release */
	struct twm_hints *hints; /* HNT-*: non-NULL while hint mode is on */

	/* TRU-*: the trusted prompt on screen, the ones waiting, the dim */
	struct twm_prompt *prompt;
	struct wl_list prompt_queue;
	struct wlr_scene_rect *prompt_dim;
	struct portal *portal; /* portal.c: D-Bus, when there is a session bus */
	struct wl_list media_jobs; /* media.c: volume and brightness lines running */

	/* CAP-*: who is capturing the screen */
	struct wlr_screencopy_manager_v1 *screencopy;
	struct wlr_ext_image_copy_capture_manager_v1 *ext_capture;
	struct wl_listener new_capture_session;
	struct wl_event_source *capture_timer;
	struct wl_list capturers;
	uint64_t sticky_counter;

	/* HIS-*: the operation history and its window */
	struct history history;
	struct twm_internal_view *histview;
	struct wl_event_source *histview_timer;

	struct wlr_layer_shell_v1 *layer_shell;
	struct wl_listener new_layer_surface;
	struct wl_list layer_surfaces;

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
	struct twm_card *card; /* CARD-*: the root card */

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

	/* INP-*: pointers and touchscreens (libinput's settings), the touchscreen */
	struct wl_list devices; /* twm_device.link */
	struct wl_listener cursor_touch_down, cursor_touch_up, cursor_touch_motion,
		cursor_touch_cancel, cursor_touch_frame;
	int32_t touch_pointer; /* the touch point acting as the pointer, or -1 */
	struct wlr_touch *test_touch; /* --test: a touchscreen driven over IPC */

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
	struct wlr_box usable; /* box minus layer-shell exclusive zones */
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
void output_apply_config(struct twm_server *server);
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
/* An activation token for a program twm launches (FOC-9), or NULL. */
const char *focus_launch_token(struct twm_server *server);

/* decoration.c */
void decoration_init(struct twm_server *server);
void decoration_finish(struct twm_server *server);

/* input.c */
void input_init(struct twm_server *server);
void input_finish(struct twm_server *server);
void input_update_pointer_focus(struct twm_server *server, uint32_t time_msec);
/* The pointer moved (a device, or a finger acting as one): grabs, menus, focus. */
void input_pointer_moved(struct twm_server *server, uint32_t time_msec);
/* A button from a pointer, or from a finger acting as one (touch.c). */
void input_button(struct twm_server *server, struct wlr_input_device *device,
	uint32_t time_msec, uint32_t button, bool pressed);
/* INP-*: apply the config's touchpad and touchscreen settings again. */
void input_configure_devices(struct twm_server *server);

/* A pointer or a touchscreen (input.c), with libinput's settings applied. */
struct twm_device {
	struct wl_list link; /* twm_server.devices */
	struct twm_server *server;
	struct wlr_input_device *device;
	struct wl_listener destroy;
};

/* touch.c: the touchscreen (INP-3 … INP-5) */
void touch_init(struct twm_server *server);
void touch_finish(struct twm_server *server);
bool touch_present(struct twm_server *server);
/* Map each touchscreen onto its output (devices or outputs changed). */
void touch_map(struct twm_server *server);
/* `--test`: "touch down|move|up ..." from the IPC socket. */
bool touch_test_request(struct twm_server *server, const char *req, char *reply, size_t len);
/* Monotonic milliseconds. */
uint64_t server_now_ms(void);

/* xdg.c */
void xdg_init(struct twm_server *server);
void xdg_finish(struct twm_server *server);

/* xwayland.c */
#if HAVE_XWAYLAND
void xwayland_init(struct twm_server *server);
void xwayland_finish(struct twm_server *server);
#endif

#endif
