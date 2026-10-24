#ifndef TWM_SHARE_H
#define TWM_SHARE_H

#include <stdbool.h>
#include <stddef.h>

struct twm_server;

/*
 * SHR-1, SHR-3: take the look a fetch shot carries — its appearance keys,
 * merged into this .twmrc and shown as pending edits in the Settings
 * window; Write applies them. The answer for `twmfetch <image>` goes to
 * `reply`, starting "look " when the look was taken.
 */
bool share_absorb(struct twm_server *server, const char *path, char *reply, size_t len);

#endif
