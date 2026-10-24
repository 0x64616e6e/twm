#ifndef TWM_IPC_H
#define TWM_IPC_H

#include <stdbool.h>
#include <stddef.h>
#include "ipcclient.h"

struct twm_server;

/*
 * CFG-12: a line-based UNIX socket at $XDG_RUNTIME_DIR/twm.<display>.sock
 * (also exported as $TWM_SOCK). Requests: "reload". The answer is text, then
 * the server closes the connection.
 */
void ipc_init(struct twm_server *server);
void ipc_finish(struct twm_server *server);


/* Client side, for `twm --reload`: send `request`, print the answer to
 * stdout, return 0 if it starts with "config ok". */
int ipc_request(const char *request);

/* Client side, for `twm --choose [who]`: a dmenu-style chooser for
 * xdg-desktop-portal-wlr. Lines from stdin go into a trusted prompt; the
 * chosen line goes to stdout (0), Deny or Esc prints nothing (1). */
int ipc_choose(const char *who);

#endif
