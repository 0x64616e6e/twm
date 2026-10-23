#ifndef TWM_GRAB_H
#define TWM_GRAB_H

#include <stdbool.h>
#include <stdint.h>
#include <xkbcommon/xkbcommon.h>
#include "server.h"

/*
 * Interactive operations that take over the pointer: move, resize and
 * click-to-place (OUT-*). They show the outline and touch the window only
 * when they end (OUT-1).
 */

bool grab_active(struct twm_server *server);

void grab_begin_pending_move(struct twm_server *server, struct twm_view *view,
	uint32_t button);
void grab_begin_move(struct twm_server *server, struct twm_view *view, uint32_t button);
/* GRP-5: B2 drags one tab of a group. */
void grab_begin_tab(struct twm_server *server, struct twm_view *member, uint32_t button);
/* ROOT-10: B2 drags a root icon. */
void grab_begin_icon(struct twm_server *server, struct twm_view *view, uint32_t button);
void grab_begin_resize(struct twm_server *server, struct twm_view *view,
	uint32_t button, uint32_t edges);

/* Queue a new window for click-to-place. False if it can't be placed
 * interactively (no outputs). */
bool grab_place_enqueue(struct twm_server *server, struct twm_view *view);

void grab_motion(struct twm_server *server);
/* Returns true when the grab consumed the button event. */
bool grab_button(struct twm_server *server, uint32_t button, bool pressed);
/* Returns true when the grab consumed the key. */
bool grab_key(struct twm_server *server, xkb_keysym_t sym, uint32_t mods);

/* Keyboard modifiers changed (SNP-7: M pressed during a drag = free). */
void grab_modifiers(struct twm_server *server, uint32_t mods);

/* Abandon any grab as Esc would (a trusted prompt opened, TRU-4). */
void grab_cancel(struct twm_server *server);
/* Start a waiting placement, if any (the prompt closed). */
void grab_resume(struct twm_server *server);

/* The view unmapped or is going away. */
void grab_view_gone(struct twm_server *server, struct twm_view *view);

#endif
