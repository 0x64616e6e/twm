#include "squeeze.h"

/* A hatch narrower than this no longer reads as a texture. */
#define TAB_MIN_HATCH 8

static int fixed_width(const struct tab_metrics *m, bool iconify, bool resize, bool close) {
	int w = 2 * m->pad;
	if (iconify) {
		w += m->title_height + m->separator;
	}
	if (resize) {
		w += m->separator + m->title_height;
	}
	if (close) {
		w += m->separator + m->title_height;
	}
	return w;
}

int tab_min_width(const struct tab_metrics *m, bool iconify, bool resize, bool close) {
	return fixed_width(m, iconify, resize, close) + m->ellipsis_width;
}

void tab_layout_compute(const struct tab_metrics *m, const struct tab_input *in,
		struct tab_layout *out) {
	*out = (struct tab_layout){0};
	int fixed = fixed_width(m, in->iconify, in->resize, in->close) + in->marker_width;
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
	if (in->marker_width) {
		out->marker = (struct tab_span){x, in->marker_width};
		x += in->marker_width;
	}
	if (hatch > 0) {
		out->hatch = (struct tab_span){x, hatch - m->hatch_gap};
		x += hatch;
	}
	/* The boxes on the right, from the tab's right end (with `off`, the
	 * frame's): the resize box last, the close box before it (FRM-19). */
	int right = width;
	if (in->resize) {
		right -= m->title_height;
		out->resize = (struct tab_span){right, m->title_height};
		right -= m->separator;
		out->sep_right = (struct tab_span){right, m->separator};
	}
	if (in->close) {
		right -= m->title_height;
		out->close = (struct tab_span){right, m->title_height};
		right -= m->separator;
		out->sep_close = (struct tab_span){right, m->separator};
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
	if (in_span(l->close, x)) {
		return TAB_PART_CLOSE;
	}
	return TAB_PART_TITLE;
}

void tab_group_layout_compute(const struct tab_metrics *m, const struct tab_group_input *in,
		struct tab_group_layout *out) {
	*out = (struct tab_group_layout){0};
	int n = in->n > TAB_GROUP_MAX ? TAB_GROUP_MAX : in->n;
	int text[TAB_GROUP_MAX];
	bool shown[TAB_GROUP_MAX];
	for (int i = 0; i < n; i++) {
		text[i] = in->text_width[i];
		shown[i] = !in->folded || i == in->active;
	}
	int hatch = in->hatch ? TAB_GROUP_HATCH_GAP + TAB_GROUP_HATCH : 0;
	int slot = in->slot_index >= 0 ? in->slot_width + 2 * TAB_GROUP_SLOT_MARGIN : 0;
	int fixed = m->title_height + m->separator + slot;
	if (in->resize) {
		fixed += m->title_height;
	}
	if (in->close) {
		fixed += m->title_height + (in->resize ? m->separator : 0);
	}
	for (int i = 0; i < n; i++) {
		if (shown[i]) {
			fixed += 2 * m->pad + m->separator + (i == in->active ? hatch : 0);
		}
	}
	/* Too wide: the longest titles give way first, down to an ellipsis. */
	for (;;) {
		int total = fixed;
		int widest = -1;
		for (int i = 0; i < n; i++) {
			if (!shown[i]) {
				continue;
			}
			total += text[i];
			if (widest < 0 || text[i] > text[widest]) {
				widest = i;
			}
		}
		if (total <= in->frame_width || widest < 0 || text[widest] <= m->ellipsis_width) {
			break;
		}
		int over = total - in->frame_width;
		int second = m->ellipsis_width;
		for (int i = 0; i < n; i++) {
			if (shown[i] && i != widest && text[i] > second) {
				second = text[i];
			}
		}
		int cut = text[widest] - second;
		if (cut <= 0) {
			cut = 1;
		}
		if (cut > over) {
			cut = over;
		}
		text[widest] -= cut;
		if (text[widest] < m->ellipsis_width) {
			text[widest] = m->ellipsis_width;
		}
	}

	int x = 0;
	out->iconify = (struct tab_span){x, m->title_height};
	x += m->title_height;
	out->sep_left = (struct tab_span){x, m->separator};
	x += m->separator;
	for (int i = 0; i <= n; i++) {
		if (i == in->slot_index && slot) {
			out->tabs[out->ntabs++] = (struct tab_group_span){
				.index = -1, .x = x + TAB_GROUP_SLOT_MARGIN, .width = in->slot_width,
				.sep_x = -1,
			};
			x += slot;
		}
		if (i == n || !shown[i]) {
			continue;
		}
		struct tab_group_span t = {.index = i, .x = x, .text_x = x + m->pad, .text_w = text[i]};
		int w = 2 * m->pad + text[i];
		if (i == in->active && hatch) {
			t.hatch_x = t.text_x + text[i] + TAB_GROUP_HATCH_GAP;
			t.hatch_w = TAB_GROUP_HATCH;
			w += hatch;
		}
		t.width = w;
		t.sep_x = x + w;
		out->tabs[out->ntabs++] = t;
		x += w + m->separator;
	}
	if (in->close) {
		out->close = (struct tab_span){x, m->title_height};
		x += m->title_height;
		if (in->resize) {
			out->sep_close = (struct tab_span){x, m->separator};
			x += m->separator;
		}
	}
	if (in->resize) {
		out->resize = (struct tab_span){x, m->title_height};
		x += m->title_height;
	}
	out->width = x;
	switch (in->justify) {
	case TWM_JUSTIFY_CENTER:
		out->x = (in->frame_width - x) / 2;
		break;
	case TWM_JUSTIFY_RIGHT:
		out->x = in->frame_width - x;
		break;
	default:
		out->x = 0;
		break;
	}
	if (out->x < 0) {
		out->x = 0;
	}
}

int tab_group_tab_at(const struct tab_group_layout *l, int x) {
	for (int i = 0; i < l->ntabs; i++) {
		const struct tab_group_span *t = &l->tabs[i];
		if (t->index >= 0 && x >= t->x && x < t->x + t->width + 1) {
			return t->index;
		}
	}
	return -1;
}

int tab_group_insert_at(const struct tab_group_layout *l, int x, int n) {
	for (int i = 0; i < l->ntabs; i++) {
		const struct tab_group_span *t = &l->tabs[i];
		if (t->index >= 0 && x < t->x + t->width / 2) {
			return t->index;
		}
	}
	return n;
}
