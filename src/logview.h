#ifndef TWM_LOGVIEW_H
#define TWM_LOGVIEW_H

struct twm_server;

/* ROOT-8: M-n toggles a window with the recent root-line messages. */
void logview_init(struct twm_server *server);
void logview_finish(struct twm_server *server);
void logview_toggle(struct twm_server *server);
void logview_refresh(struct twm_server *server);

#endif
