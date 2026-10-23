#ifndef TWM_SETWIN_H
#define TWM_SETWIN_H

#include <stdbool.h>

struct twm_server;

/*
 * The Settings window (SET-1 … SET-9, B8): `settings — ~/.twmrc`. Controls
 * that each name the key they write, a live preview, and the diff of the
 * pending changes, always in view. Writing the file is applying it (hot
 * reload); a hand edit of the file updates the controls. Not a second
 * source of truth: what it holds is a list of pending edits to the file.
 */
void setwin_init(struct twm_server *server);
void setwin_finish(struct twm_server *server);
/* f.settings: show it (focused), or hide it if it has the focus. */
void setwin_toggle(struct twm_server *server);
/* The file may have changed (any reload attempt, SET-8). */
void setwin_file_changed(struct twm_server *server);

#endif
