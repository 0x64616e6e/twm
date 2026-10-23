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

#endif
