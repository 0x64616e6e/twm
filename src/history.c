#include <stdio.h>
#include <string.h>
#include "history.h"

static void drop(struct history *h, size_t i) {
	memmove(&h->e[i], &h->e[i + 1], (h->n - i - 1) * sizeof(h->e[0]));
	h->n--;
}

void history_record(struct history *h, enum hist_op op, uint64_t window,
		const char *target, time_t when, struct hist_state before, struct hist_state after) {
	for (size_t i = h->n; i-- > 0;) {
		if (h->e[i].window == window && h->e[i].undone) {
			drop(h, i);
		}
	}
	if (h->n == HIST_MAX) {
		drop(h, 0);
	}
	struct hist_entry *e = &h->e[h->n++];
	*e = (struct hist_entry){
		.op = op, .window = window, .when = when, .before = before, .after = after,
	};
	snprintf(e->target, sizeof(e->target), "%s", target ? target : "");
}

const struct hist_entry *history_undo(struct history *h, uint64_t window) {
	for (size_t i = h->n; i-- > 0;) {
		struct hist_entry *e = &h->e[i];
		if (e->window != window || e->undone) {
			continue;
		}
		if (e->op == HIST_PLACE) {
			return NULL;
		}
		e->undone = true;
		return e;
	}
	return NULL;
}

const struct hist_entry *history_redo(struct history *h, uint64_t window) {
	/* Undone entries are always the newest of their window; redo the oldest
	 * of them. */
	for (size_t i = 0; i < h->n; i++) {
		struct hist_entry *e = &h->e[i];
		if (e->window == window && e->undone) {
			e->undone = false;
			return e;
		}
	}
	return NULL;
}

const struct hist_entry *history_newest(const struct history *h, size_t i) {
	return i < h->n ? &h->e[h->n - 1 - i] : NULL;
}

bool hist_state_equal(struct hist_state a, struct hist_state b) {
	return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h &&
		a.desktop == b.desktop && a.iconified == b.iconified && a.squeezed == b.squeezed;
}

const char *history_op_name(enum hist_op op) {
	static const char *const names[] = {
		"place", "move", "resize", "iconify", "deiconify", "squeeze", "group",
		"ungroup", "send", "write",
	};
	return (unsigned)op < sizeof(names) / sizeof(names[0]) ? names[op] : "?";
}

void history_age(time_t now, time_t when, char *buf, size_t len) {
	long s = (long)(now - when);
	if (s < 60) {
		snprintf(buf, len, "now");
	} else if (s < 3600) {
		snprintf(buf, len, "%ldm", s / 60);
	} else if (s < 86400) {
		snprintf(buf, len, "%ldh", s / 3600);
	} else {
		snprintf(buf, len, "%ldd", s / 86400);
	}
}
