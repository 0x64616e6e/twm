#ifndef TWM_MEDIA_H
#define TWM_MEDIA_H

#include "levels.h"
#include "server.h"

/* Volume and brightness (LVL-*): change it with the system's own tools, in
 * the background, and say the new level on the root line. */
void media_level(struct twm_server *server, enum level_action action);

void media_init(struct twm_server *server);
void media_finish(struct twm_server *server);

#endif
