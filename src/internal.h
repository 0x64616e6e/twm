#ifndef TWM_INTERNAL_H
#define TWM_INTERNAL_H

#include "server.h"
#include "view.h"

/*
 * Windows twm draws itself: history now, the Icon Manager, Panner and log
 * from M4 (FRM-18). They are ordinary views (frames, focus, stacking, moves,
 * remembered geometry) whose content is a pixman buffer instead of a client.
 */

struct twm_pixbuf;
struct twm_internal_view;

/* Draw the content at `scale`; set *buf (device px) and the logical size. */
typedef void (*internal_render_fn)(struct twm_internal_view *iv, float scale,
	struct twm_pixbuf **buf, int *lw, int *lh);

struct twm_internal_view {
	struct twm_view view;
	char title[64];
	char app_id[64];
	internal_render_fn render;
	void *data;
	struct wlr_scene_buffer *buffer;
	bool positioned;
};

struct twm_internal_view *internal_view_create(struct twm_server *server,
	const char *title, const char *app_id, internal_render_fn render, void *data);
void internal_view_destroy(struct twm_internal_view *iv);

void internal_view_show(struct twm_internal_view *iv);
void internal_view_hide(struct twm_internal_view *iv);
void internal_view_toggle(struct twm_internal_view *iv);
/* Re-render the content (its size may change). */
void internal_view_redraw(struct twm_internal_view *iv);

#endif
