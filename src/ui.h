#ifndef TWM_UI_H
#define TWM_UI_H

struct twm_server;

/*
 * Something the Icon Manager, Panner, root icons or history show has changed.
 * Redraws are coalesced into one per event-loop pass.
 */
void ui_changed(struct twm_server *server);
void ui_init(struct twm_server *server);
void ui_finish(struct twm_server *server);

#endif
