#ifndef TWM_CARD_H
#define TWM_CARD_H

#include <stdbool.h>

struct twm_server;

/*
 * The root card (CARD-*): the machine's facts drawn on the root like the
 * wordmark, under every window, updated once a second.
 */
void card_init(struct twm_server *server);
void card_finish(struct twm_server *server);
/* The config or the outputs changed: show, hide or move it. */
void card_configure(struct twm_server *server);
/* SHOT-2: draw it for a fetch shot even when RootCard is off. */
void card_force(struct twm_server *server, bool on);
/* CARD-3: B1 on it. */
void card_open_fetch(struct twm_server *server);

#endif
