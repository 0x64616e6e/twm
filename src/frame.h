#ifndef TWM_FRAME_H
#define TWM_FRAME_H

#include <stdbool.h>
#include "server.h"
#include "squeeze.h"

/*
 * A window's frame (FRM-*): the title tab and the 1 px border. The tab is one
 * pixman buffer; the border is four scene rects, or four dashed buffers when
 * the window is urgent (FRM-9).
 */

enum frame_state {
	FRAME_UNFOCUSED,
	FRAME_FOCUSED,
	FRAME_URGENT,
};

struct twm_frame {
	struct twm_view *view;
	struct wlr_scene_tree *tree;
	struct wlr_scene_buffer *tab;
	struct wlr_scene_rect *border[4];   /* top, right, bottom, left */
	struct wlr_scene_buffer *dash[4];
	struct twm_hit tab_hit, border_hit;
	struct tab_layout layout;

	/* What the tab was last drawn from; redraw only when it changes. */
	struct {
		char *title;
		enum frame_state state;
		float scale;
		int frame_width;
		bool resize;
		uint32_t gen;
		bool valid;
	} drawn;
	struct {
		enum frame_state state;
		int width, height;
		float scale;
		int th;
		uint32_t gen;
		bool valid;
	} drawn_border;
};

struct twm_frame *frame_create(struct twm_view *view);
void frame_destroy(struct twm_frame *frame);

/* Re-read title, focus, urgency, size and scale; redraw what changed. */
void frame_update(struct twm_frame *frame);

/* Refine a TWM_HIT_TITLE at layout (lx, ly) into iconify/title/resize. */
enum twm_hit_kind frame_part_at(struct twm_frame *frame, double lx, double ly);

enum frame_state frame_state_of(struct twm_view *view);

/* Smallest frame width that keeps the tab's buttons (OUT-10). */
int frame_min_width(struct twm_frame *frame);

#endif
