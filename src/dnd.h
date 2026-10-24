#ifndef TWM_DND_H
#define TWM_DND_H

#include <stdbool.h>

struct twm_server;

/*
 * Drag and drop between clients (wl_data_device), and a PNG dropped on the
 * bare root takes the look it carries (SHR-1). The drag icon has a layer of
 * its own that hit testing never sees.
 */
void dnd_init(struct twm_server *server);
void dnd_finish(struct twm_server *server);
/* The pointer moved: the icon follows. */
void dnd_motion(struct twm_server *server);
/* A button is about to be released while a drag runs: over the bare root,
 * ask the source for its files. */
void dnd_release(struct twm_server *server);

#endif
