#ifndef TWM_HINTS_H
#define TWM_HINTS_H

#include <stdbool.h>
#include <xkbcommon/xkbcommon.h>

struct twm_server;
struct twm_view;

/*
 * Hint mode (HNT-1 … HNT-5): M-; puts a home-row letter on every visible
 * tab, nearest to the pointer first; typing it raises and focuses that
 * window. Esc or any other key leaves with nothing changed. It holds the
 * keyboard while it's on.
 */
void hints_begin(struct twm_server *server);
void hints_cancel(struct twm_server *server);
bool hints_active(struct twm_server *server);
/* A key while hint mode is on; always taken (HNT-5). */
void hints_key(struct twm_server *server, xkb_keysym_t sym, const char *utf8);
void hints_view_gone(struct twm_server *server, struct twm_view *view);
void hints_finish(struct twm_server *server);

#endif
