/* Unit tests for outline geometry (OUT-2, OUT-4, OUT-9, OUT-10). */
#include <string.h>
#include "check.h"
#include "geometry.h"

int main(void) {
	char buf[64];
	geo_format(buf, sizeof(buf), 560, 300, 380, 690);
	CHECK("OUT-4", strcmp(buf, "560\xc3\x97" "300+380+690") == 0);
	geo_format(buf, sizeof(buf), 420, 300, -12, 0);
	CHECK("OUT-4", strcmp(buf, "420\xc3\x97" "300-12+0") == 0);

	struct geo_box b = {100, 100, 300, 200};
	struct geo_limits none = {0};
	struct geo_box r = geo_resize(b, GEO_EDGE_BOTTOM | GEO_EDGE_RIGHT, 50, 20, none);
	CHECK("OUT-9", r.x == 100 && r.y == 100 && r.w == 350 && r.h == 220);
	r = geo_resize(b, GEO_EDGE_TOP | GEO_EDGE_LEFT, 50, 20, none);
	CHECK("OUT-9", r.x == 150 && r.y == 120 && r.w == 250 && r.h == 180);
	r = geo_resize(b, GEO_EDGE_LEFT, 1000, 0, (struct geo_limits){.min_w = 90, .min_h = 30});
	CHECK("OUT-10", r.w == 90 && r.x == 400 - 90);
	r = geo_resize(b, GEO_EDGE_RIGHT, 1000, 0, (struct geo_limits){.max_w = 500});
	CHECK("OUT-10", r.w == 500 && r.x == 100);

	CHECK_EQ("OUT-9", geo_edges_at(b, 110, 110), GEO_EDGE_TOP | GEO_EDGE_LEFT);
	CHECK_EQ("OUT-9", geo_edges_at(b, 250, 110), GEO_EDGE_TOP);
	CHECK_EQ("OUT-9", geo_edges_at(b, 390, 290), GEO_EDGE_BOTTOM | GEO_EDGE_RIGHT);
	CHECK_EQ("OUT-9", geo_edges_at(b, 250, 200), GEO_EDGE_BOTTOM | GEO_EDGE_RIGHT);
	CHECK_EQ("OUT-9", geo_edges_at(b, 110, 200), GEO_EDGE_LEFT);

	int a, c;
	geo_thirds(420, &a, &c);
	CHECK("OUT-2", a == 140 && c == 280);
	return UNIT_RESULT();
}
