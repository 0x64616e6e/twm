#ifndef TWM_OUTLINE_H
#define TWM_OUTLINE_H

#include "geometry.h"
#include "server.h"
#include "snap.h"

/*
 * The rubber-band outline with its rule-of-thirds grid and geometry readout
 * (OUT-2, OUT-3), drawn in the overlay layer above everything.
 */

struct twm_outline {
	struct twm_server *server;
	struct wlr_scene_tree *tree;
	struct wlr_scene_rect *edge[4];
	struct wlr_scene_rect *title_line;
	struct wlr_scene_rect *third[4];
	struct wlr_scene_buffer *label;
	struct wlr_scene_buffer *readout;
	char *label_text;
	float label_scale;

	/* SNP-3, SNP-4, SNP-6: guides across the output */
	struct wlr_scene_tree *guides;
	struct wlr_scene_buffer *faint[4];   /* thirds: 2 vertical, 2 horizontal */
	struct wlr_scene_buffer *line[2];    /* snapped: vertical, horizontal */
	struct wlr_scene_buffer *line_label[2];
	struct wlr_scene_buffer *output_label;

	/* GRP-2: the dragged tab and its label */
	struct wlr_scene_buffer *float_tab;
	struct wlr_scene_buffer *drop_label;
};

void outline_init(struct twm_server *server);
void outline_finish(struct twm_server *server);

/* Show (or move) the outline for frame box `box`; `hint` may be NULL. */
void outline_show(struct twm_server *server, struct geo_box box,
	const char *label, const char *hint);
void outline_hide(struct twm_server *server);

/* Guides for the output `out`: faint thirds when `thirds`, a dashed accent
 * line and label for each snapped axis, and the output's name, mode and
 * scale in the corner. */
void outline_guides(struct twm_server *server, struct geo_box out,
	const struct snap_result *snap, bool thirds, const char *output_label);

/* GRP-2: a floating tab titled `title` with the pointer at (px, py), and
 * `label` (DROP → GROUP OF N) beside it; returns the tab's logical width. */
int outline_drop(struct twm_server *server, double px, double py, const char *title,
	const char *label);
/* The width a tab titled `title` takes (for the drop slot). */
int outline_tab_width(struct twm_server *server, const char *title);

#endif
