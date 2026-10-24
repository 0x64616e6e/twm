#ifndef TWM_INFO_H
#define TWM_INFO_H

#include <stddef.h>

struct twm_server;

/*
 * FET-3: the `info` request, read-only: what twm knows that a generic
 * fetch tool can't — counts, outputs, the config's state and the theme's
 * colors. Never a window's title or app id. One fact a line, fields split
 * by tabs.
 */
void info_write(struct twm_server *server, char *out, size_t len);

struct info_counts {
	int open, iconified, groups;
};
void info_count(struct twm_server *server, struct info_counts *c);

#endif
