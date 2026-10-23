/* Unit tests for the operation history (HIS-1 … HIS-4). */
#include <string.h>
#include "check.h"
#include "history.h"

static struct hist_state st(int x) {
	return (struct hist_state){x, x, 100 + x, 100 + x};
}

int main(void) {
	static struct history h;
	history_record(&h, HIST_PLACE, 1, "xterm", 0, st(0), st(10));
	history_record(&h, HIST_MOVE, 1, "xterm", 1, st(10), st(20));
	history_record(&h, HIST_MOVE, 2, "zpool", 2, st(5), st(6));
	history_record(&h, HIST_RESIZE, 1, "xterm", 3, st(20), st(30));

	/* HIS-2: undo is per window, newest first. */
	const struct hist_entry *e = history_undo(&h, 1);
	CHECK("HIS-2", e && e->op == HIST_RESIZE && e->before.x == 20);
	e = history_undo(&h, 1);
	CHECK("HIS-2", e && e->op == HIST_MOVE && e->before.x == 10);
	/* Place isn't undoable; window 2's move is untouched. */
	CHECK("HIS-2", history_undo(&h, 1) == NULL);
	CHECK("HIS-2", !h.e[2].undone);

	/* Redo replays in order. */
	e = history_redo(&h, 1);
	CHECK("HIS-2", e && e->op == HIST_MOVE && e->after.x == 20);
	e = history_undo(&h, 1);
	CHECK("HIS-2", e && e->op == HIST_MOVE);
	/* A new operation clears this window's redo. */
	history_record(&h, HIST_MOVE, 1, "xterm", 4, st(10), st(40));
	CHECK("HIS-2", history_redo(&h, 1) == NULL);
	CHECK_EQ("HIS-1", h.n, 3); /* place, zpool move, the new move; the undone two are gone */
	CHECK("HIS-1", strcmp(history_newest(&h, 0)->target, "xterm") == 0 &&
		history_newest(&h, 0)->after.x == 40);
	CHECK("HIS-1", history_newest(&h, 1)->op == HIST_MOVE &&
		strcmp(history_newest(&h, 1)->target, "zpool") == 0);
	CHECK("HIS-2", history_undo(&h, 2) && history_redo(&h, 2));

	/* HIS-4: 100 at most, oldest dropped. */
	for (int i = 0; i < 150; i++) {
		history_record(&h, HIST_MOVE, 3, "notes", 10 + i, st(i), st(i + 1));
	}
	CHECK_EQ("HIS-4", h.n, HIST_MAX);
	CHECK("HIS-4", history_newest(&h, HIST_MAX - 1)->before.x == 50);

	char age[16];
	history_age(1000, 990, age, sizeof(age));
	CHECK("HIS-3", strcmp(age, "now") == 0);
	history_age(1000, 1000 - 125, age, sizeof(age));
	CHECK("HIS-3", strcmp(age, "2m") == 0);
	history_age(100000, 0, age, sizeof(age));
	CHECK("HIS-3", strcmp(age, "1d") == 0);
	CHECK("HIS-1", strcmp(history_op_name(HIST_SQUEEZE), "squeeze") == 0);
	return UNIT_RESULT();
}
