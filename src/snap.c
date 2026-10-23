#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "snap.h"

/* Lower is preferred on a tie (SNP-2: frame edges beat thirds). */
static int rank(enum snap_target t) {
	switch (t) {
	case SNAP_NEIGHBOR: return 0;
	case SNAP_OUTPUT_EDGE: return 1;
	case SNAP_THIRD_1:
	case SNAP_THIRD_2: return 2;
	case SNAP_CENTRE: return 3;
	case SNAP_NONE: break;
	}
	return 9;
}

struct best {
	struct snap_axis axis;
	int dist;
	int delta;
};

static void consider(struct best *b, int edge, int target, enum snap_target kind,
		enum snap_side side, const char *name, int limit) {
	int d = abs(target - edge);
	if (d > limit) {
		return;
	}
	if (b->axis.target != SNAP_NONE &&
			(d > b->dist || (d == b->dist && rank(kind) >= rank(b->axis.target)))) {
		return;
	}
	b->axis = (struct snap_axis){.target = kind, .side = side, .coord = target, .neighbor = name};
	b->dist = d;
	b->delta = target - edge;
}

/* Candidates for one edge of the outline on one axis. `horizontal` means the
 * edge is a horizontal line (top/bottom/centre-y), compared with y values. */
static void axis_edge(struct best *b, const struct snap_params *p,
		const struct snap_neighbor *nb, size_t n, struct geo_box box,
		enum snap_side side, int edge) {
	bool horizontal = side == SNAP_SIDE_TOP || side == SNAP_SIDE_BOTTOM ||
		side == SNAP_SIDE_CENTRE_Y;
	int o0 = horizontal ? p->output.y : p->output.x;
	int olen = horizontal ? p->output.h : p->output.w;
	int lim = p->distance;

	if (side == SNAP_SIDE_CENTRE_X || side == SNAP_SIDE_CENTRE_Y) {
		if (p->flags & SNAP_CENTER) {
			consider(b, edge, o0 + olen / 2, SNAP_CENTRE, side, NULL, lim);
		}
		return;
	}
	if (p->flags & SNAP_EDGES) {
		for (size_t i = 0; i < n; i++) {
			struct geo_box o = nb[i].box;
			/* Only neighbours beside the outline on the other axis. */
			bool near = horizontal ?
				(box.x < o.x + o.w + lim && o.x < box.x + box.w + lim) :
				(box.y < o.y + o.h + lim && o.y < box.y + box.h + lim);
			if (!near) {
				continue;
			}
			int a = horizontal ? o.y : o.x;
			int z = horizontal ? o.y + o.h : o.x + o.w;
			consider(b, edge, a, SNAP_NEIGHBOR, side, nb[i].name, lim);
			consider(b, edge, z, SNAP_NEIGHBOR, side, nb[i].name, lim);
		}
		consider(b, edge, o0, SNAP_OUTPUT_EDGE, side, NULL, lim);
		consider(b, edge, o0 + olen, SNAP_OUTPUT_EDGE, side, NULL, lim);
	}
	if (p->flags & SNAP_THIRDS) {
		consider(b, edge, o0 + olen / 3, SNAP_THIRD_1, side, NULL, lim);
		consider(b, edge, o0 + olen * 2 / 3, SNAP_THIRD_2, side, NULL, lim);
	}
}

void snap_move(const struct snap_params *p, const struct snap_neighbor *nb, size_t n,
		struct geo_box box, struct snap_result *out) {
	struct best bx = {0}, by = {0};
	axis_edge(&bx, p, nb, n, box, SNAP_SIDE_LEFT, box.x);
	axis_edge(&bx, p, nb, n, box, SNAP_SIDE_RIGHT, box.x + box.w);
	axis_edge(&bx, p, nb, n, box, SNAP_SIDE_CENTRE_X, box.x + box.w / 2);
	axis_edge(&by, p, nb, n, box, SNAP_SIDE_TOP, box.y);
	axis_edge(&by, p, nb, n, box, SNAP_SIDE_BOTTOM, box.y + box.h);
	axis_edge(&by, p, nb, n, box, SNAP_SIDE_CENTRE_Y, box.y + box.h / 2);
	out->box = box;
	out->box.x += bx.delta;
	out->box.y += by.delta;
	out->x = bx.axis;
	out->y = by.axis;
}

