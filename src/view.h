#ifndef TWM_VIEW_H
#define TWM_VIEW_H

#include <stdbool.h>
#include <stdint.h>
#include <wayland-server-core.h>
#include <wlr/util/box.h>
#include "server.h"

/*
 * One managed toplevel, whatever its shell (xdg-shell or XWayland). The
 * shell-specific code fills in `impl` and drives map/unmap/commit; the rest
 * of twm talks only to this struct.
 */

enum twm_view_type {
	TWM_VIEW_XDG,
	TWM_VIEW_XWAYLAND,
	TWM_VIEW_INTERNAL, /* drawn by twm (internal.c) */
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
	/* Parent window for transients, or NULL (OUT-15). */
	struct twm_view *(*get_parent)(struct twm_view *view);
	/* A position the user asked for (X11 USPosition), client top-left. */
	bool (*get_user_position)(struct twm_view *view, int *x, int *y);
};

struct twm_frame;
struct twm_icon;

struct twm_view {
	struct wl_list link;       /* twm_server.views, only while shown */
	struct wl_list place_link; /* twm_server.place_queue while waiting */
	struct twm_server *server;
	enum twm_view_type type;
	const struct twm_view_impl *impl;
	uint64_t id; /* stable, for the history (HIS-1) */

	struct wlr_surface *surface;
	struct wlr_scene_tree *tree;    /* at the frame's outer top-left */
	struct wlr_scene_tree *content; /* client surfaces live here */
	struct twm_frame *frame;
	struct twm_hit client_hit;

	int x, y;           /* frame outer top-left, layout coordinates */
	int width, height;  /* client size */
	int geom_x, geom_y; /* xdg window-geometry offset inside the surface */

	bool mapped;  /* the client mapped its surface */
	bool shown;   /* placed and on screen */
	bool placing; /* waiting for click-to-place (OUT-12) */
	bool urgent;  /* FRM-9, URG-* */
	bool notitle; /* NoTitle: border only (FRM-15) */
	bool utility; /* FRM-18: title-only tab, no buttons */
	int desktop;    /* DSK-1, 1-based */
	bool sticky;    /* on every desktop (twm's own windows) */
	bool iconified; /* FRM-13 */
	bool squeezed;  /* FRM-14: shaded to its tab */
	struct twm_icon *icon; /* ROOT-9, while iconified */
	uint64_t focus_seq; /* when it last had focus (FOC-10) */
};

void view_init(struct twm_view *view, struct twm_server *server,
	enum twm_view_type type, const struct twm_view_impl *impl);
void view_finish(struct twm_view *view);
void view_set_content(struct twm_view *view, struct wlr_scene_tree *content);

void view_map(struct twm_view *view);
void view_unmap(struct twm_view *view);
/* Put a placed view on screen at its current x/y. */
void view_show(struct twm_view *view, bool focus);

/* The client committed a new window geometry (surface-local). */
void view_update_geometry(struct twm_view *view, int gx, int gy,
	int width, int height);
/* Title, app id, size limits or similar changed. */
void view_update_frame(struct twm_view *view);

void view_move(struct twm_view *view, int x, int y);
/* Frame position plus a new client size (OUT-1: one configure). */
void view_move_resize(struct twm_view *view, int x, int y, int w, int h);
void view_raise(struct twm_view *view);
void view_lower(struct twm_view *view);
/* The theme or the window's title rules changed: offsets, frame, client. */
void view_relayout(struct twm_view *view);
/* Title-band height for this view: 0 for NoTitle windows. */
int view_title_height(struct twm_view *view);
void view_focus(struct twm_view *view);
void view_set_urgent(struct twm_view *view, bool urgent);

/* Offset of the client area inside the frame (FRM-2). */
void view_client_offset(struct twm_view *view, int *dx, int *dy);
/* Frame outer box in layout coordinates. */
struct wlr_box view_frame_box(struct twm_view *view);
/* Frame size for a client size. */
void view_frame_size(struct twm_server *server, int cw, int ch, int *fw, int *fh);

const char *view_title(struct twm_view *view);

struct twm_view *view_from_surface(struct twm_server *server,
	struct wlr_surface *surface);

#endif
