#ifndef TWM_GEOMEM_H
#define TWM_GEOMEM_H

#include <stdbool.h>
#include <stddef.h>

/*
 * Remembered window geometry per app id (CFG-11). Plain text, one window per
 * line:  app_id <TAB> output <TAB> x y w h
 * x, y: the frame's top-left relative to the output; w, h: the client size.
 * Pure; the compositor side lives in remember.c.
 */

#define GEOMEM_MAX 500

struct geomem_entry {
	char app_id[128];
	char output[64];
	int x, y, w, h;
	bool from_disk; /* loaded at startup and not updated since */
};

struct geomem {
	struct geomem_entry *entries; /* most recently used first */
	size_t n;
};

/* Replace the contents with `text`; bad lines are skipped. */
void geomem_parse(struct geomem *gm, const char *text);
/* malloc'd file contents. */
char *geomem_serialize(const struct geomem *gm);
const struct geomem_entry *geomem_find(const struct geomem *gm, const char *app_id);
void geomem_set(struct geomem *gm, const char *app_id, const char *output,
	int x, int y, int w, int h);
void geomem_clear(struct geomem *gm);

#endif