void snap_resize(const struct snap_params *p, const struct snap_neighbor *nb, size_t n,
		struct geo_box box, uint32_t edges, struct geo_limits lim, struct snap_result *out) {
	struct best bx = {0}, by = {0};
	if (edges & GEO_EDGE_LEFT) {
		axis_edge(&bx, p, nb, n, box, SNAP_SIDE_LEFT, box.x);
	} else if (edges & GEO_EDGE_RIGHT) {
		axis_edge(&bx, p, nb, n, box, SNAP_SIDE_RIGHT, box.x + box.w);
	}
	if (edges & GEO_EDGE_TOP) {
		axis_edge(&by, p, nb, n, box, SNAP_SIDE_TOP, box.y);
	} else if (edges & GEO_EDGE_BOTTOM) {
		axis_edge(&by, p, nb, n, box, SNAP_SIDE_BOTTOM, box.y + box.h);
	}
	/* Move only the snapped edge, then keep within the limits. */
	struct geo_box s = geo_resize(box, edges,
		(edges & GEO_EDGE_LEFT) || (edges & GEO_EDGE_RIGHT) ? bx.delta : 0,
		(edges & GEO_EDGE_TOP) || (edges & GEO_EDGE_BOTTOM) ? by.delta : 0, lim);
	out->box = s;
	out->x = bx.axis;
	out->y = by.axis;
	/* A snap the limits undid didn't happen. */
	if (bx.axis.target != SNAP_NONE) {
		int e = (edges & GEO_EDGE_LEFT) ? s.x : s.x + s.w;
		if (e != bx.axis.coord) {
			out->x.target = SNAP_NONE;
		}
	}
	if (by.axis.target != SNAP_NONE) {
		int e = (edges & GEO_EDGE_TOP) ? s.y : s.y + s.h;
		if (e != by.axis.coord) {
			out->y.target = SNAP_NONE;
		}
	}
}

/* A window's short name: the first word of its title ("zpool status" →
 * "zpool"), as B3 writes it. */
static void short_name(const char *name, char *buf, size_t len, bool upper) {
	size_t i = 0;
	for (; name && name[i] && name[i] != ' ' && i + 1 < len && i < 24; i++) {
		buf[i] = upper ? (char)toupper((unsigned char)name[i]) : name[i];
	}
	buf[i] = '\0';
}

void snap_label(const struct snap_axis *a, bool vertical, char *buf, size_t len) {
	const char *dim = vertical ? "WIDTH" : "HEIGHT";
	char name[32];
	switch (a->target) {
	case SNAP_NEIGHBOR:
		short_name(a->neighbor, name, sizeof(name), true);
		snprintf(buf, len, "EDGE \xc2\xb7 %s", name);
		return;
	case SNAP_OUTPUT_EDGE:
		snprintf(buf, len, "OUTPUT EDGE");
		return;
	case SNAP_THIRD_1:
		snprintf(buf, len, "\xe2\x85\x93 %s", dim);
		return;
	case SNAP_THIRD_2:
		snprintf(buf, len, "\xe2\x85\x94 %s", dim);
		return;
	case SNAP_CENTRE:
		snprintf(buf, len, "CENTRE");
		return;
	case SNAP_NONE:
		break;
	}
	buf[0] = '\0';
}

static const char *side_name(enum snap_side s) {
	switch (s) {
	case SNAP_SIDE_LEFT: return "left";
	case SNAP_SIDE_RIGHT: return "right";
	case SNAP_SIDE_TOP: return "top";
	case SNAP_SIDE_BOTTOM: return "bottom";
	case SNAP_SIDE_CENTRE_X:
	case SNAP_SIDE_CENTRE_Y: return "centre";
	}
	return "";
}

