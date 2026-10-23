/* Unit tests for remembered geometry (CFG-11). */
#include <stdlib.h>
#include <string.h>
#include "check.h"
#include "geomem.h"

int main(void) {
	struct geomem gm = {0};
	geomem_parse(&gm, "firefox\tDP-1\t40 60 1118 872\n"
		"bad line\n"
		"foot\tHEADLESS-1\t-10 5 640 400\n"
		"\tDP-1\t1 2 3 4\n"
		"zero\tDP-1\t1 2 0 4\n"
		"firefox\tDP-2\t0 0 10 10\n");
	CHECK_EQ("CFG-11", gm.n, 2);
	const struct geomem_entry *e = geomem_find(&gm, "firefox");
	CHECK("CFG-11", e && strcmp(e->output, "DP-1") == 0 && e->x == 40 && e->w == 1118 &&
		e->from_disk);
	e = geomem_find(&gm, "foot");
	CHECK("CFG-11", e && e->x == -10 && e->h == 400);

	geomem_set(&gm, "foot", "DP-1", 1, 2, 300, 200);
	e = geomem_find(&gm, "foot");
	CHECK("CFG-11", e && e->w == 300 && !e->from_disk);
	CHECK_EQ("CFG-11", gm.n, 2);
	CHECK("CFG-11", strcmp(gm.entries[0].app_id, "foot") == 0); /* most recent first */

	geomem_set(&gm, "we\tird\nid", "DP-1", 0, 0, 1, 1);
	CHECK("CFG-11", geomem_find(&gm, "we ird id") != NULL);

	char *text = geomem_serialize(&gm);
	struct geomem back = {0};
	geomem_parse(&back, text);
	CHECK_EQ("CFG-11", back.n, gm.n);
	CHECK("CFG-11", geomem_find(&back, "firefox")->y == 60);
	free(text);

	for (int i = 0; i < GEOMEM_MAX + 20; i++) {
		char id[16];
		snprintf(id, sizeof(id), "app%d", i);
		geomem_set(&gm, id, "DP-1", i, i, 10, 10);
	}
	CHECK_EQ("CFG-11", gm.n, GEOMEM_MAX);
	CHECK("CFG-11", geomem_find(&gm, "firefox") == NULL);
	CHECK("CFG-11", geomem_find(&gm, "app519") != NULL);
	geomem_clear(&gm);
	geomem_clear(&back);
	return UNIT_RESULT();
}
