#include <stdio.h>
#include "geometry.h"

static int clamp_len(int v, int min, int max) {
	if (max > 0 && v > max) {
		v = max;
	}
	if (v < min) {
		v = min;
	}
	return v < 1 ? 1 : v;
}

struct geo_box geo_resize(struct geo_box s, uint32_t edges, int dx, int dy,
		struct geo_limits lim) {
	int left = s.x, top = s.y, right = s.x + s.w, bottom = s.y + s.h;
	if (edges & GEO_EDGE_LEFT) {
		left += dx;
	} else if (edges & GEO_EDGE_RIGHT) {
		right += dx;
	}
	if (edges & GEO_EDGE_TOP) {
		top += dy;
	} else if (edges & GEO_EDGE_BOTTOM) {
		bottom += dy;
	}
	int w = clamp_len(right - left, lim.min_w, lim.max_w);
	int h = clamp_len(bottom - top, lim.min_h, lim.max_h);
	struct geo_box out = {.w = w, .h = h};
	out.x = (edges & GEO_EDGE_LEFT) ? right - w : left;
	out.y = (edges & GEO_EDGE_TOP) ? bottom - h : top;
	return out;
}

uint32_t geo_edges_at(struct geo_box box, int px, int py) {
	int col = (px - box.x) * 3 / (box.w > 0 ? box.w : 1);
	int row = (py - box.y) * 3 / (box.h > 0 ? box.h : 1);
	uint32_t edges = 0;
	if (col <= 0) {
		edges |= GEO_EDGE_LEFT;
	} else if (col >= 2) {
		edges |= GEO_EDGE_RIGHT;
	}
	if (row <= 0) {
		edges |= GEO_EDGE_TOP;
	} else if (row >= 2) {
		edges |= GEO_EDGE_BOTTOM;
	}
	return edges ? edges : (GEO_EDGE_BOTTOM | GEO_EDGE_RIGHT);
}

void geo_format(char *buf, size_t len, int w, int h, int x, int y) {
	/* U+00D7 MULTIPLICATION SIGN, as on the boards. */
	snprintf(buf, len, "%d\xc3\x97%d%s%d%s%d", w, h,
		x < 0 ? "" : "+", x, y < 0 ? "" : "+", y);
}

void geo_thirds(int len, int *first, int *second) {
	*first = len / 3;
	*second = len * 2 / 3;
}
