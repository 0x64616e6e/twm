#include <stdlib.h>
#include "desktop.h"
#include "group.h"
#include "ui.h"
#include "view.h"

size_t group_size(struct twm_group *g) {
	return g ? (size_t)wl_list_length(&g->members) : 0;
}

int group_index(struct twm_view *view) {
	if (!view->group) {
		return 0;
	}
	int i = 0;
	struct twm_view *v;
	wl_list_for_each(v, &view->group->members, group_link) {
		if (v == view) {
			return i;
		}
		i++;
	}
	return -1;
}

struct twm_view *group_member(struct twm_group *g, int index) {
	int i = 0;
	struct twm_view *v;
	wl_list_for_each(v, &g->members, group_link) {
		if (i++ == index) {
			return v;
		}
	}
	return NULL;
}

bool view_hidden_member(struct twm_view *view) {
	return view->group && view->group->active != view;
}

void group_sync_geometry(struct twm_view *active, int x, int y, int w, int h) {
	struct twm_group *g = active->group;
	if (!g || g->active != active) {
		return;
	}
	struct twm_view *v;
	wl_list_for_each(v, &g->members, group_link) {
		if (v == active) {
			continue;
		}
		v->x = x;
		v->y = y;
		wlr_scene_node_set_position(&v->tree->node, x, y);
		int dx, dy;
		view_client_offset(v, &dx, &dy);
		v->impl->configure(v, x + dx, y + dy, w, h); /* GRP-3: one size for all */
	}
}

static void refresh(struct twm_group *g) {
	struct twm_view *v;
	wl_list_for_each(v, &g->members, group_link) {
		view_update_visibility(v);
		view_update_frame(v);
	}
}

/* Hand the group's place on screen from `from` to `to`. */
static void hand_over(struct twm_view *from, struct twm_view *to) {
	struct twm_server *server = to->server;
	to->desktop = from->desktop;
	to->sticky = from->sticky;
	to->squeezed = from->squeezed;
	if (to->x != from->x || to->y != from->y || to->width != from->width ||
			to->height != from->height) {
		view_move_resize(to, from->x, from->y, from->width, from->height);
	}
	/* Take its stacking place. */
	wlr_scene_node_place_above(&to->tree->node, &from->tree->node);
	if (to->shown && from->shown) {
		wl_list_remove(&to->link);
		wl_list_insert(from->link.prev, &to->link);
	}
	(void)server;
}

void group_join(struct twm_view *view, struct twm_view *target, int index) {
	struct twm_server *server = view->server;
	if (view == target || (view->group && view->group == target->group)) {
		return;
	}
	if (view->group) {
		group_leave(view);
	}
	if (view->iconified) {
		view_set_iconified(view, false);
	}
	struct twm_group *g = target->group;
	if (!g) {
		g = calloc(1, sizeof(*g));
		if (!g) {
			return;
		}
		wl_list_init(&g->members);
		wl_list_insert(&g->members, &target->group_link);
		target->group = g;
		g->active = target;
	}
	struct twm_view *anchor = index >= 0 ? group_member(g, index) : NULL;
	if (anchor) {
		wl_list_insert(anchor->group_link.prev, &view->group_link);
	} else {
		wl_list_insert(g->members.prev, &view->group_link);
	}
	view->group = g;
	struct twm_view *old = g->active;
	g->active = view; /* the dropped tab comes to the front */
	view->squeezed = false;
	hand_over(old, view);
	refresh(g);
	view_focus(view);
	input_update_pointer_focus(server, 0);
	ui_changed(server);
}

void group_leave(struct twm_view *view) {
	struct twm_group *g = view->group;
	if (!g) {
		return;
	}
	struct twm_server *server = view->server;
	bool was_active = g->active == view;
	struct twm_view *next = NULL;
	if (was_active) {
		/* The tab beside it takes over: the next one, else the previous. */
		struct wl_list *n = view->group_link.next != &g->members ?
			view->group_link.next : view->group_link.prev;
		next = n != &g->members ? wl_container_of(n, next, group_link) : NULL;
	}
	wl_list_remove(&view->group_link);
	wl_list_init(&view->group_link);
	view->group = NULL;
	if (next) {
		g->active = next;
		hand_over(view, next);
		if (view->server->focused == view) {
			view_focus(next);
		}
	}
	view_update_visibility(view);
	view_update_frame(view);
	if (group_size(g) <= 1) {
		/* One left: it's an ordinary window again. */
		struct twm_view *last = group_size(g) ? group_member(g, 0) : NULL;
		if (last) {
			wl_list_remove(&last->group_link);
			wl_list_init(&last->group_link);
			last->group = NULL;
			view_update_visibility(last);
			view_update_frame(last);
		}
		free(g);
	} else {
		refresh(g);
	}
	input_update_pointer_focus(server, 0);
	ui_changed(server);
}

void group_activate(struct twm_view *view) {
	struct twm_group *g = view->group;
	if (!g || g->active == view) {
		return;
	}
	struct twm_view *old = g->active;
	bool had_focus = view->server->focused == old;
	g->active = view;
	view->iconified = false;
	hand_over(old, view);
	refresh(g);
	if (had_focus) {
		view_focus(view);
	}
	input_update_pointer_focus(view->server, 0);
	ui_changed(view->server);
}

void group_cycle(struct twm_view *view, int dir) {
	struct twm_group *g = view ? view->group : NULL;
	if (!g || group_size(g) < 2) {
		return;
	}
	struct twm_view *a = g->active;
	struct wl_list *n = dir > 0 ? a->group_link.next : a->group_link.prev;
	if (n == &g->members) {
		n = dir > 0 ? n->next : n->prev;
	}
	struct twm_view *next = wl_container_of(n, next, group_link);
	g->active = next;
	hand_over(a, next);
	refresh(g);
	view_focus(next);
	input_update_pointer_focus(view->server, 0);
	ui_changed(view->server);
}

void group_set_folded(struct twm_group *g, bool folded) {
	if (!g || g->folded == folded) {
		return;
	}
	g->folded = folded;
	view_update_frame(g->active);
	ui_changed(g->active->server);
}
