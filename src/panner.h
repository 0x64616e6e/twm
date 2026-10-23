#ifndef TWM_PANNER_H
#define TWM_PANNER_H

struct twm_server;

void panner_init(struct twm_server *server);
void panner_finish(struct twm_server *server);
void panner_refresh(struct twm_server *server);

#endif
