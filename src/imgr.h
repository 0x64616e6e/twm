#ifndef TWM_IMGR_H
#define TWM_IMGR_H

struct twm_server;

void imgr_init(struct twm_server *server);
void imgr_finish(struct twm_server *server);
void imgr_refresh(struct twm_server *server);

#endif
