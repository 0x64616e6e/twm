#ifndef TWM_SQUEEZE_H
#define TWM_SQUEEZE_H

#include <stdbool.h>

/*
 * Title tab layout (FRM-3 … FRM-6, FRM-11). Pure arithmetic in logical px;
 * no wlroots, so it is unit-tested on its own.
 */

enum twm_justify {
	TWM_JUSTIFY_LEFT,
	TWM_JUSTIFY_CENTER,
	TWM_JUSTIFY_RIGHT,
	TWM_JUSTIFY_OFF, /* tab spans the frame (classic unsqueezed title) */
};

struct tab_metrics {
	int title_height; /* also the button box size */
	int separator;
	int pad;          /* title padding, each side */
	int hatch_width;
	int hatch_gap;    /* between hatch and resize box */
	int ellipsis_width; /* narrowest a cut title may get */
};

struct tab_input {
	int frame_width;
	int text_width;   /* natural title width */
	bool hatch;       /* focused and TitleHighlight on (FRM-10) */
	bool iconify;     /* utility frames have no buttons (FRM-18) */
	bool resize;      /* false for fixed-size clients (FRM-6) */
	enum twm_justify justify;
};

struct tab_span {
	int x, width; /* relative to the tab's left edge; width 0 = absent */
};

struct tab_layout {
	int x;        /* tab left edge, relative to the frame */
	int width;
	struct tab_span iconify, sep_left, text, hatch, sep_right, resize;
	bool truncated; /* title cut to fit (FRM-5) */
};

void tab_layout_compute(const struct tab_metrics *m, const struct tab_input *in,
	struct tab_layout *out);

/* Narrowest frame whose tab still shows both buttons and an ellipsis. */
int tab_min_width(const struct tab_metrics *m, bool iconify, bool resize);

enum tab_part {
	TAB_PART_NONE,
	TAB_PART_ICONIFY,
	TAB_PART_TITLE,
	TAB_PART_RESIZE,
};

/* Which part of the tab is at x (relative to the tab's left edge). */
enum tab_part tab_part_at(const struct tab_layout *l, int x);

/*
 * A tab group's row (GRP-3, B2): iconify box, one squeezed tab per member,
 * resize box. The active tab carries a smaller hatch inside it; every tab is
 * followed by a 1 px separator; a drop slot (GRP-2) can open between tabs.
 */
#define TAB_GROUP_MAX 16
#define TAB_GROUP_HATCH 38      /* B2's 44 px at 26/30 */
#define TAB_GROUP_HATCH_GAP 12  /* title to hatch, inside the active tab */
#define TAB_GROUP_SLOT_MARGIN 4

struct tab_group_input {
	int frame_width;
	int n;
	int text_width[TAB_GROUP_MAX];
	int active;
	bool hatch;     /* focused and TitleHighlight on */
	bool resize;
	bool folded;    /* GRP-6: only the active tab */
	int slot_index; /* -1, or the insertion point 0..n (GRP-2) */
	int slot_width;
	enum twm_justify justify;
};

struct tab_group_span {
	int index;   /* member index; -1 for the drop slot */
	int x, width;
	int text_x, text_w;
	int hatch_x, hatch_w; /* hatch_w 0 = none */
	int sep_x;            /* the separator after it; -1 none */
};

struct tab_group_layout {
	int x, width;
	struct tab_span iconify, sep_left, resize;
	struct tab_group_span tabs[TAB_GROUP_MAX + 1];
	int ntabs;
};

void tab_group_layout_compute(const struct tab_metrics *m, const struct tab_group_input *in,
	struct tab_group_layout *out);
/* Member index of the tab at x (relative to the row), or -1. */
int tab_group_tab_at(const struct tab_group_layout *l, int x);
/* Insertion point for a tab dropped at x: 0..n (GRP-2). */
int tab_group_insert_at(const struct tab_group_layout *l, int x, int n);

#endif
