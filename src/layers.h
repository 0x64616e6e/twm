#ifndef TWM_LAYERS_H
#define TWM_LAYERS_H

#include <wlr/util/box.h>

struct twm_server;
struct twm_output;
struct wlr_surface;

/*
 * wlr-layer-shell (GEN-2): bars, launchers, lockers and on-screen displays
 * the user runs; twm draws none itself. Exclusive zones shrink the output's
 * usable area, which placement, fit, snapping and twm's own windows respect.
 */
void layers_init(struct twm_server *server);
void layers_finish(struct twm_server *server);
void layers_arrange(struct twm_output *output);

/* The output's area minus exclusive zones. */
struct wlr_box output_usable(struct twm_output *output);

/* A press on a layer surface (keyboard interactivity on demand). */
void layers_pressed(struct twm_server *server, struct wlr_surface *surface);
/* Keyboard focus belongs to an exclusive layer surface right now. */
bool layers_hold_keyboard(struct twm_server *server);

#endif