static void hint_part(const struct snap_axis *a, char *buf, size_t len) {
	char target[32];
	switch (a->target) {
	case SNAP_NEIGHBOR:
		short_name(a->neighbor, target, sizeof(target), false);
		break;
	case SNAP_OUTPUT_EDGE:
		snprintf(target, sizeof(target), "edge");
		break;
	case SNAP_THIRD_1:
		snprintf(target, sizeof(target), "\xe2\x85\x93");
		break;
	case SNAP_THIRD_2:
		snprintf(target, sizeof(target), "\xe2\x85\x94");
		break;
	case SNAP_CENTRE:
		snprintf(target, sizeof(target), "centre");
		break;
	case SNAP_NONE:
		buf[0] = '\0';
		return;
	}
	snprintf(buf, len, "%s \xe2\x86\x92 %s", side_name(a->side), target);
}

void snap_hint(const struct snap_result *r, char *buf, size_t len) {
	char x[64], y[64];
	hint_part(&r->x, x, sizeof(x));
	hint_part(&r->y, y, sizeof(y));
	if (*x && *y) {
		snprintf(buf, len, "%s \xc2\xb7 %s", x, y);
	} else {
		snprintf(buf, len, "%s", *x ? x : y);
	}
}

static bool overlaps(struct geo_box a, struct geo_box b) {
	return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

static bool contains(struct geo_box a, int px, int py) {
	return px >= a.x && px < a.x + a.w && py >= a.y && py < a.y + a.h;
}

bool snap_fit(struct geo_box out, const struct geo_box *obs, size_t n,
		int px, int py, struct geo_box *result) {
	if (!contains(out, px, py)) {
		return false;
	}
	for (size_t i = 0; i < n; i++) {
		if (contains(obs[i], px, py)) {
			return false;
		}
	}
	/* Candidate sides: the output's, and every obstacle edge on the
	 * pointer's side of it. */
	int *lefts = malloc((n + 1) * sizeof(int)), *rights = malloc((n + 1) * sizeof(int));
	int *tops = malloc((n + 1) * sizeof(int)), *bottoms = malloc((n + 1) * sizeof(int));
	size_t nl = 0, nr = 0, nt = 0, nb = 0;
	lefts[nl++] = out.x;
	rights[nr++] = out.x + out.w;
	tops[nt++] = out.y;
	bottoms[nb++] = out.y + out.h;
	for (size_t i = 0; i < n; i++) {
		struct geo_box o = obs[i];
		if (o.x + o.w <= px && o.x + o.w > out.x) {
			lefts[nl++] = o.x + o.w;
		}
		if (o.x > px && o.x < out.x + out.w) {
			rights[nr++] = o.x;
		}
		if (o.y + o.h <= py && o.y + o.h > out.y) {
			tops[nt++] = o.y + o.h;
		}
		if (o.y > py && o.y < out.y + out.h) {
			bottoms[nb++] = o.y;
		}
	}
	long best_area = -1;
	for (size_t a = 0; a < nl; a++) {
		for (size_t b = 0; b < nr; b++) {
			for (size_t c = 0; c < nt; c++) {
				for (size_t d = 0; d < nb; d++) {
					struct geo_box r = {lefts[a], tops[c],
						rights[b] - lefts[a], bottoms[d] - tops[c]};
					long area = (long)r.w * r.h;
					if (r.w <= 0 || r.h <= 0 || area <= best_area) {
						continue;
					}
					bool clear = true;
					for (size_t i = 0; i < n && clear; i++) {
						clear = !overlaps(r, obs[i]);
					}
					if (clear) {
						best_area = area;
						*result = r;
					}
				}
			}
		}
	}
	free(lefts);
	free(rights);
	free(tops);
	free(bottoms);
	return best_area > 0;
}
