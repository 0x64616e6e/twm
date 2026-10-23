#ifndef TWM_GROUP_H
#define TWM_GROUP_H

#include <stdbool.h>
#include <stddef.h>
#include <wayland-server-core.h>

/*
 * Tab groups (GRP-*): windows sharing one frame. The active member is on
 * screen and draws the whole tab row; the others are hidden at the same
 * place and size. Moving, resizing, sending or iconifying the active member
 * carries the rest along.
 */

struct twm_view;

struct twm_group {
	struct wl_list members; /* twm_view.group_link, in tab order */
	struct twm_view *active;
	bool folded; /* GRP-6 */
};

/* `view` joins `target`'s group (making one if needed) at tab `index`
 * (-1 = end) and becomes its active tab (GRP-1). */
void group_join(struct twm_view *view, struct twm_view *target, int index);
/* `view` leaves its group and stands alone where it is (GRP-5). */
void group_leave(struct twm_view *view);
/* Make `view` its group's active tab (GRP-4). */
void group_activate(struct twm_view *view);
/* M-Tab: the next tab of the focused group (GRP-4). */
void group_cycle(struct twm_view *view, int dir);
void group_set_folded(struct twm_group *group, bool folded);

size_t group_size(struct twm_group *group);
int group_index(struct twm_view *view);
struct twm_view *group_member(struct twm_group *group, int index);
/* A member that isn't the active tab: never on screen by itself. */
bool view_hidden_member(struct twm_view *view);

/* The active member moved or resized: bring the others along. */
void group_sync_geometry(struct twm_view *active, int x, int y, int w, int h);

#endif
