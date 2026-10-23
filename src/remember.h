#ifndef TWM_REMEMBER_H
#define TWM_REMEMBER_H

#include <stdbool.h>

struct twm_server;
struct twm_view;

/*
 * Geometry remembered per app id (CFG-11, OUT-16), kept in
 * $XDG_STATE_HOME/twm/geometry and written at most once a second.
 */
void remember_init(struct twm_server *server);
void remember_finish(struct twm_server *server);

/* The view's frame was placed, moved or resized, or it is going away. */
void remember_record(struct twm_server *server, struct twm_view *view);

/* A client size to offer at the first configure, if the app id is known. */
bool remember_initial_size(struct twm_server *server, struct twm_view *view,
	const char *app_id, int *w, int *h);

/* Put a new view where it was last time; true if it was restored (then it
 * is shown and the root line says so). */
bool remember_restore(struct twm_server *server, struct twm_view *view);

#endif
