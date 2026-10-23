#ifndef TWM_GEOMETRY_H
#define TWM_GEOMETRY_H

#include <stddef.h>
#include <stdint.h>

/* Outline geometry (OUT-2 … OUT-10). Pure arithmetic, logical px. */

struct geo_box {
	int x, y, w, h;
};

/* Same values as wlroots' enum wlr_edges. */
enum geo_edge {
	GEO_EDGE_TOP = 1,
	GEO_EDGE_BOTTOM = 2,
	GEO_EDGE_LEFT = 4,
	GEO_EDGE_RIGHT = 8,
};

struct geo_limits {
	int min_w, min_h;
	int max_w, max_h; /* 0 = unbounded */
};

/* Move the given edges of `start` by (dx, dy), clamped to `lim`; the
 * opposite edges stay put (OUT-9, OUT-10). */
struct geo_box geo_resize(struct geo_box start, uint32_t edges, int dx, int dy,
	struct geo_limits lim);

/* OUT-9: which edges a M-B3 resize grabs, by the 3x3 cell under (px, py). */
uint32_t geo_edges_at(struct geo_box box, int px, int py);

/* OUT-4: "560×300+380+690"; a negative coordinate prints as "-12". */
void geo_format(char *buf, size_t len, int w, int h, int x, int y);

/* OUT-2: offsets of the ⅓ and ⅔ lines across a span of `len`. */
void geo_thirds(int len, int *first, int *second);

#endif
