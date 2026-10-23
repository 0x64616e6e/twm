#ifndef TWM_VIEW_H
#define TWM_VIEW_H

#include <stdbool.h>
#include <wayland-server-core.h>
#include "server.h"

/*
 * One managed toplevel, whatever its shell (xdg-shell or XWayland). The
 * shell-specific code fills in `impl` and drives map/unmap/commit; the rest
 * of twm talks only to this struct.
 */

enum twm_view_type {
	TWM_VIEW_XDG,
	TWM_VIEW_XWAYLAND,
};

struct twm_size_limits {
	int min_width, min_height;
	int max_width, max_height; /* 0 = unbounded */
};

struct twm_view_impl {
	const char *(*get_title)(struct twm_view *view);
	const char *(*get_app_id)(struct twm_view *view);
	void (*set_activated)(struct twm_view *view, bool activated);
	/* Client box in layout coordinates. */
	void (*configure)(struct twm_view *view, int x, int y, int w, int h);
	void (*close)(struct twm_view *view);
	struct twm_size_limits (*get_size_limits)(struct twm_view *view);
};

struct twm_view {
	struct wl_list link; /* twm_server.views */
	struct twm_server *server;
	enum twm_view_type type;
	const struct twm_view_impl *impl;

	struct wlr_surface *surface;
	struct wlr_scene_tree *tree;    /* at the frame's outer top-left */
	struct wlr_scene_tree *content; /* client surfaces live here */
	struct twm_hit client_hit;

	int x, y;          /* frame outer top-left, layout coordinates */
	int width, height; /* client size */
	int geom_x, geom_y; /* xdg window-geometry offset inside the surface */
	bool mapped;
};

void view_init(struct twm_view *view, struct twm_server *server,
	enum twm_view_type type, const struct twm_view_impl *impl);
void view_finish(struct twm_view *view);
void view_set_content(struct twm_view *view, struct wlr_scene_tree *content);

void view_map(struct twm_view *view);
void view_unmap(struct twm_view *view);
/* The client committed a new window geometry (surface-local). */
void view_update_geometry(struct twm_view *view, int gx, int gy,
	int width, int height);

void view_move(struct twm_view *view, int x, int y);
void view_raise(struct twm_view *view);
void view_focus(struct twm_view *view);

/* Offset of the client area inside the frame. */
void view_client_offset(struct twm_view *view, int *dx, int *dy);

const char *view_title(struct twm_view *view);

struct twm_view *view_from_surface(struct twm_server *server,
	struct wlr_surface *surface);

#endif
