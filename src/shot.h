#ifndef TWM_SHOT_H
#define TWM_SHOT_H

#include <stdbool.h>
#include <stddef.h>

struct twm_server;

/*
 * Screenshots (SHOT-*), taken by the compositor itself: the output under
 * the pointer, rendered into a buffer of twm's own. A fetch shot draws the
 * root card in and carries the .twmrc (SHOT-2).
 */

/* f.screenshot / f.fetchshot. The file's path into `path`, or false with
 * the reason in `path`. */
bool shot_take(struct twm_server *server, bool fetch, char *path, size_t len);

#endif
