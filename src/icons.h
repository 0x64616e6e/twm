#ifndef TWM_ICONS_H
#define TWM_ICONS_H

#include <stdbool.h>
#include "server.h"

/*
 * Root icons (ROOT-9, ROOT-10): an iconified window's icon on the root,
 * laid out from (72, H − 140) in 72 px slots unless dragged elsewhere.
 * Icons of the current desktop show.
 */

struct twm_view;

struct twm_icon {
	struct twm_view *view;
	struct wlr_scene_tree *tree;
	struct wlr_scene_buffer *buffer;
	struct twm_hit hit;
	int x, y;          /* slot top-left, layout coordinates */
	bool user_placed;  /* dragged with B2: stays where it was put */
	uint64_t order;    /* iconify order, for the slots */
	int width;         /* drawn width (labels may be wider than a slot) */
};

void icon_create(struct twm_view *view);
void icon_destroy(struct twm_view *view);
void icon_move(struct twm_view *view, int x, int y);
void icons_layout(struct twm_server *server);

/* "htop", "irc · urgent", "mutt · 4" (ROOT-9). */
void icon_label(const char *title, const char *app_id, bool urgent, char *buf, size_t len);

#endif
