#ifndef TWM_IPC_H
#define TWM_IPC_H

#include <stdbool.h>
#include <stddef.h>

struct twm_server;

/*
 * CFG-12: a line-based UNIX socket at $XDG_RUNTIME_DIR/twm.<display>.sock
 * (also exported as $TWM_SOCK). Requests: "reload". The answer is text, then
 * the server closes the connection.
 */
void ipc_init(struct twm_server *server);
void ipc_finish(struct twm_server *server);

/* Socket path for a Wayland display name; false if XDG_RUNTIME_DIR is unset. */
bool ipc_socket_path(const char *display, char *buf, size_t len);

/* Client side, for `twm --reload`: send `request`, print the answer to
 * stdout, return 0 if it starts with "config ok". */
int ipc_request(const char *request);

#endif
