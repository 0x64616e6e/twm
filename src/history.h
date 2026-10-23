#ifndef TWM_HISTORY_H
#define TWM_HISTORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

/*
 * Window operation history (HIS-1 … HIS-4). Pure: windows are opaque ids,
 * states are frame positions and client sizes. Undo is per window (HIS-2);
 * the log is shared and keeps the last 100 operations (HIS-4).
 */

#define HIST_MAX 100

enum hist_op {
	HIST_PLACE,
	HIST_MOVE,
	HIST_RESIZE,
	HIST_ICONIFY,
	HIST_DEICONIFY,
	HIST_SQUEEZE,
	HIST_GROUP,
	HIST_UNGROUP,
	HIST_SEND,
	HIST_WRITE,
};

struct hist_state {
	int x, y; /* frame top-left, layout coordinates */
	int w, h; /* client size */
	int desktop;
	bool iconified, squeezed;
	uint64_t group_with; /* another member of its group; 0 = alone */
};

bool hist_state_equal(struct hist_state a, struct hist_state b);

struct hist_entry {
	enum hist_op op;
	uint64_t window;
	char target[64]; /* the window's name when it happened */
	time_t when;
	struct hist_state before, after;
	bool undone;
};

struct history {
	struct hist_entry e[HIST_MAX]; /* oldest first */
	size_t n;
};

/* Record an operation; it clears that window's redo (HIS-2). */
void history_record(struct history *h, enum hist_op op, uint64_t window,
	const char *target, time_t when, struct hist_state before, struct hist_state after);

/* The entry to undo for `window` (apply its `before`), or NULL. Placing a
 * window can't be undone: there is no earlier position. */
const struct hist_entry *history_undo(struct history *h, uint64_t window);
/* The entry to redo (apply its `after`), or NULL. */
const struct hist_entry *history_redo(struct history *h, uint64_t window);

/* Newest-first access for the history view: i = 0 is the latest. */
const struct hist_entry *history_newest(const struct history *h, size_t i);

const char *history_op_name(enum hist_op op);
/* "now", "2m", "1h", "3d" (B3). */
void history_age(time_t now, time_t when, char *buf, size_t len);

#endif
