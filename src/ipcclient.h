#ifndef TWM_IPCCLIENT_H
#define TWM_IPCCLIENT_H

#include <stdbool.h>
#include <stddef.h>

/*
 * The client side of twm's socket (D-8), for `twm --reload`, `--choose`
 * and twmfetch. No wlroots.
 */

/* Socket path for a Wayland display name; false if XDG_RUNTIME_DIR is unset. */
bool ipc_socket_path(const char *display, char *buf, size_t len);

/* Connect to the running twm ($TWM_SOCK, else from $WAYLAND_DISPLAY); -1
 * if there is none. `quiet` keeps the reason off stderr. */
int ipcc_connect(bool quiet);

void ipcc_write_all(int fd, const char *s, size_t n);
/* Everything until the server closes, NUL-terminated. */
void ipcc_read_reply(int fd, char *buf, size_t len);

/* One request (a line, without its newline) and its whole answer. False if
 * no twm answers. */
bool ipcc_request(const char *request, char *reply, size_t len, bool quiet);

#endif
