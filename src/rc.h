#ifndef TWM_RC_H
#define TWM_RC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

struct twm_server;
struct twm_config;

/* CFG-1: -f path, else $TWMRC, else ~/.twmrc. Returns a malloc'd path. */
char *rc_path(const char *override);

/* Whole file into *out (NUL-terminated). False with *missing set when the
 * file doesn't exist, or with errno for other failures. */
bool rc_read(const char *path, char **out, size_t *len, bool *missing);

/* CFG-4: twm --check [file]; prints to `out`/`err`, returns the exit code. */
int rc_check(const char *path, FILE *out, FILE *err);

/* Startup: load the config (or the built-in defaults) and apply it. */
void rc_init(struct twm_server *server, const char *path_override);
void rc_finish(struct twm_server *server);

/*
 * CFG-5 … CFG-7: re-read the file; apply it only if it has no errors.
 * `reply` (may be NULL) receives the result for `twm --reload`. `force`
 * reloads even when the text hasn't changed. Returns true when applied.
 */
bool rc_reload(struct twm_server *server, bool force, char *reply, size_t reply_len);

/* Push the current config into the theme, frames, roots and outputs. */
void rc_apply(struct twm_server *server, const struct twm_config *old);

#endif
