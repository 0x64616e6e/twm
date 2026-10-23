#ifndef TWM_DESKTOP_H
#define TWM_DESKTOP_H

#include <stdbool.h>
#include "history.h"
#include "server.h"
#include "view.h"

/*
 * Window state beyond geometry: virtual desktops (DSK-1, DSK-2), iconify
 * (FRM-13) and squeeze (FRM-14). Windows on another desktop, iconified ones
 * and ones not yet placed are off screen; twm's own windows are on every
 * desktop.
 */

bool view_visible(struct twm_view *view);
/* Apply visibility and squeeze to the scene. */
void view_update_visibility(struct twm_view *view);

void desktop_switch(struct twm_server *server, int desktop);
void desktop_send(struct twm_server *server, struct twm_view *view, int desktop);
/* Number of desktops (Panner { desktops N }). */
int desktop_count(struct twm_server *server);
/* Windows (not twm's own) on a desktop, iconified ones too. */
int desktop_window_count(struct twm_server *server, int desktop);
/* Keep desktop numbers valid after the count changed (reload). */
void desktops_reconfigure(struct twm_server *server);

void view_set_iconified(struct twm_view *view, bool iconified);
/* HIS-2: put a window back into a recorded state, without recording. */
void view_apply_state(struct twm_view *view, struct hist_state s);
void view_set_squeezed(struct twm_view *view, bool squeezed);

/* IMG-5 / IMG-8: bring a window into view, wherever it is, and focus it. */
void view_present(struct twm_view *view);
/* IMG-8: M-Return, pull it onto this desktop instead. */
void view_bring_here(struct twm_view *view);

#endif
