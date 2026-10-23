#ifndef TWM_SNAP_H
#define TWM_SNAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "geometry.h"

/*
 * Outlines that think (SNP-1 … SNP-5, OUT-13). Pure arithmetic on frame
 * boxes in layout coordinates; unit-tested without a compositor.
 */

/* Same values as TWM_SNAP_* in settings.h. */
#define SNAP_EDGES 1u
#define SNAP_THIRDS 2u
#define SNAP_CENTER 4u

struct snap_params {
	struct geo_box output; /* the output the outline is on */
	uint32_t flags;
	int distance;          /* SnapDistance */
};

struct snap_neighbor {
	struct geo_box box;    /* another window's frame */
	const char *name;      /* its title */
};

enum snap_target {
	SNAP_NONE,
	SNAP_NEIGHBOR,
	SNAP_OUTPUT_EDGE,
	SNAP_THIRD_1,
	SNAP_THIRD_2,
	SNAP_CENTRE,
};

enum snap_side {
	SNAP_SIDE_LEFT,
	SNAP_SIDE_RIGHT,
	SNAP_SIDE_TOP,
	SNAP_SIDE_BOTTOM,
	SNAP_SIDE_CENTRE_X,
	SNAP_SIDE_CENTRE_Y,
};

struct snap_axis {
	enum snap_target target;
	enum snap_side side;  /* which edge of the outline snapped */
	int coord;            /* where the guide line goes (layout) */
	const char *neighbor; /* SNAP_NEIGHBOR: its name */
};

struct snap_result {
	struct geo_box box;
	struct snap_axis x; /* a vertical guide */
	struct snap_axis y; /* a horizontal guide */
};

/* A moving outline: all its edges travel together. */
void snap_move(const struct snap_params *p, const struct snap_neighbor *nb, size_t n,
	struct geo_box box, struct snap_result *out);

/* A resizing outline: only `edges` move; the result stays within `lim`. */
void snap_resize(const struct snap_params *p, const struct snap_neighbor *nb, size_t n,
	struct geo_box box, uint32_t edges, struct geo_limits lim, struct snap_result *out);

/* SNP-3: "EDGE · ZPOOL", "⅓ HEIGHT", "OUTPUT EDGE", "CENTRE". */
void snap_label(const struct snap_axis *axis, bool vertical, char *buf, size_t len);

/* SNP-5: "left → zpool · top → ⅓"; empty when nothing snapped. */
void snap_hint(const struct snap_result *r, char *buf, size_t len);

/*
 * OUT-13: the largest rectangle containing (px, py) that stays inside
 * `output` and overlaps none of `obstacles`. False if the point is inside
 * an obstacle or off the output.
 */
bool snap_fit(struct geo_box output, const struct geo_box *obstacles, size_t n,
	int px, int py, struct geo_box *out);

#endif
