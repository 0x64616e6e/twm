#include <stdlib.h>
#include <time.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/util/log.h>
#include "grab.h"
#include "intent.h"
#include "view.h"

static uint32_t now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint32_t)((uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000);
}

/* The view whose frame is under the pointer, or NULL for the root. Sets
 * *unmanaged when the pointer is over an X11 menu or tooltip. */
static struct twm_view *view_under_pointer(struct twm_server *server, bool *unmanaged) {
	double sx, sy;
	struct wlr_surface *surface;
	struct twm_hit hit = server_hit_at(server, server->cursor->x, server->cursor->y,
		&surface, &sx, &sy);
	*unmanaged = hit.kind == TWM_HIT_UNMANAGED;
	return hit.view && hit.view->shown ? hit.view : NULL;
}

static int handle_timer(void *data) {
	struct twm_server *server = data;
	if (grab_active(server)) {
		return 0; /* FOC-8 */
	}
	bool unmanaged;
	struct twm_view *under = view_under_pointer(server, &unmanaged);
	uint32_t now = now_ms();
	const void *target = focus_intent_fire(&server->intent, under, now,
		(uint32_t)server->theme.focus_delay_ms);
	if (target) {
		view_focus((struct twm_view *)target);
	} else if (under && under == server->intent.candidate) {
		/* Timer granularity: not quite rested long enough yet. */
		uint32_t elapsed = now - server->intent.since;
		uint32_t delay = (uint32_t)server->theme.focus_delay_ms;
		wl_event_source_timer_update(server->focus_timer,
			(int)(elapsed < delay ? delay - elapsed : 1));
	}
	return 0;
}

void focus_pointer_moved(struct twm_server *server) {
	if (grab_active(server)) {
		return; /* FOC-8 */
	}
	bool unmanaged;
	struct twm_view *under = view_under_pointer(server, &unmanaged);
	if (unmanaged) {
		return; /* an X11 menu belongs to whoever opened it */
	}
	uint32_t deadline = 0;
	uint32_t delay = (uint32_t)server->theme.focus_delay_ms;
	switch (focus_intent_motion(&server->intent, under, server->focused,
			now_ms(), delay, &deadline)) {
	case INTENT_NONE:
		break;
	case INTENT_CANCEL:
		wl_event_source_timer_update(server->focus_timer, 0);
		break;
	case INTENT_ARM:
		wl_event_source_timer_update(server->focus_timer, (int)delay);
		break;
	case INTENT_COMMIT:
		view_focus(under);
		break;
	}
}

void focus_cancel_pending(struct twm_server *server) {
	wl_event_source_timer_update(server->focus_timer, 0);
	focus_intent_reset(&server->intent, server->focused, now_ms());
}

void focus_changed(struct twm_server *server) {
	focus_cancel_pending(server);
}

/*
 * URG-1 (simple form in M1, URG-2): activation is honoured only for a token
 * issued for user input to the focused client; anything else marks the
 * window urgent. A window never takes focus because it asked.
 */
static void handle_request_activate(struct wl_listener *listener, void *data) {
	struct twm_server *server = wl_container_of(listener, server, request_activate);
	struct wlr_xdg_activation_v1_request_activate_event *event = data;
	struct twm_view *view = view_from_surface(server, event->surface);
	if (!view) {
		return;
	}
	struct wlr_xdg_activation_token_v1 *token = event->token;
	struct twm_view *requester = token->surface ?
		view_from_surface(server, token->surface) : NULL;
	if (token->seat && requester && requester == server->focused) {
		view_raise(view);
		view_focus(view);
	} else {
		wlr_log(WLR_DEBUG, "'%s' asked for focus; marked urgent", view_title(view));
		view_set_urgent(view, true);
	}
}

void focus_init(struct twm_server *server) {
	server->focus_timer = wl_event_loop_add_timer(server->loop, handle_timer, server);
	server->xdg_activation = wlr_xdg_activation_v1_create(server->display);
	server->request_activate.notify = handle_request_activate;
	wl_signal_add(&server->xdg_activation->events.request_activate,
		&server->request_activate);
}

void focus_finish(struct twm_server *server) {
	if (server->focus_timer) {
		wl_event_source_remove(server->focus_timer);
		server->focus_timer = NULL;
	}
	if (server->xdg_activation) {
		wl_list_remove(&server->request_activate.link);
		server->xdg_activation = NULL;
	}
}
