#ifndef TWM_HISTVIEW_H
#define TWM_HISTVIEW_H

#include "history.h"
#include "server.h"

/* The history's compositor side (HIS-1 … HIS-3). */

void histview_init(struct twm_server *server);
void histview_finish(struct twm_server *server);

/* The window's state as the history sees it. */
struct hist_state hist_state_of(struct twm_view *view);

/* A grab finished: record what it did to the window. */
void histview_record(struct twm_server *server, enum hist_op op, struct twm_view *view,
	struct hist_state before, struct hist_state after);

/* HIS-2: M-z / M-S-z on `view`. */
void histview_undo(struct twm_server *server, struct twm_view *view);
void histview_redo(struct twm_server *server, struct twm_view *view);

/* HIS-3: f.history. */
void histview_toggle(struct twm_server *server);
/* Something it shows changed (a grab started or ended). */
void histview_changed(struct twm_server *server);

#endif
