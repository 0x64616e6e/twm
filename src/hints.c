#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/types/wlr_cursor.h>
#include "desktop.h"
#include "hintcode.h"
#include "hints.h"
#include "server.h"
#include "settings.h"
#include "view.h"

#define HINTS_MAX 64
#define SELECTED_MS 60 /* HNT-3: shown selected briefly (P-3: under 80 ms) */

struct twm_hints {
	struct twm_view *views[HINTS_MAX];
	size_t n;
	char typed[4];
	struct twm_view *chosen;
	struct wl_event_source *timer;
};

bool hints_active(struct twm_server *server) {
	return server->hints != NULL;
}

static void clear(struct twm_server *server) {
	struct twm_hints *h = server->hints;
	for (size_t i = 0; i < h->n; i++) {
		if (h->views[i]) {
			h->views[i]->hint[0] = '\0';
			h->views[i]->hint_selected = false;
			view_update_frame(h->views[i]);
		}
	}
	if (h->timer) {
		wl_event_source_remove(h->timer);
	}
	free(h);
	server->hints = NULL;
}

void hints_cancel(struct twm_server *server) {
	if (server->hints) {
		clear(server);
	}
}

void hints_finish(struct twm_server *server) {
	hints_cancel(server);
}

void hints_view_gone(struct twm_server *server, struct twm_view *view) {
	struct twm_hints *h = server->hints;
	if (!h) {
		return;
	}
	for (size_t i = 0; i < h->n; i++) {
		if (h->views[i] == view) {
			h->views[i] = NULL;
		}
	}
	if (h->chosen == view) {
		clear(server);
	}
}

/* Distance from the pointer to a frame; 0 inside it. */
static double distance(struct twm_view *v, double px, double py) {
	struct wlr_box f = view_frame_box(v);
	double dx = px < f.x ? f.x - px : px > f.x + f.width ? px - (f.x + f.width) : 0;
	double dy = py < f.y ? f.y - py : py > f.y + f.height ? py - (f.y + f.height) : 0;
	return sqrt(dx * dx + dy * dy);
}

void hints_begin(struct twm_server *server) {
	if (server->hints) {
		hints_cancel(server);
		return;
	}
	struct twm_hints *h = calloc(1, sizeof(*h));
	if (!h) {
		return;
	}
	double px = server->cursor->x, py = server->cursor->y;
	double dist[HINTS_MAX];
	struct twm_view *v;
	/* Every visible tab on this desktop (HNT-1); stacking order breaks ties. */
	wl_list_for_each(v, &server->views, link) {
		if (h->n == HINTS_MAX || !view_visible(v) || v->utility || v->notitle) {
			continue;
		}
		size_t i = h->n++;
		double d = distance(v, px, py);
		while (i > 0 && dist[i - 1] > d) { /* stable: the higher window wins a tie */
			h->views[i] = h->views[i - 1];
			dist[i] = dist[i - 1];
			i--;
		}
		h->views[i] = v;
		dist[i] = d;
	}
	if (h->n == 0) {
		free(h);
		return;
	}
	char codes[HINTS_MAX][4];
	size_t made = hint_codes(h->n, server->config->hint_keys, codes);
	h->n = made;
	for (size_t i = 0; i < h->n; i++) {
		memcpy(h->views[i]->hint, codes[i], sizeof(codes[i]));
		view_update_frame(h->views[i]);
	}
	server->hints = h;
}

static int handle_timer(void *data) {
	struct twm_server *server = data;
	struct twm_hints *h = server->hints;
	struct twm_view *chosen = h ? h->chosen : NULL;
	hints_cancel(server);
	if (chosen) {
		view_present(chosen); /* HNT-3: raise and focus */
	}
	return 0;
}

void hints_key(struct twm_server *server, xkb_keysym_t sym, const char *utf8) {
	struct twm_hints *h = server->hints;
	if (!h || h->chosen) {
		return;
	}
	if ((sym >= XKB_KEY_Shift_L && sym <= XKB_KEY_Hyper_R) || sym == XKB_KEY_ISO_Level3_Shift) {
		return; /* modifiers alone don't count */
	}
	size_t len = strlen(h->typed);
	if (sym == XKB_KEY_Escape || !utf8[0] || utf8[1] || len + 1 >= sizeof(h->typed)) {
		hints_cancel(server); /* HNT-4 */
		return;
	}
	h->typed[len] = utf8[0];
	h->typed[len + 1] = '\0';
	size_t matching = 0;
	struct twm_view *exact = NULL;
	for (size_t i = 0; i < h->n; i++) {
		struct twm_view *v = h->views[i];
		if (!v) {
			continue;
		}
		if (strncmp(v->hint, h->typed, len + 1) == 0) {
			matching++;
			if (strcmp(v->hint, h->typed) == 0) {
				exact = v;
			}
		} else if (v->hint[0]) {
			v->hint[0] = '\0'; /* two-letter codes: the others drop out */
			view_update_frame(v);
		}
	}
	if (!matching) {
		hints_cancel(server); /* HNT-4: not a hint */
		return;
	}
	if (exact) {
		exact->hint_selected = true;
		view_update_frame(exact);
		h->chosen = exact;
		h->timer = wl_event_loop_add_timer(server->loop, handle_timer, server);
		wl_event_source_timer_update(h->timer, SELECTED_MS);
	}
}
