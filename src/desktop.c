#include <wlr/types/wlr_seat.h>
#include "desktop.h"
#include "grab.h"
#include "group.h"
#include "histview.h"
#include "icons.h"
#include "settings.h"
#include "ui.h"

bool view_visible(struct twm_view *view) {
	return view->shown && !view->iconified && !view_hidden_member(view) &&
		(view->sticky || view->desktop == view->server->desktop);
}

void view_update_visibility(struct twm_view *view) {
	wlr_scene_node_set_enabled(&view->tree->node, view_visible(view));
	if (view->content) {
		wlr_scene_node_set_enabled(&view->content->node, !view->squeezed);
	}
}

int desktop_count(struct twm_server *server) {
	return server->config ? server->config->desktops : 4;
}

int desktop_window_count(struct twm_server *server, int desktop) {
	int n = 0;
	struct twm_view *v;
	wl_list_for_each(v, &server->views, link) {
		n += v->type != TWM_VIEW_INTERNAL && v->desktop == desktop;
	}
	return n;
}

/*
 * FOC-10 when the focused window leaves the screen: the most recently focused
 * window still on view. `pick` also fills empty focus (a desktop switch);
 * otherwise nothing gains focus that didn't have it (FOC-9).
 */
static void refocus_pick(struct twm_server *server, bool pick) {
	if (!server->seat) {
		return; /* the config is applied before input exists */
	}
	if (server->focused ? view_visible(server->focused) : !pick) {
		return;
	}
	struct twm_view *best = NULL, *v;
	wl_list_for_each(v, &server->views, link) {
		if (view_visible(v) && v->type != TWM_VIEW_INTERNAL &&
				(!best || v->focus_seq > best->focus_seq)) {
			best = v;
		}
	}
	if (best) {
		view_focus(best);
	} else {
		if (server->focused) {
			struct twm_view *old = server->focused;
			server->focused = NULL;
			old->impl->set_activated(old, false);
			view_update_frame(old);
		}
		wlr_seat_keyboard_notify_clear_focus(server->seat);
	}
}

void desktop_switch(struct twm_server *server, int desktop) {
	if (desktop < 1 || desktop > desktop_count(server) || desktop == server->desktop) {
		return;
	}
	server->desktop = desktop; /* DSK-2: instant */
	struct twm_view *v;
	wl_list_for_each(v, &server->views, link) {
		view_update_visibility(v);
	}
	refocus_pick(server, true);
	input_update_pointer_focus(server, 0);
	ui_changed(server);
}

void desktop_send(struct twm_server *server, struct twm_view *view, int desktop) {
	if (!view || view->sticky || desktop < 1 || desktop > desktop_count(server) ||
			desktop == view->desktop) {
		return;
	}
	struct hist_state before = hist_state_of(view);
	view->desktop = desktop;
	if (view->group) {
		struct twm_view *m; /* GRP-7: the whole group goes */
		wl_list_for_each(m, &view->group->members, group_link) {
			m->desktop = desktop;
		}
	}
	view_update_visibility(view);
	histview_record(server, HIST_SEND, view, before, hist_state_of(view));
	refocus_pick(server, false);
	input_update_pointer_focus(server, 0);
	ui_changed(server);
}

void desktops_reconfigure(struct twm_server *server) {
	int n = desktop_count(server);
	struct twm_view *v;
	wl_list_for_each(v, &server->views, link) {
		if (v->desktop > n) {
			v->desktop = n;
		}
	}
	if (server->desktop > n) {
		server->desktop = n;
	}
	wl_list_for_each(v, &server->views, link) {
		view_update_visibility(v);
	}
	refocus_pick(server, false);
	ui_changed(server);
}

static void set_iconified(struct twm_view *view, bool iconified) {
	view->iconified = iconified;
	if (iconified) {
		icon_create(view);
	} else {
		icon_destroy(view);
	}
	view_update_visibility(view);
}

void view_apply_state(struct twm_view *view, struct hist_state s) {
	struct twm_server *server = view->server;
	/* GRP-8: grouping first; a joined window takes the group's place. */
	if (!s.group_with && view->group) {
		group_leave(view);
	} else if (s.group_with) {
		struct twm_view *w;
		wl_list_for_each(w, &server->views, link) {
			if (w->id == s.group_with && (!view->group || view->group != w->group)) {
				struct twm_view *target = w->group ? w->group->active : w;
				group_join(view, target, -1);
				break;
			}
		}
		if (view->group) {
			ui_changed(server);
			return;
		}
	}
	view_move_resize(view, s.x, s.y, s.w, s.h);
	if (!view->sticky && s.desktop >= 1 && s.desktop <= desktop_count(server)) {
		view->desktop = s.desktop;
	}
	if (view->iconified != s.iconified) {
		set_iconified(view, s.iconified);
	}
	view->squeezed = s.squeezed;
	view_update_visibility(view);
	view_update_frame(view);
	refocus_pick(server, false);
	input_update_pointer_focus(server, 0);
	ui_changed(server);
}

void view_set_iconified(struct twm_view *view, bool iconified) {
	struct twm_server *server = view->server;
	if (view->iconified == iconified || view->utility || !view->shown) {
		return;
	}
	grab_view_gone(server, view);
	struct hist_state before = hist_state_of(view);
	set_iconified(view, iconified);
	histview_record(server, iconified ? HIST_ICONIFY : HIST_DEICONIFY, view, before,
		hist_state_of(view));
	if (!iconified) {
		view_raise(view);
		view_focus(view);
	}
	refocus_pick(server, false);
	input_update_pointer_focus(server, 0);
	ui_changed(server);
}

void view_set_squeezed(struct twm_view *view, bool squeezed) {
	struct twm_server *server = view->server;
	if (view->squeezed == squeezed || view->utility) {
		return;
	}
	struct hist_state before = hist_state_of(view);
	view->squeezed = squeezed;
	view_update_visibility(view);
	view_update_frame(view);
	histview_record(server, HIST_SQUEEZE, view, before, hist_state_of(view));
	input_update_pointer_focus(server, 0);
	ui_changed(server);
}

void view_present(struct twm_view *view) {
	struct twm_server *server = view->server;
	if (view_hidden_member(view)) {
		group_activate(view); /* a tab of a group: bring it to the front */
	}
	if (!view->sticky && view->desktop != server->desktop) {
		desktop_switch(server, view->desktop);
	}
	if (view->iconified) {
		view_set_iconified(view, false);
	}
	view_raise(view);
	view_focus(view);
	ui_changed(server);
}

void view_bring_here(struct twm_view *view) {
	struct twm_server *server = view->server;
	if (!view->sticky && view->desktop != server->desktop) {
		desktop_send(server, view, server->desktop);
	}
	view_present(view);
}
