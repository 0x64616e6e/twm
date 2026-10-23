#ifndef TWM_OUTLINE_H
#define TWM_OUTLINE_H

#include "geometry.h"
#include "server.h"

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
};

void outline_init(struct twm_server *server);
void outline_finish(struct twm_server *server);

/* Show (or move) the outline for frame box `box`; `hint` may be NULL. */
void outline_show(struct twm_server *server, struct geo_box box,
	const char *label, const char *hint);
void outline_hide(struct twm_server *server);

#endif
