#ifndef TWM_ZOOM_H
#define TWM_ZOOM_H

#include <stdbool.h>
#include "history.h"
#include "server.h"
#include "view.h"

/*
 * Zoom and fullscreen (ZOM-*). twm's f.zoom, f.horizoom and f.fullzoom keep
 * the frame and fill the output's usable area; f.fullscreen shows the
 * client alone over the whole output. While either holds, the window keeps
 * its normal geometry to go back to (ZOM-2).
 */

/* f.zoom / f.horizoom / f.fullzoom: that zoom, or back if it's the one on. */
void view_zoom(struct twm_view *view, enum twm_zoom kind);
/* f.fullscreen and app requests; `output` NULL = where the window is. */
void view_set_fullscreen(struct twm_view *view, bool on, struct twm_output *output);

/* Moved or resized by hand: the zoom ends where it landed (ZOM-2). */
void view_end_zoom(struct twm_view *view);

/* A window that maps already maximized or fullscreen (ZOM-5): open it so on
 * the output under the pointer. False if there's no output. */
bool zoom_open_requested(struct twm_view *view);

/* HIS-2: take the zoom state of a recorded state, before its geometry. */
void zoom_apply_recorded(struct twm_view *view, struct hist_state s);

/* ZOM-4: above the bars while focused and fullscreen, else with the rest. */
void view_update_layer(struct twm_view *view);

/* The geometry to remember for the app (OUT-16): never a zoomed one. */
struct wlr_box view_normal_geometry(struct twm_view *view);

/* A group hands its place to another tab (ZOM-7): the state goes along. */
void zoom_hand_over(struct twm_view *from, struct twm_view *to);

/* The client box an initial configure should offer for a requested state. */
bool zoom_initial_size(struct twm_server *server, bool fullscreen, bool maximized,
	int title_height, int *w, int *h);

#endif
