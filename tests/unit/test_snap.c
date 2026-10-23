/* Unit tests for snapping and fit (SNP-1 … SNP-5, OUT-13). */
#include <string.h>
#include "check.h"
#include "snap.h"

int main(void) {
	/* B3: an 860x640 screen, zpool at (40,50) 340x266, xclock (640,40) 180x156. */
	struct snap_params p = {.output = {0, 0, 860, 640}, .flags = SNAP_EDGES | SNAP_THIRDS,
		.distance = 12};
	struct snap_neighbor nb[] = {
		{{40, 50, 340, 266}, "zpool status"},
		{{640, 40, 180, 156}, "xclock"},
	};
	struct snap_result r;
	snap_move(&p, nb, 2, (struct geo_box){384, 210, 420, 300}, &r);
	CHECK("SNP-1", r.box.x == 380 && r.box.y == 213 && r.box.w == 420 && r.box.h == 300);
	CHECK("SNP-1", r.x.target == SNAP_NEIGHBOR && r.x.side == SNAP_SIDE_LEFT && r.x.coord == 380);
	CHECK("SNP-1", r.y.target == SNAP_THIRD_1 && r.y.side == SNAP_SIDE_TOP && r.y.coord == 213);
	char buf[128];
	snap_label(&r.x, true, buf, sizeof(buf));
	CHECK("SNP-3", strcmp(buf, "EDGE \xc2\xb7 ZPOOL") == 0);
	snap_label(&r.y, false, buf, sizeof(buf));
	CHECK("SNP-3", strcmp(buf, "\xe2\x85\x93 HEIGHT") == 0);
	snap_hint(&r, buf, sizeof(buf));
	CHECK("SNP-5", strcmp(buf, "left \xe2\x86\x92 zpool \xc2\xb7 top \xe2\x86\x92 \xe2\x85\x93") == 0);

	/* Out of reach: nothing snaps, no hint. */
	snap_move(&p, nb, 2, (struct geo_box){450, 240, 200, 100}, &r);
	CHECK("SNP-2", r.box.x == 450 && r.box.y == 240 && r.x.target == SNAP_NONE &&
		r.y.target == SNAP_NONE);
	snap_hint(&r, buf, sizeof(buf));
	CHECK("SNP-5", buf[0] == '\0');

	/* SNP-2: nearest wins; a tie goes to the frame edge over a third. */
	struct snap_params p3 = {.output = {0, 0, 900, 900}, .flags = SNAP_EDGES | SNAP_THIRDS,
		.distance = 12};
	struct snap_neighbor at_third[] = {{{0, 0, 300, 400}, "notes"}}; /* right edge at x=300 = ⅓ */
	snap_move(&p3, at_third, 1, (struct geo_box){305, 100, 200, 200}, &r);
	CHECK("SNP-2", r.box.x == 300 && r.x.target == SNAP_NEIGHBOR);
	snap_move(&p3, NULL, 0, (struct geo_box){305, 100, 200, 200}, &r);
	CHECK("SNP-2", r.box.x == 300 && r.x.target == SNAP_THIRD_1);

	/* Output edges; right and bottom edges snap too. */
	snap_move(&p, NULL, 0, (struct geo_box){455, 345, 400, 290}, &r);
	CHECK("SNP-1", r.box.x == 460 && r.box.y == 350 && r.x.side == SNAP_SIDE_RIGHT &&
		r.x.target == SNAP_OUTPUT_EDGE && r.y.target == SNAP_OUTPUT_EDGE);
	snap_label(&r.x, true, buf, sizeof(buf));
	CHECK("SNP-3", strcmp(buf, "OUTPUT EDGE") == 0);

	/* Neighbours far away on the other axis don't count. */
	struct snap_neighbor far[] = {{{100, 600, 200, 30}, "far"}};
	struct snap_params pe = {.output = {0, 0, 2000, 2000}, .flags = SNAP_EDGES, .distance = 12};
	snap_move(&pe, far, 1, (struct geo_box){305, 100, 200, 100}, &r);
	CHECK("SNP-1", r.x.target == SNAP_NONE);

	/* Centre, when asked for. */
	struct snap_params pc = {.output = {0, 0, 1000, 800}, .flags = SNAP_CENTER, .distance = 12};
	snap_move(&pc, NULL, 0, (struct geo_box){395, 290, 200, 200}, &r);
	CHECK("SNP-1", r.box.x == 400 && r.box.y == 300 && r.x.target == SNAP_CENTRE);
	snap_label(&r.y, false, buf, sizeof(buf));
	CHECK("SNP-3", strcmp(buf, "CENTRE") == 0);

	/* Resizing moves only the dragged edge. */
	struct geo_limits lim = {.min_w = 90, .min_h = 30};
	/* Beside xclock (y 40..196), right edge 5 px short of its left edge. */
	snap_resize(&p, nb, 2, (struct geo_box){400, 150, 235, 200},
		GEO_EDGE_RIGHT | GEO_EDGE_BOTTOM, lim, &r);
	CHECK("SNP-1", r.box.x == 400 && r.box.w == 240 && r.x.target == SNAP_NEIGHBOR);
	CHECK("SNP-1", r.box.y == 150 && r.box.h == 200 && r.y.target == SNAP_NONE);
	snap_resize(&p, nb, 2, (struct geo_box){384, 300, 200, 200}, GEO_EDGE_LEFT, lim, &r);
	CHECK("SNP-1", r.box.x == 380 && r.box.w == 204);
	/* A snap the limits undo isn't reported. */
	struct geo_limits tight = {.min_w = 90, .min_h = 30, .max_w = 236};
	snap_resize(&p, nb, 2, (struct geo_box){400, 150, 235, 200}, GEO_EDGE_RIGHT, tight, &r);
	CHECK("OUT-10", r.box.w == 236 && r.x.target == SNAP_NONE);

	/* OUT-13: fit. Output 1000x800, two windows; pointer in the gap. */
	struct geo_box obs[] = {{0, 0, 400, 300}, {600, 0, 400, 800}};
	struct geo_box f;
	CHECK("OUT-13", snap_fit((struct geo_box){0, 0, 1000, 800}, obs, 2, 500, 100, &f));
	CHECK("OUT-13", f.x == 400 && f.y == 0 && f.w == 200 && f.h == 800);
	CHECK("OUT-13", snap_fit((struct geo_box){0, 0, 1000, 800}, obs, 2, 100, 500, &f));
	CHECK("OUT-13", f.x == 0 && f.y == 300 && f.w == 600 && f.h == 500);
	CHECK("OUT-13", !snap_fit((struct geo_box){0, 0, 1000, 800}, obs, 2, 100, 100, &f));
	CHECK("OUT-13", snap_fit((struct geo_box){0, 0, 1000, 800}, NULL, 0, 5, 5, &f) &&
		f.w == 1000 && f.h == 800);
	return UNIT_RESULT();
}
