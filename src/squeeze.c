#include "squeeze.h"

/* A hatch narrower than this no longer reads as a texture. */
#define TAB_MIN_HATCH 8

static int fixed_width(const struct tab_metrics *m, bool iconify, bool resize) {
	int w = 2 * m->pad;
	if (iconify) {
		w += m->title_height + m->separator;
	}
	if (resize) {
		w += m->separator + m->title_height;
	}
	return w;
}

int tab_min_width(const struct tab_metrics *m, bool iconify, bool resize) {
	return fixed_width(m, iconify, resize) + m->ellipsis_width;
}

void tab_layout_compute(const struct tab_metrics *m, const struct tab_input *in,
		struct tab_layout *out) {
	*out = (struct tab_layout){0};
	int fixed = fixed_width(m, in->iconify, in->resize);
	int hatch = in->hatch ? m->hatch_width + m->hatch_gap : 0;
	int text = in->text_width;

	int natural = fixed + text + hatch;
	int width = natural;
	if (in->justify == TWM_JUSTIFY_OFF || natural > in->frame_width) {
		width = in->frame_width;
	}

	/*
	 * FRM-5: the title gives way first, down to an ellipsis; then the hatch
	 * shrinks, since it is the focus texture (P-5); buttons always stay.
	 */
	int room = width - fixed;
	if (room < 0) {
		room = 0;
	}
	if (text + hatch > room) {
		text = room - hatch;
		if (text < m->ellipsis_width) {
			text = m->ellipsis_width < room ? m->ellipsis_width : room;
			hatch = room - text;
			if (hatch < m->hatch_gap + TAB_MIN_HATCH) {
				text = room;
				hatch = 0;
			}
		}
		out->truncated = text < in->text_width;
	} else if (in->justify == TWM_JUSTIFY_OFF && in->hatch) {
		/* FRM-4: unsqueezed, the hatch fills what the title leaves. */
		hatch = room - text;
	}

	int x = 0;
	if (in->iconify) {
		out->iconify = (struct tab_span){x, m->title_height};
		x += m->title_height;
		out->sep_left = (struct tab_span){x, m->separator};
		x += m->separator;
	}
	out->text = (struct tab_span){x + m->pad, text};
	x += m->pad + text + m->pad;
	if (hatch > 0) {
		out->hatch = (struct tab_span){x, hatch - m->hatch_gap};
		x += hatch;
	}
	if (in->resize) {
		/* With `off`, the resize box sits at the frame's right edge. */
		x = width - m->title_height - m->separator;
		out->sep_right = (struct tab_span){x, m->separator};
		x += m->separator;
		out->resize = (struct tab_span){x, m->title_height};
	}
	out->width = width;

	switch (in->justify) {
	case TWM_JUSTIFY_CENTER:
		out->x = (in->frame_width - width) / 2;
		break;
	case TWM_JUSTIFY_RIGHT:
		out->x = in->frame_width - width;
		break;
	case TWM_JUSTIFY_LEFT:
	case TWM_JUSTIFY_OFF:
		out->x = 0;
		break;
	}
}

static bool in_span(struct tab_span s, int x) {
	return s.width > 0 && x >= s.x && x < s.x + s.width;
}

enum tab_part tab_part_at(const struct tab_layout *l, int x) {
	if (x < 0 || x >= l->width) {
		return TAB_PART_NONE;
	}
	if (in_span(l->iconify, x)) {
		return TAB_PART_ICONIFY;
	}
	if (in_span(l->resize, x)) {
		return TAB_PART_RESIZE;
	}
	return TAB_PART_TITLE;
}
