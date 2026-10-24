#ifndef TWM_IMGR_H
#define TWM_IMGR_H

struct twm_server;

void imgr_init(struct twm_server *server);
void imgr_finish(struct twm_server *server);
void imgr_refresh(struct twm_server *server);
/* f.run (IMG-13): the Icon Manager up, focused, ready to filter. */
void imgr_run(struct twm_server *server);

#endif
